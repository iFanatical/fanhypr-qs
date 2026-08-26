#include "osd.h"

#include <QApplication>
#include <QDBusVariant>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QtMath>

#include "hyprstate.h"
#include "theme.h"
#include "volume.h"
#include "wlutil.h"

namespace {

constexpr int osdWidth = 300;
constexpr int osdHeight = 78;
constexpr int osdShadowRange = 4;
constexpr int osdBottomMargin = 64;
constexpr int osdTimeoutMs = 2000;

QVariant unboxOsdValue(QVariant value)
{
    while (value.canConvert<QDBusVariant>()) {
        const QVariant inner = qvariant_cast<QDBusVariant>(value).variant();
        if (!inner.isValid() || inner == value)
            break;
        value = inner;
    }
    return value;
}

int percentage(const QVariantMap &hints, const QString &body)
{
    const QVariant hinted = unboxOsdValue(hints.value(QStringLiteral("value")));
    if (hinted.isValid()) {
        bool ok = false;
        const int value = hinted.toInt(&ok);
        if (ok)
            return qBound(0, value, 100);
    }
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("(\\d+)\\s*%")).match(body);
    return match.hasMatch() ? qBound(0, match.captured(1).toInt(), 100) : -1;
}

QScreen *focusedScreen()
{
    const QString name = HyprState::instance()->focusedMonitor();
    for (QScreen *screen : QGuiApplication::screens())
        if (screen->name() == name)
            return screen;
    return QGuiApplication::primaryScreen();
}

} // namespace

HardwareOsd *HardwareOsd::instance()
{
    static HardwareOsd *osd = new HardwareOsd;
    return osd;
}

HardwareOsd::HardwareOsd(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint),
      m_timer(new QTimer(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        hide();
        const uint old = m_id;
        m_id = 0;
        if (old)
            emit closed(old, 1);
    });
}

bool HardwareOsd::showNotification(uint id, const QString &appName,
                                   const QString &summary,
                                   const QString &body,
                                   const QVariantMap &hints, int urgency)
{
    if (urgency == 2)
        return false;

    const QString app = appName.trimmed().toLower();
    const QString title = summary.trimmed();
    const QString titleLower = title.toLower();
    if (app == QLatin1String("volume")) {
        m_kind = titleLower.contains(QLatin1String("microphone"))
                     ? Kind::Microphone : Kind::Volume;
    } else if (app == QLatin1String("brightness")) {
        m_kind = titleLower.contains(QLatin1String("keyboard"))
                     ? Kind::Keyboard : Kind::Brightness;
    } else if (app == QLatin1String("keyboard-brightness")
               || app == QLatin1String("keyboard_backlight")) {
        m_kind = Kind::Keyboard;
    } else {
        return false;
    }

    const QString state = body.trimmed();
    m_muted = state.compare(QLatin1String("muted"),
                            Qt::CaseInsensitive) == 0;
    m_value = percentage(hints, body);
    PulseBackend *pulse = PulseBackend::instance();
    if (m_value < 0 && m_kind == Kind::Volume && pulse->hasSink)
        m_value = qRound(pulse->sinkVolume * 100.0);
    if (m_value < 0 && m_kind == Kind::Microphone && pulse->hasSource)
        m_value = qRound(pulse->sourceVolume * 100.0);
    if (m_value < 0 && !m_muted)
        return false;
    m_value = qBound(0, m_value < 0 ? 0 : m_value, 100);

    switch (m_kind) {
    case Kind::Volume:
        m_label = QStringLiteral("Volume");
        m_accent = m_muted ? Theme::danger : Theme::success;
        break;
    case Kind::Microphone:
        m_label = QStringLiteral("Microphone");
        m_accent = m_muted ? Theme::danger : Theme::success;
        break;
    case Kind::Brightness:
        m_label = titleLower.contains(QLatin1String("monitor"))
                      ? QStringLiteral("Monitor brightness")
                      : QStringLiteral("Brightness");
        m_accent = Theme::warning;
        break;
    case Kind::Keyboard:
        m_label = QStringLiteral("Keyboard backlight");
        m_accent = Theme::brightmagenta;
        break;
    }
    m_state = m_muted ? QStringLiteral("Muted")
                      : QString::number(m_value) + QLatin1Char('%');

    if (m_id && m_id != id)
        emit closed(m_id, 2);
    m_id = id;
    configureFor(focusedScreen());
    show();
    raise();
    update();
    m_timer->start(osdTimeoutMs);
    return true;
}

void HardwareOsd::configureFor(QScreen *screen)
{
    if (!screen)
        return;
    if (m_configured && screen != m_screen) {
        hide();
        destroy();
        m_configured = false;
    }
    m_screen = screen;
    setFixedSize(osdWidth, osdHeight);
    if (m_configured)
        return;
    WlUtil::SurfaceSpec spec;
    spec.layer = WlUtil::Layer::Overlay;
    spec.anchors = WlUtil::AnchorBottom;
    spec.margins = QMargins(0, 0, 0, osdBottomMargin);
    spec.desiredSize = QSize(osdWidth, osdHeight);
    spec.exclusiveZone = -1;
    spec.keyboard = WlUtil::Keyboard::None;
    spec.activateOnShow = false;
    spec.scope = QStringLiteral("fanhypr-qs-osd");
    WlUtil::configure(this, screen, spec);
    m_configured = true;
}

void HardwareOsd::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF card = QRectF(rect()).adjusted(osdShadowRange,
                                                osdShadowRange,
                                                -osdShadowRange,
                                                -osdShadowRange);
    p.setPen(Qt::NoPen);
    for (int spread = osdShadowRange; spread >= 1; --spread) {
        const qreal strength =
            qreal(osdShadowRange - spread + 1) / osdShadowRange;
        QColor shadow = Theme::bg;
        shadow.setAlpha(qRound(Theme::notificationShadowAlpha
                               * strength * strength * strength));
        p.setBrush(shadow);
        p.drawRoundedRect(card.adjusted(-spread, -spread, spread, spread),
                          Theme::radius + spread, Theme::radius + spread);
    }
    QColor fill = Theme::bg;
    fill.setAlpha(Theme::notificationOpacity);
    Theme::paintRect(p, card, fill, Theme::radius, m_accent, 2);

    drawIcon(p, QRectF(card.left() + 16, card.top() + 18, 34, 34));
    const qreal left = card.left() + 64;
    const qreal right = card.right() - 16;
    p.setFont(Theme::font(Theme::smallFontSize, true));
    p.setPen(Theme::textStrong);
    p.drawText(QRectF(left, card.top() + 11, right - left, 20),
               Qt::AlignLeft | Qt::AlignVCenter, m_label);
    p.setFont(Theme::font(Theme::smallFontSize));
    p.setPen(m_accent);
    p.drawText(QRectF(left, card.top() + 11, right - left, 20),
               Qt::AlignRight | Qt::AlignVCenter, m_state);

    const QRectF track(left, card.top() + 43, right - left, 8);
    Theme::paintRect(p, track, Theme::surface, 4);
    if (!m_muted && m_value > 0) {
        QRectF level = track;
        level.setWidth(track.width() * m_value / 100.0);
        Theme::paintRect(p, level, m_accent, 4);
    }
}

void HardwareOsd::drawIcon(QPainter &p, const QRectF &r) const
{
    p.save();
    QPen pen(m_accent, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QPointF c = r.center();
    if (m_kind == Kind::Brightness) {
        p.drawEllipse(c, 6, 6);
        for (int i = 0; i < 8; ++i) {
            const qreal a = i * M_PI / 4.0;
            p.drawLine(c + QPointF(qCos(a) * 10, qSin(a) * 10),
                       c + QPointF(qCos(a) * 15, qSin(a) * 15));
        }
    } else if (m_kind == Kind::Keyboard) {
        p.drawRoundedRect(r.adjusted(2, 7, -2, -7), 3, 3);
        p.drawLine(QPointF(r.left() + 8, r.bottom() - 11),
                   QPointF(r.right() - 8, r.bottom() - 11));
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(r.left() + 9 + i * 8, r.top() + 13),
                       QPointF(r.left() + 12 + i * 8, r.top() + 13));
    } else if (m_kind == Kind::Microphone) {
        p.drawRoundedRect(QRectF(c.x() - 5, r.top() + 3, 10, 18), 5, 5);
        p.drawArc(QRectF(c.x() - 10, r.top() + 8, 20, 18), 180 * 16,
                  180 * 16);
        p.drawLine(QPointF(c.x(), r.bottom() - 8),
                   QPointF(c.x(), r.bottom() - 3));
        p.drawLine(QPointF(c.x() - 6, r.bottom() - 3),
                   QPointF(c.x() + 6, r.bottom() - 3));
    } else {
        QPainterPath speaker;
        speaker.moveTo(r.left() + 3, c.y() - 5);
        speaker.lineTo(r.left() + 10, c.y() - 5);
        speaker.lineTo(r.left() + 17, c.y() - 12);
        speaker.lineTo(r.left() + 17, c.y() + 12);
        speaker.lineTo(r.left() + 10, c.y() + 5);
        speaker.lineTo(r.left() + 3, c.y() + 5);
        speaker.closeSubpath();
        p.drawPath(speaker);
        if (!m_muted) {
            p.drawArc(QRectF(r.left() + 12, c.y() - 9, 16, 18), -60 * 16,
                      120 * 16);
            if (m_value > 50)
                p.drawArc(QRectF(r.left() + 9, c.y() - 14, 26, 28),
                          -55 * 16, 110 * 16);
        }
    }
    if (m_muted) {
        p.setPen(QPen(Theme::danger, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(r.topLeft() + QPointF(4, 4),
                   r.bottomRight() - QPointF(4, 4));
    }
    p.restore();
}
