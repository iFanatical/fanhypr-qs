/* Script-backed bar pills and singleton state helpers. */
#ifndef FANHYPR_QS_PILLS_H
#define FANHYPR_QS_PILLS_H

#include "popup.h"
#include "procutil.h"
#include "widgets.h"

#include <QHash>

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

/* Sequential WireGuard tunnel state (fanhypr-qs-vpn). FANHYPR_TUN_COUNT
 * exposes tun1..tunN (default one). No polling: status is read once at startup
 * and refreshed after a selection. */
class VpnState : public QObject {
    Q_OBJECT
public:
    static VpnState *instance();

    void refresh();
    void toggle();
    void select(const QString &tunnel);
    bool isUp(const QString &tunnel) const;
    QString address(const QString &tunnel) const;
    QString activeTunnel() const;
    const QStringList &tunnels() const { return m_tunnels; }

signals:
    void changed();

private:
    explicit VpnState(QObject *parent = nullptr);
    void parse(const QString &text, bool fromToggle);

    CollectorProcess m_statusProc;
    CollectorProcess m_toggleProc;
    QStringList m_tunnels;
    QHash<QString, bool> m_up;
    QHash<QString, QString> m_ips;
};

class VpnPopup;

/* WireGuard indicator/selector pill. Click = tunnel menu, right-click =
 * refresh. */
class VpnWidget : public BarPill {
    Q_OBJECT
public:
    explicit VpnWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(0, sizeHint().height()); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void sync();

    VpnPopup *m_popup = nullptr;
};

class VpnPopup : public ContentPopup {
    Q_OBJECT
public:
    explicit VpnPopup(QWidget *anchor);

private:
    void sync();

    QVector<ShellButton *> m_buttons;
};

#endif
