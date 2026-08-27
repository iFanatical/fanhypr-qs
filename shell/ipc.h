/* Control socket, driven by `fanhypr-qs-shell ipc call <target> <fn>` --
 * this is what a Hyprland `bind` invokes to raise the launcher:
 *   fanhypr-qs-shell ipc call launcher toggle|show|hide
 *   fanhypr-qs-shell ipc call runner   toggle|show|hide
 *   fanhypr-qs-shell ipc call notifications toggle-dnd
 *   fanhypr-qs-shell ipc call shell quit
 * A QLocalServer on $XDG_RUNTIME_DIR keyed by $DISPLAY. */
#ifndef FANHYPR_QS_IPC_H
#define FANHYPR_QS_IPC_H

#include <QLocalServer>
#include <QMap>
#include <QObject>

#include <functional>

class IpcServer : public QObject {
    Q_OBJECT
public:
    explicit IpcServer(QObject *parent = nullptr);

    bool listen();
    void handle(const QString &target, const QString &function,
                std::function<void()> fn);

    static QString socketPath();
    /* Client side; returns process exit code. */
    static int call(const QString &target, const QString &function);
    static bool serverRunning();

private:
    QLocalServer m_server;
    QMap<QString, QMap<QString, std::function<void()>>> m_handlers;
};

#endif
