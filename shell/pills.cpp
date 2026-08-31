#include "pills.h"

#include "hyprstate.h"
#include "notification.h"
#include "osd.h"

#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

static QString glyph(char32_t c)
{
    return QString::fromUcs4(&c, 1);
}

/* -------------------------------------------------------------- ScriptPill */

ScriptPill::ScriptPill(const QStringList &command, int intervalMs,
                       QWidget *parent)
    : BarPill(parent)
{
    m_proc.setCommand(command);
    connect(&m_proc, &CollectorProcess::finished, this, &ScriptPill::parse);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, &m_proc, [this]() { m_proc.start(); });
    timer->start(intervalMs);
    m_proc.start(); /* triggeredOnStart */

    hide(); /* hidden until the script reports something */
}

void ScriptPill::setFixedTint(const QColor &c)
{
    m_fixedTint = c;
    m_hasFixedTint = true;
    setTint(c);
}

void ScriptPill::parse(const QString &out)
{
    QString ic, tx, cl;
    const QStringList lines = out.trimmed().split('\n');
    for (const QString &line : lines) {
        const int s = line.indexOf('=');
        if (s < 0)
            continue;
        const QString k = line.left(s), v = line.mid(s + 1);
        if (k == QLatin1String("icon"))
            ic = v;
        else if (k == QLatin1String("text"))
            tx = v;
        else if (k == QLatin1String("color"))
            cl = v;
    }
    setIcon(ic);
    setLabel(tx);
    if (!m_hasFixedTint)
        setTint(cl.isEmpty() ? Theme::text : QColor(cl));
    setVisible(!tx.isEmpty() || !ic.isEmpty()); /* hideWhenEmpty */
}

/* ------------------------------------------------------------ SubmapWidget */

SubmapWidget::SubmapWidget(QWidget *parent) : BarPill(parent)
{
    setIcon(QStringLiteral("\U000F030C")); /* mdi keyboard */
    connect(this, &BarPill::clicked, this,
            []() { HyprState::instance()->resetSubmap(); });
    connect(HyprState::instance(), &HyprState::changed, this,
            &SubmapWidget::sync);
    sync();
}

void SubmapWidget::sync()
{
    const QString name = HyprState::instance()->submap;
    const bool active = !name.isEmpty();
    setLabel(active ? name : QStringLiteral("global"));
    /* Accent an active submap so a modal keybind mode is hard to miss, and
     * mute the default one so it reads as "nothing to see here". */
    setTint(active ? Theme::accent : Theme::textMuted);
    setActive(active);
}

/* ---------------------------------------------------------------- VpnState */

VpnState *VpnState::instance()
{
    static VpnState *s = new VpnState();
    return s;
}

VpnState::VpnState(QObject *parent) : QObject(parent)
{
    m_statusProc.setCommand({"fanhypr-qs-vpn", "status"});
    connect(&m_statusProc, &CollectorProcess::finished, this,
            [this](const QString &text) { parse(text, false); });
    connect(&m_toggleProc, &CollectorProcess::finished, this,
            [this](const QString &text) { parse(text, true); });
    refresh(); /* once at startup; no interval polling */
}

void VpnState::refresh()
{
    m_statusProc.start();
}

void VpnState::toggle()
{
    select(activeTunnel().isEmpty() ? QStringLiteral("tun1")
                                    : activeTunnel());
}

void VpnState::select(const QString &tunnel)
{
    if (tunnel != QLatin1String("tun1") && tunnel != QLatin1String("tun2"))
        return;
    m_toggleProc.start({QStringLiteral("fanhypr-qs-vpn"),
                        QStringLiteral("select"), tunnel});
}

bool VpnState::isUp(const QString &tunnel) const
{
    return tunnel == QLatin1String("tun1") ? tun1Up : tun2Up;
}

QString VpnState::address(const QString &tunnel) const
{
    return tunnel == QLatin1String("tun1") ? tun1Ip : tun2Ip;
}

QString VpnState::activeTunnel() const
{
    if (tun1Up)
        return QStringLiteral("tun1");
    if (tun2Up)
        return QStringLiteral("tun2");
    return {};
}

void VpnState::parse(const QString &text, bool fromToggle)
{
    const bool wasUp = tun1Up || tun2Up;
    bool resultKnown = false;
    bool succeeded = false;
    QString interfaceName;
    const QStringList lines = text.trimmed().split('\n');
    for (const QString &l : lines) {
        const int i = l.indexOf('=');
        if (i < 0)
            continue;
        const QString k = l.left(i), v = l.mid(i + 1);
        if (k == QLatin1String("tun1"))
            tun1Up = (v == QLatin1String("up"));
        else if (k == QLatin1String("tun2"))
            tun2Up = (v == QLatin1String("up"));
        else if (k == QLatin1String("tun1_ip"))
            tun1Ip = v;
        else if (k == QLatin1String("tun2_ip"))
            tun2Ip = v;
        else if (k == QLatin1String("iface"))
            interfaceName = v;
        else if (k == QLatin1String("result")) {
            resultKnown = true;
            succeeded = (v == QLatin1String("ok"));
        }
    }
    emit changed();
    if (!fromToggle)
        return;
    const bool nowUp = tun1Up || tun2Up;
    if ((resultKnown && succeeded) || (!resultKnown && nowUp != wasUp)) {
        HardwareOsd::instance()->showVpn(
            isUp(interfaceName), interfaceName, address(interfaceName));
        return;
    }

    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), 2);
    NotificationService::instance()->Notify(
        QStringLiteral("vpn"), 0, QString(),
        QStringLiteral("VPN toggle failed"),
        QStringLiteral("WireGuard remained %1. Check wg-quick and sudo permissions.")
            .arg(nowUp ? QStringLiteral("connected")
                       : QStringLiteral("disconnected")),
        QStringList(), hints, 0);
}

/* --------------------------------------------------------------- VpnWidget */

VpnWidget::VpnWidget(QWidget *parent) : BarPill(parent)
{
    setFixedHeight(Theme::pillHeight * 2);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_popup = new VpnPopup(this);
    connect(this, &BarPill::clicked, m_popup, &ShellPopup::togglePopup);
    connect(m_popup, &ShellPopup::popupVisibleChanged, this,
            &BarPill::setActive);
    connect(VpnState::instance(), &VpnState::changed, this,
            &VpnWidget::sync);
    connect(this, &BarPill::rightClicked, VpnState::instance(),
            &VpnState::refresh);
    sync();
}

void VpnWidget::sync()
{
    update();
}

QSize VpnWidget::sizeHint() const
{
    return QSize(230, Theme::pillHeight * 2);
}

void VpnWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    Theme::paintRect(p, rect(),
                     highlighted() ? Theme::surfaceHover : Theme::surface,
                     Theme::radius * 2, Theme::accent, isActive() ? 2 : 0);

    VpnState *vpn = VpnState::instance();
    const QString tunnel = vpn->activeTunnel();
    const bool connected = !tunnel.isEmpty();
    const int iconW = 36;
    p.setFont(Theme::font(28));
    p.setPen(connected ? Theme::accent : Theme::text);
    p.drawText(QRectF(12, 0, iconW, height()),
               Qt::AlignLeft | Qt::AlignVCenter,
               connected ? glyph(U'\U000F099D') : glyph(U'\U000F099E'));

    const int textX = 12 + iconW + 8;
    const int textW = width() - textX - 12;
    if (connected) {
        p.setFont(Theme::font(Theme::smallFontSize, true));
        p.setPen(Theme::textStrong);
        p.drawText(QRectF(textX, 6, textW, 20),
                   Qt::AlignLeft | Qt::AlignVCenter, tunnel);
        p.setFont(Theme::font(Theme::tinyFontSize));
        p.setPen(Theme::textMuted);
        p.drawText(QRectF(textX, height() - 24, textW, 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Connected"));
    } else {
        p.setFont(Theme::font(Theme::smallFontSize));
        p.setPen(Theme::textMuted);
        p.drawText(QRectF(textX, 0, textW, height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Disconnected"));
    }
}

/* --------------------------------------------------------------- VpnPopup */

VpnPopup::VpnPopup(QWidget *anchor) : ContentPopup(anchor, 230)
{
    m_manageAsTopLevel = false;
    auto *lay = bodyLayout();
    auto *title = new TextItem(body());
    title->setPixelSize(Theme::bodyFontSize);
    title->setBold(true);
    title->setColor(Theme::textStrong);
    title->setText(QStringLiteral("WireGuard tunnel"));
    lay->addWidget(title);
    lay->addWidget(new HLine(body()));

    m_tun1 = new ShellButton(QStringLiteral("tun1"), body());
    m_tun2 = new ShellButton(QStringLiteral("tun2"), body());
    m_tun1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_tun2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lay->addWidget(m_tun1);
    lay->addWidget(m_tun2);

    connect(m_tun1, &ShellButton::activated, this,
            []() { VpnState::instance()->select(QStringLiteral("tun1")); });
    connect(m_tun2, &ShellButton::activated, this,
            []() { VpnState::instance()->select(QStringLiteral("tun2")); });
    connect(VpnState::instance(), &VpnState::changed, this, &VpnPopup::sync);
    connect(this, &ShellPopup::popupVisibleChanged, this, [](bool visible) {
        if (visible)
            VpnState::instance()->refresh();
    });
    sync();
}

void VpnPopup::sync()
{
    VpnState *vpn = VpnState::instance();
    m_tun1->setLabel(vpn->tun1Up ? QStringLiteral("tun1 · Connected")
                                 : QStringLiteral("tun1"));
    m_tun2->setLabel(vpn->tun2Up ? QStringLiteral("tun2 · Connected")
                                 : QStringLiteral("tun2"));
    m_tun1->setActive(vpn->tun1Up);
    m_tun2->setActive(vpn->tun2Up);
}
