#include "notification.h"

#include <QApplication>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QImage>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

#include "hypripc.h"
#include "hyprstate.h"
#include "popup.h"
#include "theme.h"
#include "wlutil.h"

namespace {

constexpr int historyLimit = 20;
constexpr int toastWidth = 360;
constexpr int toastGap = 6;
constexpr int toastPadding = 12;
constexpr int toastEdgePadding = 12;
/* Enough transparent space above a toast to contain the close button without
 * clipping its antialiased edge. */
constexpr int closeOverhang = 10;
constexpr int closeRadius = 9;
constexpr int rightColumnWidth = 68;
constexpr int notificationImageSize = 56;

QString plainText(const QString &text)
{
    return QTextDocumentFragment::fromHtml(text).toPlainText();
}

QVariant unbox(QVariant v)
{
    if (v.canConvert<QDBusVariant>())
        return v.value<QDBusVariant>().variant();
    return v;
}

QIcon resolveIcon(const QString &name)
{
    if (name.isEmpty())
        return QIcon();
    QString path = name;
    if (path.startsWith(QLatin1String("file://")))
        path = path.mid(7);
    if (QFileInfo(path).isAbsolute() && QFileInfo::exists(path))
        return QIcon(path);
    return QIcon::fromTheme(name);
}

QImage imageFromHints(const QVariantMap &hints)
{
    QVariant path = unbox(hints.value(QStringLiteral("image-path")));
    if (!path.isValid())
        path = unbox(hints.value(QStringLiteral("image_path")));
    if (path.isValid()) {
        QString imagePath = path.toString();
        if (imagePath.startsWith(QLatin1String("file://")))
            imagePath = imagePath.mid(7);
        const QImage image(imagePath);
        if (!image.isNull())
            return image;
    }

    QVariant value = hints.value(QStringLiteral("image-data"));
    if (!value.isValid())
        value = hints.value(QStringLiteral("image_data"));
    if (!value.isValid())
        value = hints.value(QStringLiteral("icon_data"));
    value = unbox(value);
    if (!value.canConvert<QDBusArgument>())
        return QImage();

    int width = 0, height = 0, rowstride = 0;
    bool alpha = false;
    int bits = 0, channels = 0;
    QByteArray bytes;
    const QDBusArgument arg = value.value<QDBusArgument>();
    arg.beginStructure();
    arg >> width >> height >> rowstride >> alpha >> bits >> channels >> bytes;
    arg.endStructure();
    if (width <= 0 || height <= 0 || rowstride <= 0 || bits != 8
            || (channels != 3 && channels != 4)
            || bytes.size() < rowstride * height)
        return QImage();
    const QImage::Format format = alpha ? QImage::Format_RGBA8888
                                        : QImage::Format_RGB888;
    return QImage(reinterpret_cast<const uchar *>(bytes.constData()), width,
                  height, rowstride, format).copy();
}

QString timestampText(const Notification &n)
{
    return QLocale().toString(n.receivedAt.time(), QLocale::ShortFormat);
}

/* Shared row geometry for the toast stack and history view. */
int rowHeight(const Notification &n, int width, bool history = false)
{
    const int textWidth = width - toastPadding * 2 - rightColumnWidth - 8
                          - (n.icon.isNull() ? 0 : 42);
    QFontMetrics titleFm(Theme::font(Theme::bodyFontSize, true));
    QFontMetrics bodyFm(Theme::font(Theme::smallFontSize));
    const QRect title = titleFm.boundingRect(QRect(0, 0, textWidth, 1000),
                                             Qt::TextWordWrap, n.summary);
    const QRect body = bodyFm.boundingRect(QRect(0, 0, textWidth, 1000),
                                           Qt::TextWordWrap, n.body);
    const int textHeight = toastPadding * 2 + title.height()
                           + (n.body.isEmpty() ? 0 : 4 + body.height());
    const int sideHeight = toastPadding * 2 + Theme::tinyFontSize + 4
                           + (n.image.isNull() ? 0 : notificationImageSize);
    const int content = qMax(64, qMax(textHeight, sideHeight));
    return content + (history ? 0 : closeOverhang);
}

void paintNotification(QPainter &p, const Notification &n, const QRect &outer,
                       bool hovered, bool history, bool closeHovered = false)
{
    /* Toasts reserve space above the card for a macOS-style close circle
     * straddling its top edge. History rows stay inside their popup and do
     * not expose that dismiss affordance. */
    const QRect card = history ? outer : outer.adjusted(0, closeOverhang, 0, 0);
    const QColor border = n.color.isValid() ? n.color : Theme::accent;
    QColor fill = Theme::bg;
    if (!history)
        fill.setAlpha(Theme::notificationOpacity);
    Theme::paintRect(p, card, fill, Theme::radius, border, 2);

    int x = card.left() + toastPadding;
    if (!n.icon.isNull()) {
        n.icon.paint(&p, QRect(x, card.top() + toastPadding, 32, 32));
        x += 42;
    }
    const int columnRight = card.right() - toastPadding;
    const int columnLeft = columnRight - rightColumnWidth;
    const int textRight = columnLeft - 8;
    const int textWidth = qMax(1, textRight - x);
    int y = card.top() + toastPadding;
    p.setFont(Theme::font(Theme::bodyFontSize, true));
    p.setPen(n.foreground);
    QRect titleRect = p.fontMetrics().boundingRect(
        QRect(x, y, textWidth, 1000), Qt::TextWordWrap, n.summary);
    p.drawText(titleRect, Qt::TextWordWrap, n.summary);
    y = titleRect.bottom() + 5;
    if (!n.body.isEmpty()) {
        p.setFont(Theme::font(Theme::smallFontSize));
        p.setPen(n.foreground);
        p.drawText(QRect(x, y, textWidth, card.bottom() - y - toastPadding + 1),
                   Qt::TextWordWrap, n.body);
    }

    p.setFont(Theme::font(Theme::tinyFontSize));
    p.setPen(Theme::textMuted);
    const int timeHeight = p.fontMetrics().height();
    p.drawText(QRect(columnLeft, card.top() + toastPadding,
                     rightColumnWidth, timeHeight),
               Qt::AlignRight | Qt::AlignVCenter, timestampText(n));
    if (!n.image.isNull()) {
        const QImage scaled = n.image.scaled(
            QSize(notificationImageSize, notificationImageSize),
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRect imageRect(QPoint(), scaled.size());
        imageRect.moveTopRight(QPoint(columnRight,
                                      card.top() + toastPadding
                                          + timeHeight + 5));
        p.drawImage(imageRect, scaled);
    }

    if (hovered && !history) {
        const QPoint c(card.left() + closeRadius + 7, card.top());
        p.setPen(Qt::NoPen);
        p.setBrush(closeHovered ? Theme::tagEmpty : Theme::brightblack);
        p.drawEllipse(c, closeRadius, closeRadius);

        /* Draw rather than typeset the X: round-capped antialiased strokes
         * stay crisp and centered regardless of font glyph metrics. */
        QPen mark(closeHovered ? Theme::textStrong : Theme::bg, 2.0,
                  Qt::SolidLine, Qt::RoundCap);
        p.setPen(mark);
        const int arm = 3;
        p.drawLine(c + QPoint(-arm, -arm), c + QPoint(arm, arm));
        p.drawLine(c + QPoint(arm, -arm), c + QPoint(-arm, arm));
    }
}

class NotificationToastWindow : public QWidget {
public:
    NotificationToastWindow()
        : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint),
          m_service(NotificationService::instance())
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setMouseTracking(true);
        connect(m_service, &NotificationService::changed, this,
                [this]() { sync(); });
        connect(HyprState::instance(), &HyprState::changed, this,
                [this]() { sync(); });
        connect(qApp, &QGuiApplication::screenAdded, this,
                [this]() { sync(); });
        connect(qApp, &QGuiApplication::screenRemoved, this,
                [this]() { sync(); });
    }

    void sync()
    {
        m_ids = m_service->visibleIds();
        QScreen *target = nullptr;
        const QString host = HyprState::instance()->primaryMonitor();
        for (QScreen *s : QGuiApplication::screens())
            if (s->name() == host)
                target = s;
        if (!target && !QGuiApplication::screens().isEmpty())
            target = QGuiApplication::screens().first();

        if (m_configured && target != m_screen) {
            hide();
            destroy();
            m_configured = false;
        }
        m_screen = target;
        if (m_ids.isEmpty() || !m_screen) {
            hide();
            return;
        }

        m_totalHeight = 0;
        for (uint id : m_ids)
            if (const Notification *n = m_service->find(id))
                m_totalHeight += rowHeight(*n, toastWidth)
                                 + (m_totalHeight ? toastGap : 0);
        const int maxHeight = qMax(80, m_screen->geometry().height()
                                      - Theme::panelHeight
                                      - toastEdgePadding * 2);
        const int h = qMin(m_totalHeight, maxHeight);
        m_scroll = qBound(0, m_scroll, qMax(0, m_totalHeight - h));
        setFixedSize(toastWidth, h);
        if (!m_configured) {
            WlUtil::SurfaceSpec spec;
            spec.layer = WlUtil::Layer::Overlay;
            spec.anchors = WlUtil::AnchorTop | WlUtil::AnchorRight;
            /* The surface begins at the panel edge; its transparent top
             * overhang leaves room for the close circle. */
            spec.margins = QMargins(0, Theme::panelHeight,
                                    toastEdgePadding, 0);
            spec.desiredSize = QSize(toastWidth, h);
            spec.exclusiveZone = -1;
            spec.keyboard = WlUtil::Keyboard::None;
            spec.activateOnShow = false;
            spec.scope = QStringLiteral("fanhypr-qs-notifications");
            WlUtil::configure(this, m_screen, spec);
            m_configured = true;
        } else {
            WlUtil::setDesiredSize(this, QSize(toastWidth, h));
        }
        show();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setClipRect(rect());
        int y = -m_scroll;
        for (uint id : m_ids) {
            const Notification *n = m_service->find(id);
            if (!n)
                continue;
            const int h = rowHeight(*n, width());
            paintNotification(p, *n, QRect(0, y, width(), h),
                              id == m_hovered, false,
                              id == m_hovered && m_closeHovered);
            y += h + toastGap;
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const uint next = idAt(e->pos());
        bool close = false;
        if (next) {
            const QRect card = rectFor(next).adjusted(0, closeOverhang, 0, 0);
            const QPoint center(card.left() + closeRadius + 7, card.top());
            const QPoint delta = e->pos() - center;
            close = delta.x() * delta.x() + delta.y() * delta.y()
                    <= closeRadius * closeRadius;
        }
        if (m_hovered != next || m_closeHovered != close) {
            m_hovered = next;
            m_closeHovered = close;
            m_service->setHovered(next);
            setCursor(close ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_hovered = 0;
        m_closeHovered = false;
        m_service->setHovered(0);
        setCursor(Qt::ArrowCursor);
        update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        const uint id = idAt(e->pos());
        const Notification *n = m_service->find(id);
        if (!n)
            return;
        const QRect r = rectFor(id);
        const QRect card = r.adjusted(0, closeOverhang, 0, 0);
        const QPoint closeCenter(card.left() + closeRadius + 7, card.top());
        const QPoint delta = e->pos() - closeCenter;
        if (delta.x() * delta.x() + delta.y() * delta.y()
                <= closeRadius * closeRadius)
            m_service->dismiss(id);
        else
            m_service->activate(id);
    }

    void wheelEvent(QWheelEvent *e) override
    {
        const int maxScroll = qMax(0, m_totalHeight - height());
        m_scroll = qBound(0, m_scroll - e->angleDelta().y() / 2, maxScroll);
        update();
    }

private:
    QRect rectFor(uint wanted) const
    {
        int y = -m_scroll;
        for (uint id : m_ids) {
            const Notification *n = m_service->find(id);
            if (!n)
                continue;
            const int h = rowHeight(*n, width());
            if (id == wanted)
                return QRect(0, y, width(), h);
            y += h + toastGap;
        }
        return QRect();
    }

    uint idAt(const QPoint &pos) const
    {
        for (uint id : m_ids)
            if (rectFor(id).contains(pos))
                return id;
        return 0;
    }

    NotificationService *m_service;
    QVector<uint> m_ids;
    QScreen *m_screen = nullptr;
    uint m_hovered = 0;
    bool m_closeHovered = false;
    int m_totalHeight = 0;
    int m_scroll = 0;
    bool m_configured = false;
};

NotificationToastWindow *toastWindow()
{
    static NotificationToastWindow *w = new NotificationToastWindow();
    return w;
}

class NotificationHistoryView : public QWidget {
public:
    explicit NotificationHistoryView(QWidget *parent = nullptr)
        : QWidget(parent), m_service(NotificationService::instance())
    {
        setMouseTracking(true);
        connect(m_service, &NotificationService::changed, this,
                [this]() { update(); });
        setMinimumHeight(80);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setClipRect(rect());
        const auto &items = m_service->history();
        if (items.isEmpty()) {
            p.setFont(Theme::font(Theme::bodyFontSize));
            p.setPen(Theme::textMuted);
            p.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("No notifications yet."));
            return;
        }
        int y = -m_scroll;
        for (const Notification &n : items) {
            const int h = rowHeight(n, width(), true);
            paintNotification(p, n, QRect(0, y, width(), h),
                              n.id == m_hovered, true);
            y += h + toastGap;
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        m_hovered = idAt(e->pos());
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hovered = 0;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            const uint id = idAt(e->pos());
            if (id)
                m_service->activate(id);
        }
    }

    void wheelEvent(QWheelEvent *e) override
    {
        int total = 0;
        for (const Notification &n : m_service->history())
            total += rowHeight(n, width(), true) + toastGap;
        m_scroll = qBound(0, m_scroll - e->angleDelta().y() / 2,
                          qMax(0, total - height()));
        update();
    }

private:
    uint idAt(const QPoint &pos) const
    {
        int y = -m_scroll;
        for (const Notification &n : m_service->history()) {
            const int h = rowHeight(n, width(), true);
            if (QRect(0, y, width(), h).contains(pos))
                return n.id;
            y += h + toastGap;
        }
        return 0;
    }

    NotificationService *m_service;
    uint m_hovered = 0;
    int m_scroll = 0;
};

} /* namespace */

class NotificationHistoryPopup : public ShellPopup {
public:
    explicit NotificationHistoryPopup(QWidget *anchor)
        : ShellPopup(anchor, 380)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(Theme::popupMargin, Theme::popupMargin,
                                   Theme::popupMargin, Theme::popupMargin);
        layout->setSpacing(Theme::popupSpacing);
        auto *title = new TextItem(this);
        title->setText(QStringLiteral("Notifications"));
        title->setBold(true);
        title->setPixelSize(Theme::titleFontSize);
        layout->addWidget(title);
        layout->addWidget(new NotificationHistoryView(this), 1);
        setFixedHeight(500);
    }

protected:
    int popupHeight() const override { return 500; }
};

NotificationService *NotificationService::instance()
{
    static NotificationService *service = new NotificationService();
    return service;
}

NotificationService::NotificationService(QObject *parent) : QObject(parent)
{
    auto *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, &NotificationService::tick);
    timer->start();
    m_lastTick = QDateTime::currentMSecsSinceEpoch();
}

bool NotificationService::start()
{
    if (m_started)
        return true;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.freedesktop.Notifications")))
        return false;
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                            this, QDBusConnection::ExportScriptableSlots
                                      | QDBusConnection::ExportScriptableSignals)) {
        bus.unregisterService(QStringLiteral("org.freedesktop.Notifications"));
        return false;
    }
    m_started = true;
    toastWindow()->sync();
    return true;
}

QStringList NotificationService::GetCapabilities()
{
    return {QStringLiteral("actions"), QStringLiteral("body"),
            QStringLiteral("icon-static"), QStringLiteral("persistence")};
}

void NotificationService::GetServerInformation(QString &name, QString &vendor,
                                                QString &version,
                                                QString &specVersion)
{
    name = QStringLiteral("fanhypr-qs-shell");
    vendor = QStringLiteral("fanhypr-qs");
    version = QStringLiteral("1.0");
    specVersion = QStringLiteral("1.3");
}

uint NotificationService::Notify(const QString &appName, uint replacesId,
                                 const QString &appIcon,
                                 const QString &summary, const QString &body,
                                 const QStringList &actions,
                                 const QVariantMap &hints, int expireTimeout)
{
    int replaceAt = -1;
    if (replacesId)
        for (int i = 0; i < m_history.size(); i++)
            if (m_history[i].id == replacesId) {
                replaceAt = i;
                break;
            }

    Notification n;
    n.id = replaceAt >= 0 ? replacesId : m_nextId++;
    if (m_nextId == 0)
        m_nextId = 1;
    n.appName = appName;
    n.appIcon = appIcon;
    n.summary = plainText(summary);
    n.body = plainText(body);
    n.actions = actions;
    n.hints = hints;
    n.icon = resolveIcon(appIcon);
    n.image = imageFromHints(hints);
    n.receivedAt = QDateTime::currentDateTime();
    if (calledFromDBus()) {
        QDBusReply<uint> pid = QDBusConnection::sessionBus().interface()->servicePid(
            message().service());
        if (pid.isValid())
            n.senderPid = pid.value();
    }
    applyStyle(n, expireTimeout);
    n.visible = !m_dnd || n.urgency == 2;

    if (replaceAt >= 0)
        m_history.removeAt(replaceAt);
    m_history.prepend(n);
    while (m_history.size() > historyLimit) {
        const Notification old = m_history.takeLast();
        if (old.visible)
            emit NotificationClosed(old.id, 2);
    }
    emit changed();
    return n.id;
}

void NotificationService::applyStyle(Notification &n, int requestedTimeout)
{
    const QVariant urgency = unbox(n.hints.value(QStringLiteral("urgency")));
    n.urgency = urgency.isValid() ? urgency.toInt() : 1;
    n.color = n.urgency == 2 ? Theme::danger : Theme::accent;
    n.foreground = n.urgency == 2 ? Theme::danger : Theme::textStrong;
    int fallback = n.urgency == 2 ? 0 : 5000;

    const QString app = n.appName.toLower();
    if (app == QLatin1String("volume")) {
        n.color = Theme::success;
        n.foreground = Theme::success;
        fallback = 2000;
    } else if (app == QLatin1String("network")) {
        n.color = QColor(QStringLiteral("#7dcfff"));
        n.foreground = n.color;
        fallback = 3000;
    } else if (app == QLatin1String("brightness")) {
        n.color = Theme::warning;
        n.foreground = Theme::warning;
        fallback = 2000;
    } else if (app == QLatin1String("screenshot")) {
        n.color = Theme::brightmagenta;
        n.foreground = Theme::brightmagenta;
        fallback = 3000;
    } else if (n.summary.contains(QStringLiteral("warn"),
                                  Qt::CaseInsensitive)) {
        n.color = Theme::warning;
        n.foreground = Theme::warning;
        fallback = 8000;
    }
    /* Critical notifications are deliberately sticky regardless of what the
     * sender requested; they also bypass DND. */
    n.remainingMs = n.urgency == 2 ? 0
                                   : (requestedTimeout < 0 ? fallback
                                                          : requestedTimeout);
}

QVector<uint> NotificationService::visibleIds() const
{
    QVector<uint> ids;
    for (const Notification &n : m_history)
        if (n.visible)
            ids.push_back(n.id);
    return ids;
}

const Notification *NotificationService::find(uint id) const
{
    for (const Notification &n : m_history)
        if (n.id == id)
            return &n;
    return nullptr;
}

void NotificationService::CloseNotification(uint id)
{
    dismiss(id, 3);
}

void NotificationService::dismiss(uint id, uint reason)
{
    for (Notification &n : m_history) {
        if (n.id != id || !n.visible)
            continue;
        n.visible = false;
        if (m_hovered == id)
            m_hovered = 0;
        emit NotificationClosed(id, reason);
        emit changed();
        return;
    }
}

void NotificationService::activate(uint id)
{
    const Notification *ptr = find(id);
    if (!ptr)
        return;
    const Notification n = *ptr;
    bool invoked = false;
    for (int i = 0; i + 1 < n.actions.size(); i += 2) {
        if (n.actions[i] == QLatin1String("default")) {
            emit ActionInvoked(id, QStringLiteral("default"));
            invoked = true;
            break;
        }
    }
    if (!invoked)
        focusCertainSender(n);
    dismiss(id, 2);
}

bool NotificationService::focusCertainSender(const Notification &n)
{
    if (!n.senderPid)
        return false;
    QString address;
    int matches = 0;
    const QJsonArray clients = HyprIpc::requestArray("clients");
    for (const QJsonValue &v : clients) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("pid")).toInt() != int(n.senderPid))
            continue;
        address = o.value(QStringLiteral("address")).toString();
        matches++;
    }
    if (matches != 1 || address.isEmpty())
        return false;
    if (!address.startsWith(QLatin1String("0x")))
        address.prepend(QStringLiteral("0x"));
    return HyprIpc::dispatch(
        QStringLiteral("hl.dsp.focus({ window = \"address:%1\" })").arg(address),
        QStringLiteral("focuswindow address:%1").arg(address));
}

void NotificationService::setHovered(uint id)
{
    m_hovered = id;
}

void NotificationService::tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int elapsed = int(qBound<qint64>(qint64(0), now - m_lastTick,
                                           qint64(1000)));
    m_lastTick = now;
    QVector<uint> expired;
    for (Notification &n : m_history) {
        if (!n.visible || n.urgency == 2 || n.id == m_hovered
                || n.remainingMs <= 0)
            continue;
        n.remainingMs -= elapsed;
        if (n.remainingMs <= 0)
            expired.push_back(n.id);
    }
    for (uint id : expired)
        dismiss(id, 1);
}

void NotificationService::toggleDnd()
{
    setDnd(!m_dnd);
}

void NotificationService::setDnd(bool enabled)
{
    if (m_dnd == enabled)
        return;
    m_dnd = enabled;
    if (m_dnd) {
        for (Notification &n : m_history)
            if (n.visible && n.urgency != 2)
                n.visible = false;
    }
    emit changed();
}

NotificationWidget::NotificationWidget(QWidget *parent) : BarPill(parent)
{
    m_popup = new NotificationHistoryPopup(this);
    connect(this, &BarPill::clicked, m_popup,
            &ShellPopup::togglePopup);
    connect(this, &BarPill::rightClicked,
            NotificationService::instance(), &NotificationService::toggleDnd);
    connect(NotificationService::instance(), &NotificationService::changed,
            this, &NotificationWidget::sync);
    connect(m_popup, &ShellPopup::popupVisibleChanged, this,
            [this](bool) { sync(); });
    sync();
}

void NotificationWidget::sync()
{
    NotificationService *s = NotificationService::instance();
    setIcon(s->dnd() ? QStringLiteral("") : QStringLiteral(""));
    setLabel(s->dnd() && !s->history().isEmpty()
                 ? QString::number(s->history().size()) : QString());
    setTint(s->dnd() ? Theme::danger : Theme::text);
    setActive(s->dnd() || m_popup->isVisible());
}
