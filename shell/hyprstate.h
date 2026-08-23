/* Live Hyprland state for the panel — the Wayland replacement for the dwm
 * build's EWMH/root-window DwmState.
 *
 * Everything comes off HyprIpc: a persistent .socket2.sock event stream tells
 * us *that* something changed, and a coalesced re-query of .socket.sock tells
 * us *what* it now is. Coalescing matters because a single user action fans
 * out into several events (moving a window emits movewindow + activewindow +
 * workspace, and a monitor hotplug emits a burst) and each refresh costs a
 * few socket round trips.
 *
 * There is no layout indicator: dwm cycled through a dozen layouts, so its
 * []= / [M] symbol carried real information, whereas Hyprland has exactly
 * two (dwindle and master) and switching between them is rare.
 *
 * Workspaces are presented as a fixed per-monitor block rather than dwm's
 * 9-bit tag mask: each output owns a contiguous run of workspace ids that is
 * always shown whether or not the workspace exists yet, so the bar never
 * reflows as workspaces come and go. The assignment is read from Hyprland's
 * own `workspace = N, monitor:X` rules when they exist, so the bar follows
 * hyprland.conf instead of duplicating it. */
#ifndef FANHYPR_QS_HYPRSTATE_H
#define FANHYPR_QS_HYPRSTATE_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include "hypripc.h"

class HyprState : public QObject {
    Q_OBJECT
public:
    struct Monitor {
        int id = 0;
        QString name;        /* "DP-1" — matches QScreen::name() */
        QString description;
        int x = 0, y = 0, w = 0, h = 0;
        bool focused = false;
        int activeWorkspace = 0;
        QString title;             /* focused window on this output */
        QVector<int> workspaces;   /* assigned ids, ascending */
    };

    struct Workspace {
        int id = 0;
        QString name;
        QString monitor;
        int windows = 0;
    };

    explicit HyprState(QObject *parent = nullptr);

    /* One shared instance; the panels, the launcher and the tray all key off
     * the same view of the compositor. */
    static HyprState *instance();

    QVector<Monitor> monitors;
    /* Only workspaces that currently exist; a fixed button whose id is absent
     * is simply an empty workspace. */
    QHash<int, Workspace> workspaces;
    /* Active keybind submap, empty when none is (the compositor calls that
     * "default"). Unlike everything else here this is fed straight from the
     * event stream rather than re-queried: `submap>>NAME` carries the whole
     * answer, with an empty payload meaning back-to-default. */
    QString submap;
    bool connected = false;

    const Monitor *monitorByName(const QString &name) const;
    /* The output that hosts the tray.
     *
     * Hyprland has no notion of a primary monitor -- there is no such field
     * on `j/monitors` -- so it is derived rather than configured: the output
     * owning the lowest-numbered workspace wins. That follows straight from
     * the user's own `workspace = N, monitor:X` rules (the output holding
     * workspace 1 is the one they treat as primary), and unlike "the focused
     * monitor" it doesn't move the tray around as focus changes. Falls back
     * to the focused output, then to the lowest monitor id. */
    QString primaryMonitor() const;
    /* The output the compositor currently has focus on. Unlike
     * primaryMonitor() this is expected to move around -- it is what the
     * launcher opens on, so it lands where the user is working rather than
     * yanking their attention to another screen. Falls back to the primary
     * when nothing reports focus (e.g. before the first refresh). */
    QString focusedMonitor() const;
    bool workspaceOccupied(int id) const;
    /* A named workspace shows its name; a plain numbered one shows the id,
     * including ids that do not exist yet. */
    QString workspaceLabel(int id) const;

    void switchWorkspace(int id);
    /* Leave the active submap. A no-op when already in the default one. */
    void resetSubmap();

signals:
    void changed();

private:
    void scheduleRefresh();
    void refresh();
    /* Re-read what only changes when hyprland.conf does. */
    void refreshConfig();
    void assignWorkspaces();
    static QString normalizeSubmap(const QString &raw);

    HyprEventStream m_events;
    QTimer m_coalesce;
    /* monitor name -> assigned workspace ids, from Hyprland's workspace rules;
     * empty when the config has no monitor-pinned rules and we fall back to
     * contiguous per-output blocks. */
    QHash<QString, QVector<int>> m_rules;
};

#endif
