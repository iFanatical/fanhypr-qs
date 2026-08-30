#include "panel.h"

#include <QEnterEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>

#include "battery.h"
#include "calendar.h"
#include "network.h"
#include "notification.h"
#include "pills.h"
#include "system.h"
#include "tray.h"
#include "wlutil.h"

/* --------------------------------------------------------- WorkspaceButton */

WorkspaceButton::WorkspaceButton(const QString &label, QWidget *parent)
    : QWidget(parent), m_label(label)
{
    setFixedSize(Theme::workspaceButtonSize, Theme::workspaceButtonSize);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
}

void WorkspaceButton::setLabel(const QString &l)
{
    if (m_label == l)
        return;
    m_label = l;
    update();
}

void WorkspaceButton::setStates(bool selected, bool occupied)
{
    if (m_selected == selected && m_occupied == occupied)
        return;
    m_selected = selected;
    m_occupied = occupied;
    update();
}

void WorkspaceButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor fill = m_selected
                            ? Theme::surface
                            : (m_hover ? Theme::surfaceHover
                                       : Theme::transparent);
    Theme::paintRect(p, rect(), fill, Theme::smallRadius);
    /* active = purple/pink, has windows = white, empty = gray */
    p.setFont(Theme::font(Theme::panelFontSize, m_selected || m_occupied));
    p.setPen(m_selected ? Theme::tagActive
                        : (m_occupied ? Theme::tagOccupied : Theme::tagEmpty));
    p.drawText(rect(), Qt::AlignCenter, m_label);
}

void WorkspaceButton::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void WorkspaceButton::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

void WorkspaceButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        m_pressed = true;
}

void WorkspaceButton::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_pressed && e->button() == Qt::LeftButton && rect().contains(e->pos()))
        emit clicked();
    m_pressed = false;
}

/* ------------------------------------------------------------------- Panel */

Panel::Panel(HyprState *state, QScreen *screen)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint),
      m_state(state), m_screen(screen)
{
    setAttribute(Qt::WA_ShowWithoutActivating);

    /* Left group (workspaces / layout symbol / title) and the right-side pill
     * cluster each get their own container so the clock can be positioned at
     * a fixed bar-center in relayoutBar(), independent of how wide either
     * side happens to be — see panel.h. */
    m_leftBox = new QWidget(this);
    auto *leftRow = new QHBoxLayout(m_leftBox);
    leftRow->setContentsMargins(0, 0, 0, 0);
    leftRow->setSpacing(Theme::rowSpacing);

    /* Workspaces: the fixed block of ids this output owns. */
    m_wsBox = new QWidget(m_leftBox);
    m_wsLayout = new QHBoxLayout(m_wsBox);
    m_wsLayout->setContentsMargins(0, 0, 0, 0);
    m_wsLayout->setSpacing(Theme::rowSpacing);
    leftRow->addWidget(m_wsBox, 0, Qt::AlignVCenter);

    /* Focused window title, elided, fills the middle gap */
    m_title = new TextItem(m_leftBox);
    m_title->setColor(Theme::selected);
    m_title->setElide(true);
    leftRow->addWidget(m_title, 1, Qt::AlignVCenter);

    /* Centered clock: not part of any layout, positioned by relayoutBar(). */
    m_clock = new ClockWidget(this);

    /* Right-side pill cluster; auto-sizes via its own layout so
     * relayoutBar() can query sizeHint() and reposition it. */
    m_rightBox = new QWidget(this);
    auto *rightRow = new QHBoxLayout(m_rightBox);
    rightRow->setContentsMargins(0, 0, 0, 0);
    rightRow->setSpacing(Theme::rowSpacing);
    rightRow->addWidget(new NotificationWidget(m_rightBox), 0, Qt::AlignVCenter);
    rightRow->addWidget(new SubmapWidget(m_rightBox), 0, Qt::AlignVCenter);
    rightRow->addWidget(new NetworkWidget(m_rightBox), 0, Qt::AlignVCenter);
    /* Battery groups with the other status pills rather than sitting past the
     * tray, so the run of shell-owned indicators stays unbroken. Hidden
     * outright on a machine with no battery -- see battery.cpp. */
    rightRow->addWidget(new BatteryWidget(m_rightBox), 0, Qt::AlignVCenter);
    /* Same slot, opposite machine: exactly one of these is ever visible. */
    rightRow->addWidget(new PowerDrawWidget(m_rightBox), 0, Qt::AlignVCenter);
    /* VPN, Bluetooth, Clipboard live in the System dropdown (as larger tiles
     * alongside Weather) -- see system.cpp. */
    auto *tray = new TrayArea(m_rightBox);
    rightRow->addWidget(tray, 0, Qt::AlignVCenter);
    /* System furthest right, past the tray. */
    rightRow->addWidget(new SystemWidget(m_rightBox), 0, Qt::AlignVCenter);

    /* Either box's natural width can change after construction (tray icons
     * add/remove, a pill's label grows). Because these boxes are positioned
     * manually, a child first produces LayoutRequest rather than resizing its
     * parent; catch both that request and the resulting resize here. */
    m_rightBox->installEventFilter(this);
    m_clock->installEventFilter(this);

    /* Tray shows on this screen only if it is the tray host; re-evaluated on
     * monitor hot-plug. */
    auto updateTrayHost = [this, tray]() { tray->setHostsTray(hostsTray()); };
    updateTrayHost();
    connect(qApp, &QGuiApplication::screenAdded, tray, updateTrayHost);
    connect(qApp, &QGuiApplication::screenRemoved, tray, updateTrayHost);
    /* Which output counts as primary is derived from compositor state, so it
     * can settle after construction (first refresh) or change on a config
     * reload -- not just on Qt-side hotplug. */
    connect(m_state, &HyprState::changed, tray, updateTrayHost);

    connect(m_state, &HyprState::changed, this, &Panel::syncFromState);
    connect(m_state, &HyprState::titleChanged, this,
            [this](const QString &monitorName) {
                if (m_screen && m_screen->name() == monitorName)
                    syncTitle();
            });

    /* The compositor owns our width (we are anchored to both side edges), but
     * Qt still needs a size to lay widgets out against, and a mode change has
     * to be followed. */
    setFixedHeight(Theme::panelHeight);
    resize(screen ? screen->geometry().width() : 1920, Theme::panelHeight);
    if (screen) {
        connect(screen, &QScreen::geometryChanged, this,
                [this](const QRect &g) {
                    resize(g.width(), Theme::panelHeight);
                    relayoutBar();
                });
    }

    syncFromState();
    relayoutBar();

    /* Anchoring across the top edge with a matching exclusive zone is what
     * reserves the strip; everything else tiles below it. */
    WlUtil::SurfaceSpec spec;
    spec.layer = WlUtil::Layer::Top;
    spec.anchors = WlUtil::AnchorTop | WlUtil::AnchorLeft | WlUtil::AnchorRight;
    /* Width 0 = "you decide": anchored to both side edges, the compositor
     * knows the output better than we do (another exclusive zone, a
     * fractional scale). Qt's own size just follows its configure. */
    spec.desiredSize = QSize(0, Theme::panelHeight);
    spec.exclusiveZone = Theme::panelHeight;
    spec.keyboard = WlUtil::Keyboard::None;
    WlUtil::configure(this, screen, spec);
}

/* The tray lives on whichever output the compositor's own configuration marks
 * out as primary -- see HyprState::primaryMonitor(). Before the first state
 * arrives there is nothing to compare against, so fall back to the first
 * screen Qt knows about rather than showing no tray at all. */
bool Panel::hostsTray() const
{
    if (!m_screen)
        return false;
    const QString primary = m_state->primaryMonitor();
    if (!primary.isEmpty())
        return m_screen->name() == primary;
    const QList<QScreen *> screens = QGuiApplication::screens();
    return !screens.isEmpty() && m_screen->name() == screens.first()->name();
}

/* QScreen::name() is the connector name ("DP-1"), which is exactly the key
 * Hyprland reports monitors under — so unlike the X11 build there is no need
 * to match outputs up by geometry. */
const HyprState::Monitor *Panel::mon() const
{
    if (!m_screen)
        return nullptr;
    if (const HyprState::Monitor *m = m_state->monitorByName(m_screen->name()))
        return m;
    return m_state->monitors.isEmpty() ? nullptr : &m_state->monitors[0];
}

QString Panel::shortenTitle(const QString &t)
{
    const int max = maxTitleLength;
    if (t.length() <= max)
        return t;
    const QString ell = QStringLiteral("…");
    const QString sep = QStringLiteral(" - ");
    const int idx = t.lastIndexOf(sep);
    auto rstrip = [](QString s) {
        while (!s.isEmpty() && s.back().isSpace())
            s.chop(1);
        return s;
    };
    if (idx > 0) {
        const QString suffix = t.mid(idx); /* " - Mozilla Firefox" */
        const int budget = max - suffix.length() - ell.length();
        if (budget >= 1)
            return rstrip(t.left(budget)) + ell + suffix;
    }
    return rstrip(t.left(max - ell.length())) + ell;
}

void Panel::syncFromState()
{
    const HyprState::Monitor *m = mon();
    const QVector<int> ids = m ? m->workspaces : QVector<int>();

    /* Rebuild only when the output's assigned block changes (hotplug, a
     * config reload moving workspaces between monitors); otherwise the row is
     * fixed and we just restyle it. */
    if (m_wsIds != ids) {
        m_wsIds = ids;
        qDeleteAll(m_wsButtons);
        m_wsButtons.clear();
        for (int id : ids) {
            auto *b = new WorkspaceButton(m_state->workspaceLabel(id), m_wsBox);
            m_wsLayout->addWidget(b, 0, Qt::AlignVCenter);
            connect(b, &WorkspaceButton::clicked, this,
                    [this, id]() { m_state->switchWorkspace(id); });
            m_wsButtons.push_back(b);
            b->show();
        }
    }
    for (int i = 0; i < m_wsButtons.size(); i++) {
        const int id = m_wsIds[i];
        m_wsButtons[i]->setLabel(m_state->workspaceLabel(id));
        m_wsButtons[i]->setStates(m && m->activeWorkspace == id,
                                  m_state->workspaceOccupied(id));
    }
    m_wsBox->setVisible(!m_wsButtons.isEmpty());

    syncTitle();
    relayoutBar();
}

void Panel::syncTitle()
{
    const HyprState::Monitor *m = mon();
    const QString title = m ? m->title : QString();
    m_title->setText(title.isEmpty()
                         ? QString()
                         : QStringLiteral(" [ ") + shortenTitle(title)
                               + QStringLiteral(" ]"),
                     false /* elided title already owns fixed layout space */);
}

void Panel::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    relayoutBar();
}

bool Panel::eventFilter(QObject *watched, QEvent *event)
{
    const bool boxResized = event->type() == QEvent::Resize
                            && (watched == m_rightBox || watched == m_clock);
    const bool rightHintChanged = watched == m_rightBox
                                  && event->type() == QEvent::LayoutRequest;
    if (boxResized || rightHintChanged)
        relayoutBar();
    return QWidget::eventFilter(watched, event);
}

void Panel::relayoutBar()
{
    const int pad = Theme::panelPadding;
    const int gap = Theme::rowSpacing;

    const QSize rightSize = m_rightBox->sizeHint();
    const int rightX = width() - pad - rightSize.width();
    m_rightBox->setGeometry(rightX, 0, rightSize.width(), Theme::panelHeight);

    const QSize clockSize = m_clock->sizeHint();
    int clockX = (width() - clockSize.width()) / 2;
    clockX = qMin(clockX, rightX - gap - clockSize.width());
    clockX = qMax(clockX, pad);
    const int clockY = (Theme::panelHeight - clockSize.height()) / 2;
    m_clock->setGeometry(clockX, clockY, clockSize.width(),
                         clockSize.height());

    const int leftW = qMax(0, clockX - gap - pad);
    m_leftBox->setGeometry(pad, 0, leftW, Theme::panelHeight);
}

void Panel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::bg);
}
