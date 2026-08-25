#include "pills.h"

#include "hyprstate.h"

#include <QTimer>

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
    m_toggleProc.setCommand({"fanhypr-qs-vpn", "toggle"});
    connect(&m_statusProc, &CollectorProcess::finished, this,
            &VpnState::parse);
    connect(&m_toggleProc, &CollectorProcess::finished, this,
            &VpnState::parse);
    refresh(); /* once at startup; no interval polling */
}

void VpnState::refresh()
{
    m_statusProc.start();
}

void VpnState::toggle()
{
    m_toggleProc.start();
}

void VpnState::parse(const QString &text)
{
    const QStringList lines = text.trimmed().split('\n');
    for (const QString &l : lines) {
        const int i = l.indexOf('=');
        if (i < 0)
            continue;
        const QString k = l.left(i), v = l.mid(i + 1);
        if (k == QLatin1String("state"))
            up = (v == QLatin1String("up"));
        else if (k == QLatin1String("ip"))
            ip = v;
    }
    emit changed();
}

/* --------------------------------------------------------------- VpnWidget */

VpnWidget::VpnWidget(QWidget *parent) : BarPill(parent)
{
    connect(VpnState::instance(), &VpnState::changed, this,
            &VpnWidget::sync);
    connect(this, &BarPill::clicked, VpnState::instance(),
            &VpnState::toggle);
    connect(this, &BarPill::rightClicked, VpnState::instance(),
            &VpnState::refresh);
    sync();
}

void VpnWidget::sync()
{
    const bool up = VpnState::instance()->up;
    setIcon(up ? glyph(U'\U000F099D') : glyph(U'\U000F099E'));
    setTint(up ? Theme::accent : Theme::text);
    setActive(up);
}
