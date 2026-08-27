#include "system.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

#include "bluetooth.h"
#include "clipboard.h"
#include "pills.h"
#include "procutil.h"
#include "volume.h"

static QString glyph(char32_t c)
{
    return QString::fromUcs4(&c, 1);
}

static void power(const QStringList &args)
{
    QProcess::startDetached(QStringLiteral("fanhypr-qs-power"), args);
}

/* Declared in system.h (as `class StatBox *m_cpuBox` etc.), so it must live
 * at namespace scope, not anonymous -- unlike the other file-local widgets
 * below, which are never named outside this .cpp. A pill-styled tile (same
 * rounded surface-filled rect as BarPill) for one CPU/Mem/Temp stat; three
 * sit side by side instead of the stacked text rows this used to be. */
class StatBox : public QWidget {
public:
    static constexpr int kIconPx = 28;

    StatBox(const QString &icon, const QString &caption, QWidget *parent)
        : QWidget(parent), m_icon(icon), m_caption(caption)
    {
        setFixedHeight(76);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setValue(const QString &v)
    {
        if (m_value == v)
            return;
        m_value = v;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Theme::paintRect(p, rect(), Theme::surface, Theme::radius);

        p.setFont(Theme::font(kIconPx));
        p.setPen(Theme::textStrong);
        p.drawText(QRectF(4, 6, width() - 8, 32), Qt::AlignCenter, m_icon);

        p.setFont(Theme::font(Theme::bodyFontSize, true));
        p.drawText(QRectF(4, 40, width() - 8, 20), Qt::AlignCenter, m_value);

        p.setFont(Theme::font(Theme::tinyFontSize, true));
        p.setPen(Theme::textMuted);
        p.drawText(QRectF(4, 60, width() - 8, 16), Qt::AlignCenter,
                  m_caption);
    }

private:
    QString m_icon, m_caption, m_value;
};

namespace {

TextItem *sectionLabel(const QString &text, QWidget *parent)
{
    auto *t = new TextItem(parent);
    t->setPixelSize(Theme::smallFontSize);
    t->setBold(true);
    t->setColor(Theme::textMuted);
    t->setText(text);
    return t;
}

/* Weather: icon on the left, temperature stacked above the condition text on
 * the right. A dedicated pill-styled tile rather than a generic ScriptPill,
 * since it needs the temp/condition on separate lines (ScriptPill's
 * icon+label is a single line) and to match the VPN/Bluetooth/Clipboard
 * tiles' height (BarPill::setLarge's 2x, i.e. Theme::pillHeight * 2). */
class WeatherTile : public QWidget {
public:
    explicit WeatherTile(QWidget *parent) : QWidget(parent)
    {
        setFixedHeight(Theme::pillHeight * 2);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        hide(); /* hidden until the script reports something */

        m_proc.setCommand({"fanhypr-qs-weather"});
        connect(&m_proc, &CollectorProcess::finished, this,
                &WeatherTile::parse);
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, &m_proc,
                [this]() { m_proc.start(); });
    }

    void setPolling(bool enabled)
    {
        if (enabled) {
            m_proc.start();
            m_timer->start(5 * 60 * 1000);
        } else {
            m_timer->stop();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Theme::paintRect(p, rect(), Theme::surface, Theme::radius * 2);

        const int iconW = 36;
        p.setFont(Theme::font(28));
        p.setPen(Theme::text);
        p.drawText(QRectF(12, 0, iconW, height()),
                  Qt::AlignLeft | Qt::AlignVCenter, m_icon);

        const int textX = 12 + iconW + 8;
        const int textW = width() - textX - 12;
        p.setFont(Theme::font(Theme::bodyFontSize, true));
        p.drawText(QRectF(textX, 4, textW, 22),
                  Qt::AlignLeft | Qt::AlignVCenter, m_temp);

        p.setFont(Theme::font(Theme::smallFontSize));
        p.setPen(Theme::textMuted);
        p.drawText(QRectF(textX, height() - 24, textW, 20),
                  Qt::AlignLeft | Qt::AlignVCenter, m_cond);
    }

private:
    void parse(const QString &text)
    {
        const QString trimmed = text.trimmed();
        setVisible(!trimmed.isEmpty());
        if (trimmed.isEmpty())
            return;
        const QStringList lines = trimmed.split('\n');
        for (const QString &line : lines) {
            const int i = line.indexOf('=');
            if (i < 0)
                continue;
            const QString k = line.left(i), v = line.mid(i + 1);
            if (k == QLatin1String("icon"))
                m_icon = v;
            else if (k == QLatin1String("temp"))
                m_temp = v;
            else if (k == QLatin1String("cond"))
                m_cond = v;
        }
        update();
    }

    CollectorProcess m_proc;
    QTimer *m_timer;
    QString m_icon, m_temp, m_cond;
};

/* Output-device row, identical in spirit to network.cpp's WifiRow/LinkRow:
 * click to make this sink the default. */
class SinkRow : public QWidget {
public:
    SinkRow(const PulseBackend::Sink &s, QWidget *parent)
        : QWidget(parent), m_s(s)
    {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(24);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        Theme::paintRect(p, rect(),
                         m_hover ? Theme::surfaceHover
                                 : (m_s.isDefault ? Theme::surface
                                                  : Theme::transparent),
                         Theme::smallRadius);
        p.setFont(Theme::font(Theme::smallFontSize));
        p.setPen(m_s.isDefault ? Theme::accent : Theme::text);
        const QString label = (m_s.isDefault ? QStringLiteral(" ")
                                             : QString())
                              + m_s.description;
        const QFontMetrics fm(p.font());
        p.drawText(QRect(8, 0, width() - 16, height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fm.elidedText(label, Qt::ElideRight, width() - 16));
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            m_pressed = true;
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_pressed && e->button() == Qt::LeftButton
                && rect().contains(e->pos()))
            PulseBackend::instance()->setDefaultSink(m_s.name);
        m_pressed = false;
    }

private:
    PulseBackend::Sink m_s;
    bool m_hover = false;
    bool m_pressed = false;
};

/* A ShellButton that arms on the first click (label -> "Confirm?", accent
 * border) and only fires on a second click within 4s, else reverts. Used for
 * the two destructive power actions; no Q_OBJECT/moc needed since it adds no
 * new signals, matching the plain-callback row-widget idiom used elsewhere
 * (e.g. ClipRow in clipboard.cpp). */
class ConfirmButton : public ShellButton {
public:
    ConfirmButton(const QString &label, QWidget *parent)
        : ShellButton(label, parent), m_baseLabel(label)
    {
        setDanger(true);
        m_revert = new QTimer(this);
        m_revert->setSingleShot(true);
        connect(m_revert, &QTimer::timeout, this, [this]() { disarm(); });
        connect(this, &ShellButton::activated, this, [this]() {
            if (m_armed) {
                disarm();
                if (confirmed)
                    confirmed();
            } else {
                m_armed = true;
                setLabel(QStringLiteral("Confirm?"));
                setActive(true);
                m_revert->start(4000);
            }
        });
    }

    std::function<void()> confirmed;

private:
    void disarm()
    {
        m_armed = false;
        setLabel(m_baseLabel);
        setActive(false);
        m_revert->stop();
    }

    QString m_baseLabel;
    bool m_armed = false;
    QTimer *m_revert;
};

} /* namespace */

/* ----------------------------------------------------------- SystemWidget */

SystemWidget::SystemWidget(QWidget *parent) : BarPill(parent)
{
    setIcon(glyph(U'\U000F0493')); /* mdi cog */
    setTint(Theme::text);

    m_popup = new SystemPopup(this);
    connect(this, &BarPill::clicked, m_popup, &ShellPopup::togglePopup);
    connect(m_popup, &ShellPopup::popupVisibleChanged, this,
            &BarPill::setActive);
}

/* ------------------------------------------------------------ SystemPopup */

SystemPopup::SystemPopup(QWidget *anchor) : ContentPopup(anchor, 360)
{
    auto *lay = bodyLayout();
    PulseBackend *pa = PulseBackend::instance();

    auto *title = new TextItem(body());
    title->setPixelSize(Theme::bodyFontSize);
    title->setBold(true);
    title->setColor(Theme::textStrong);
    title->setText(QStringLiteral("System"));
    lay->addWidget(title);

    lay->addWidget(new HLine(body()));

    /* ---- CPU / memory / temperature: three tiles side by side ---- */
    auto *statsRow = new QWidget(body());
    auto *sr = new QHBoxLayout(statsRow);
    sr->setContentsMargins(0, 0, 0, 0);
    sr->setSpacing(Theme::rowSpacing);
    m_cpuBox = new StatBox(glyph(U'\U000F0EE0'), QStringLiteral("CPU"),
                           statsRow);
    m_memBox = new StatBox(glyph(U'\U000F035B'), QStringLiteral("Memory"),
                           statsRow);
    m_tempBox = new StatBox(glyph(U'\U000F050F'), QStringLiteral("Temp"),
                            statsRow);
    sr->addWidget(m_cpuBox, 1);
    sr->addWidget(m_memBox, 1);
    sr->addWidget(m_tempBox, 1);
    lay->addWidget(statsRow);

    auto *sysProc = new CollectorProcess(this);
    sysProc->setCommand({"fanhypr-qs-sysinfo"});
    connect(sysProc, &CollectorProcess::finished, this,
            &SystemPopup::parseSysInfo);
    auto *sysTimer = new QTimer(this);
    connect(sysTimer, &QTimer::timeout, sysProc,
            [sysProc]() { sysProc->start(); });
    connect(this, &ShellPopup::popupVisibleChanged, this,
            [sysTimer, sysProc](bool visible) {
                if (visible) {
                    sysProc->start();
                    sysTimer->start(3000);
                } else {
                    sysTimer->stop();
                }
                BluetoothState::instance()->setPolling(visible);
            });

    lay->addWidget(new HLine(body()));

    /* ---- weather + VPN/Bluetooth/Clipboard, moved off the bar and made
     * ~2x bigger (BarPill::setLarge) since they're now full-size popup
     * content rather than a compact bar row. ---- */
    auto *quickRow = new QWidget(body());
    auto *qr = new QHBoxLayout(quickRow);
    qr->setContentsMargins(0, 0, 0, 0);
    qr->setSpacing(Theme::rowSpacing);
    auto *weather = new WeatherTile(quickRow);
    auto *vpn = new VpnWidget(quickRow);
    auto *bt = new BluetoothWidget(quickRow);
    auto *clip = new ClipboardWidget(quickRow);
    vpn->setLarge(true);
    bt->setLarge(true);
    clip->setLarge(true);
    qr->addWidget(weather, 1, Qt::AlignVCenter);
    qr->addWidget(vpn, 0, Qt::AlignVCenter);
    qr->addWidget(bt, 0, Qt::AlignVCenter);
    qr->addWidget(clip, 0, Qt::AlignVCenter);
    lay->addWidget(quickRow);
    connect(this, &ShellPopup::popupVisibleChanged, weather,
            [weather](bool visible) { weather->setPolling(visible); });

    lay->addWidget(new HLine(body()));

    /* ---- Output ---- */
    lay->addWidget(sectionLabel(QStringLiteral("Output"), body()));
    auto *outRow = new QWidget(body());
    auto *ol = new QHBoxLayout(outRow);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->setSpacing(Theme::rowSpacing);
    m_outIcon = new ClickableText(outRow);
    m_outIcon->setPixelSize(Theme::bodyFontSize);
    m_outIcon->setFixedWidth(20);
    m_outSlider = new ValueSlider(outRow);
    m_outSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_outPct = new TextItem(outRow);
    m_outPct->setPixelSize(Theme::smallFontSize);
    m_outPct->setHAlign(Qt::AlignRight);
    m_outPct->setFixedWidth(44);
    ol->addWidget(m_outIcon, 0, Qt::AlignVCenter);
    ol->addWidget(m_outSlider, 1, Qt::AlignVCenter);
    ol->addWidget(m_outPct, 0, Qt::AlignVCenter);
    lay->addWidget(outRow);

    connect(m_outIcon, &ClickableText::clicked, this, [pa]() {
        if (pa->hasSink)
            pa->toggleSinkMuted();
    });
    connect(m_outSlider, &ValueSlider::moved, this, [pa](qreal v) {
        if (pa->hasSink)
            pa->setSinkVolume(v);
    });

    /* ---- Input (mic) ---- */
    lay->addWidget(sectionLabel(QStringLiteral("Input"), body()));
    auto *micRow = new QWidget(body());
    auto *ml = new QHBoxLayout(micRow);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(Theme::rowSpacing);
    m_micIcon = new ClickableText(micRow);
    m_micIcon->setPixelSize(Theme::bodyFontSize);
    m_micIcon->setFixedWidth(20);
    m_micSlider = new ValueSlider(micRow);
    m_micSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_micPct = new TextItem(micRow);
    m_micPct->setPixelSize(Theme::smallFontSize);
    m_micPct->setHAlign(Qt::AlignRight);
    m_micPct->setFixedWidth(44);
    ml->addWidget(m_micIcon, 0, Qt::AlignVCenter);
    ml->addWidget(m_micSlider, 1, Qt::AlignVCenter);
    ml->addWidget(m_micPct, 0, Qt::AlignVCenter);
    lay->addWidget(micRow);

    connect(m_micIcon, &ClickableText::clicked, this, [pa]() {
        if (pa->hasSource)
            pa->toggleSourceMuted();
    });
    connect(m_micSlider, &ValueSlider::moved, this, [pa](qreal v) {
        if (pa->hasSource)
            pa->setSourceVolume(v);
    });

    lay->addWidget(new HLine(body()));
    lay->addWidget(sectionLabel(QStringLiteral("Output device"), body()));

    m_deviceBox = new QWidget(body());
    auto *dl = new QVBoxLayout(m_deviceBox);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(Theme::popupSpacing);
    lay->addWidget(m_deviceBox);

    connect(pa, &PulseBackend::changed, this, &SystemPopup::syncAudio);

    /* ---- Power ---- */
    lay->addWidget(new HLine(body()));
    lay->addWidget(sectionLabel(QStringLiteral("Power"), body()));

    auto *row1 = new QWidget(body());
    auto *r1 = new QHBoxLayout(row1);
    r1->setContentsMargins(0, 0, 0, 0);
    r1->setSpacing(Theme::rowSpacing);
    auto *lockBtn = new ShellButton(QStringLiteral("Lock"), row1);
    auto *sleepBtn = new ShellButton(QStringLiteral("Sleep"), row1);
    auto *logoutBtn = new ShellButton(QStringLiteral("Log Out"), row1);
    r1->addStretch(1);
    r1->addWidget(lockBtn);
    r1->addWidget(sleepBtn);
    r1->addWidget(logoutBtn);
    r1->addStretch(1);
    lay->addWidget(row1);

    connect(lockBtn, &ShellButton::activated, this, [this]() {
        power({"lock"});
        closePopup();
    });
    connect(sleepBtn, &ShellButton::activated, this, [this]() {
        power({"suspend"});
        closePopup();
    });
    connect(logoutBtn, &ShellButton::activated, this, [this]() {
        power({"logout"});
        closePopup();
    });

    auto *row2 = new QWidget(body());
    auto *r2 = new QHBoxLayout(row2);
    r2->setContentsMargins(0, 0, 0, 0);
    r2->setSpacing(Theme::rowSpacing);
    auto *rebootBtn = new ConfirmButton(QStringLiteral("Reboot"), row2);
    auto *poweroffBtn = new ConfirmButton(QStringLiteral("Power Off"), row2);
    r2->addStretch(1);
    r2->addWidget(rebootBtn);
    r2->addWidget(poweroffBtn);
    r2->addStretch(1);
    lay->addWidget(row2);

    rebootBtn->confirmed = [this]() {
        power({"reboot"});
        closePopup();
    };
    poweroffBtn->confirmed = [this]() {
        power({"poweroff"});
        closePopup();
    };

    lay->addStretch(0);

    syncAudio();
}

void SystemPopup::parseSysInfo(const QString &text)
{
    int cpu = -1, memPct = -1;
    QString tempStr;
    const QStringList lines = text.trimmed().split('\n');
    for (const QString &line : lines) {
        const int i = line.indexOf('=');
        if (i < 0)
            continue;
        const QString k = line.left(i), v = line.mid(i + 1);
        if (k == QLatin1String("cpu"))
            cpu = v.toInt();
        else if (k == QLatin1String("mem_pct"))
            memPct = v.toInt();
        else if (k == QLatin1String("temp"))
            tempStr = v;
    }

    if (cpu >= 0)
        m_cpuBox->setValue(QStringLiteral("%1%").arg(cpu));
    if (memPct >= 0)
        m_memBox->setValue(QStringLiteral("%1%").arg(memPct));
    m_tempBox->setVisible(!tempStr.isEmpty());
    if (!tempStr.isEmpty())
        m_tempBox->setValue(QStringLiteral("%1°C").arg(tempStr));

    if (isVisible())
        relayout();
}

void SystemPopup::syncAudio()
{
    PulseBackend *pa = PulseBackend::instance();

    m_outIcon->setText(pa->sinkMuted ? glyph(U'\U000F075F') : glyph(U''));
    m_outIcon->setColor(pa->sinkMuted ? Theme::danger : Theme::text);
    m_outSlider->setValue(pa->sinkVolume);
    m_outSlider->setFillColor(pa->sinkMuted ? Theme::textMuted
                                            : Theme::accent);
    m_outPct->setText(QString::number(qRound(pa->sinkVolume * 100))
                      + QStringLiteral("%"));

    m_micIcon->setText(pa->sourceMuted ? glyph(U'') : glyph(U''));
    m_micIcon->setColor(pa->sourceMuted ? Theme::danger : Theme::text);
    m_micSlider->setValue(pa->sourceVolume);
    m_micSlider->setFillColor(pa->sourceMuted ? Theme::textMuted
                                              : Theme::success);
    m_micPct->setText(QString::number(qRound(pa->sourceVolume * 100))
                      + QStringLiteral("%"));

    rebuildDevices();
}

void SystemPopup::rebuildDevices()
{
    PulseBackend *pa = PulseBackend::instance();
    QStringList key;
    for (const PulseBackend::Sink &s : pa->sinks)
        key << s.name + (s.isDefault ? QStringLiteral("*") : QString());
    if (key == m_deviceKey)
        return;
    m_deviceKey = key;

    auto *dl = static_cast<QVBoxLayout *>(m_deviceBox->layout());
    while (QLayoutItem *it = dl->takeAt(0)) {
        if (it->widget()) {
            it->widget()->hide(); /* deleteLater leaves it painted a tick */
            it->widget()->deleteLater();
        }
        delete it;
    }
    for (const PulseBackend::Sink &s : pa->sinks) {
        auto *row = new SinkRow(s, m_deviceBox);
        dl->addWidget(row);
        row->show();
    }
    if (isVisible())
        relayout();
}
