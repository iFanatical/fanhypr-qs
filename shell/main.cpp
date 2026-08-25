/* fanhypr-qs-shell: a hand-rolled C++/Qt bar for Hyprland. One process runs
 * the per-output panels, popups, SNI tray, and launcher.
 *
 *   fanhypr-qs-shell [--no-duplicate]        run the shell
 *   fanhypr-qs-shell ipc call <target> <fn>  poke a running instance
 *                    (targets: launcher, runner, notifications, vpn,
 *                     wallpaper) */
#include <QApplication>
#include <QHash>
#include <QScreen>

#include "hypripc.h"
#include "hyprstate.h"
#include "ipc.h"
#include "launcher.h"
#include "notification.h"
#include "panel.h"
#include "wallpaper.h"
#include "pills.h"
#include "wlutil.h"

int main(int argc, char *argv[])
{
    /* Client mode must not need a display connection. */
    QStringList rawArgs;
    for (int i = 1; i < argc; i++)
        rawArgs << QString::fromLocal8Bit(argv[i]);
    if (!rawArgs.isEmpty() && rawArgs[0] == QLatin1String("ipc")) {
        QStringList a = rawArgs.mid(1);
        if (!a.isEmpty() && a[0] == QLatin1String("call"))
            a.removeFirst();
        if (a.size() < 2) {
            fprintf(stderr,
                    "usage: fanhypr-qs-shell ipc call <target> <function>\n");
            return 2;
        }
        return IpcServer::call(a[0], a[1]);
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("fanhypr-qs-shell"));
    app.setQuitOnLastWindowClosed(false);

    if (rawArgs.contains(QLatin1String("--no-duplicate"))
            && IpcServer::serverRunning())
        return 0;

    if (!WlUtil::active())
        fprintf(stderr,
                "fanhypr-qs-shell: platform is '%s', not wayland — the bar "
                "will come up as an ordinary window with no reserved strip. "
                "Install dev-qt/qtwayland, or force it with "
                "QT_QPA_PLATFORM=wayland.\n",
                qPrintable(QGuiApplication::platformName()));
    if (!HyprIpc::available())
        fprintf(stderr,
                "fanhypr-qs-shell: no Hyprland instance reachable "
                "(HYPRLAND_INSTANCE_SIGNATURE unset or socket missing) — "
                "workspaces and the window title stay empty until one "
                "appears.\n");

    HyprState *state = HyprState::instance();
    if (!NotificationService::instance()->start())
        fprintf(stderr,
                "fanhypr-qs-shell: cannot own org.freedesktop.Notifications "
                "-- stop dunst or another notification daemon first.\n");

    /* One panel per output, following hotplug. */
    QHash<QScreen *, Panel *> panels;
    auto addPanel = [&panels, state](QScreen *s) {
        if (panels.contains(s))
            return;
        auto *p = new Panel(state, s);
        panels.insert(s, p);
        p->show();
    };
    const QList<QScreen *> screens = app.screens();
    for (QScreen *s : screens)
        addPanel(s);
    QObject::connect(&app, &QGuiApplication::screenAdded, &app, addPanel);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app,
                     [&panels](QScreen *s) {
                         if (Panel *p = panels.take(s))
                             p->deleteLater();
                     });

    /* Single centered launcher, opened on the currently focused output. */
    auto *launcher = new AppLauncher(&app);

    /* Built now rather than on first use: it scans the wallpaper directory
     * and asks awww what is displayed, and it decodes its thumbnails one per
     * event-loop turn. Left lazy, the first open would race its own query and
     * pay the whole decode cost on the spot. */
    WallpaperPicker::instance();

    auto *ipc = new IpcServer(&app);
    ipc->handle(QStringLiteral("launcher"), QStringLiteral("toggle"),
                [launcher]() { launcher->toggleMode(QStringLiteral("apps")); });
    ipc->handle(QStringLiteral("launcher"), QStringLiteral("show"),
                [launcher]() { launcher->openMode(QStringLiteral("apps")); });
    ipc->handle(QStringLiteral("launcher"), QStringLiteral("hide"),
                [launcher]() { launcher->hideLauncher(); });
    ipc->handle(QStringLiteral("runner"), QStringLiteral("toggle"),
                [launcher]() { launcher->toggleMode(QStringLiteral("run")); });
    ipc->handle(QStringLiteral("runner"), QStringLiteral("show"),
                [launcher]() { launcher->openMode(QStringLiteral("run")); });
    ipc->handle(QStringLiteral("runner"), QStringLiteral("hide"),
                [launcher]() { launcher->hideLauncher(); });
    ipc->handle(QStringLiteral("notifications"), QStringLiteral("toggle-dnd"),
                []() { NotificationService::instance()->toggleDnd(); });
    /* Compatibility with the old keybind while migrating away from dunst. */
    ipc->handle(QStringLiteral("dunst"), QStringLiteral("toggle"),
                []() { NotificationService::instance()->toggleDnd(); });
    ipc->handle(QStringLiteral("vpn"), QStringLiteral("toggle"),
                []() { VpnState::instance()->toggle(); });
    ipc->handle(QStringLiteral("vpn"), QStringLiteral("refresh"),
                []() { VpnState::instance()->refresh(); });
    ipc->handle(QStringLiteral("wallpaper"), QStringLiteral("toggle"),
                []() { WallpaperPicker::instance()->togglePicker(); });
    ipc->handle(QStringLiteral("wallpaper"), QStringLiteral("show"),
                []() { WallpaperPicker::instance()->openPicker(); });
    ipc->handle(QStringLiteral("wallpaper"), QStringLiteral("hide"),
                []() { WallpaperPicker::instance()->hidePicker(); });
    ipc->listen();

    return app.exec();
}
