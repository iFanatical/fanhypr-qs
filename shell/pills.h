/* Script-backed bar pills and singleton state helpers. */
#ifndef FANHYPR_QS_PILLS_H
#define FANHYPR_QS_PILLS_H

#include "procutil.h"
#include "widgets.h"

/* A read-only bar pill fed by a script that prints `icon=`, `text=`, and
 * optional `color=` lines. Polled on `interval`. Hides when it prints
 * nothing. */
class ScriptPill : public BarPill {
    Q_OBJECT
public:
    explicit ScriptPill(const QStringList &command, int intervalMs,
                        QWidget *parent = nullptr);

    /* QML `ScriptPill { tint: ... }` overrides the color= line entirely. */
    void setFixedTint(const QColor &c);

private:
    void parse(const QString &out);

    CollectorProcess m_proc;
    QColor m_fixedTint;
    bool m_hasFixedTint = false;
};

/* Active Hyprland keybind submap. Shows "global" in the default one -- always
 * visible rather than hiding when there is nothing to report, because the
 * point of a modal-keybind indicator is telling you which mode you are in,
 * and "none" is one of the answers. Clicking leaves the submap. */
class SubmapWidget : public BarPill {
    Q_OBJECT
public:
    explicit SubmapWidget(QWidget *parent = nullptr);

private:
    void sync();
};

/* WireGuard tun1 state (fanhypr-qs-vpn). No polling: the status is read once at
 * startup and refreshed whenever it is toggled — via the pill or
 * `fanhypr-qs-shell ipc call vpn toggle`. Singleton so every panel's pill and
 * the IPC handler share one state. */
class VpnState : public QObject {
    Q_OBJECT
public:
    static VpnState *instance();

    bool up = false;
    QString ip;

    void refresh();
    void toggle();

signals:
    void changed();

private:
    explicit VpnState(QObject *parent = nullptr);
    void parse(const QString &text, bool fromToggle);

    CollectorProcess m_statusProc;
    CollectorProcess m_toggleProc;
};

/* WireGuard indicator/toggle pill. Click = toggle, right-click = refresh. */
class VpnWidget : public BarPill {
    Q_OBJECT
public:
    explicit VpnWidget(QWidget *parent = nullptr);

private:
    void sync();
};

#endif
