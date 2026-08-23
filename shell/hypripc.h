/* Hyprland IPC transport.
 *
 * Hyprland exposes two unix sockets per running instance, in
 * $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/ (Hyprland >= 0.40; older
 * builds used /tmp/hypr/$HIS/, which is still probed as a fallback):
 *
 *   .socket.sock   request/response — write a command, read the reply, the
 *                  compositor closes the connection. This is exactly what
 *                  `hyprctl` speaks, so we talk it directly and never spawn a
 *                  process. A `j/` prefix asks for JSON.
 *   .socket2.sock  a push event stream: one "EVENT>>DATA\n" line per change.
 *
 * This replaces the dwm build's EWMH root-window polling bridge entirely. */
#ifndef FANHYPR_QS_HYPRIPC_H
#define FANHYPR_QS_HYPRIPC_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QObject>
#include <QString>

namespace HyprIpc {

/* Directory holding the two sockets, or empty when there is no reachable
 * Hyprland instance (HYPRLAND_INSTANCE_SIGNATURE unset, or the socket gone). */
QString socketDir();
bool available();

/* Synchronous request on .socket.sock. Hyprland answers immediately over a
 * local socket and hangs up, so the block is on the order of microseconds;
 * returns empty on any failure. */
QByteArray request(const QByteArray &command);

/* request("j/" + command) parsed as JSON; returns an empty array on failure so
 * callers can iterate the result unconditionally. */
QJsonArray requestArray(const QByteArray &command);
QJsonDocument requestJson(const QByteArray &command);

/* Run a dispatcher.
 *
 * Hyprland 0.56 moved dispatch behind a Lua API: the payload is evaluated as
 * `return hl.dispatch(<payload>)`, so the classic `dispatch workspace 3` is
 * now a Lua *syntax error* rather than a command -- it fails silently from
 * the caller's point of view. Older builds only understand that classic
 * form. Callers therefore hand over both spellings and we remember which one
 * this compositor actually accepted, so the probe costs one extra round trip
 * once per process and nothing after that.
 *
 * Trying the wrong one first is harmless in both directions: a Lua payload on
 * an old build is an unknown dispatcher, and a classic payload on a new build
 * fails to parse. Neither has a side effect. */
bool dispatch(const QString &luaForm, const QString &legacyForm);

} /* namespace HyprIpc */

/* Persistent .socket2.sock reader. Emits one event() per line and reconnects
 * on its own if the compositor restarts under us. */
class HyprEventStream : public QObject {
    Q_OBJECT
public:
    explicit HyprEventStream(QObject *parent = nullptr);

    void start();
    bool isConnected() const;

signals:
    /* name/data split on the ">>" separator; data may be empty. Not called
     * `event`: that would hide QObject::event(QEvent *). */
    void hyprEvent(const QString &name, const QString &data);
    void connected();

private:
    void tryConnect();
    void onReadyRead();

    QLocalSocket m_sock;
    QByteArray m_buf;
};

#endif
