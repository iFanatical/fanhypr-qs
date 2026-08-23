#include "ipc.h"

#include <QDir>
#include <QLocalSocket>
#include <QProcessEnvironment>

QString IpcServer::socketPath()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString dir = env.value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (dir.isEmpty())
        dir = QDir::tempPath();
    /* Keyed per compositor instance so two Hyprland sessions on one machine
     * don't fight over the socket. The instance signature is the natural key
     * (it is what the compositor's own sockets are keyed by); $WAYLAND_DISPLAY
     * covers a nested/other compositor, and the literal is the last resort. */
    QString key = env.value(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE"));
    if (key.isEmpty())
        key = env.value(QStringLiteral("WAYLAND_DISPLAY"),
                        QStringLiteral("wayland-0"));
    key.replace(QLatin1Char('/'), QLatin1Char('_'));
    return dir + QStringLiteral("/fanhypr-qs-shell-") + key
           + QStringLiteral(".sock");
}

IpcServer::IpcServer(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *sock = m_server.nextPendingConnection()) {
            connect(sock, &QLocalSocket::readyRead, sock, [this, sock]() {
                if (!sock->canReadLine())
                    return;
                const QString line =
                    QString::fromUtf8(sock->readLine()).trimmed();
                const QStringList parts =
                    line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                bool ok = false;
                if (parts.size() >= 2) {
                    const auto t = m_handlers.constFind(parts[0]);
                    if (t != m_handlers.constEnd()) {
                        const auto f = t->constFind(parts[1]);
                        if (f != t->constEnd()) {
                            (*f)();
                            ok = true;
                        }
                    }
                }
                sock->write(ok ? "ok\n" : "error\n");
                sock->flush();
                sock->disconnectFromServer();
            });
            connect(sock, &QLocalSocket::disconnected, sock,
                    &QLocalSocket::deleteLater);
        }
    });
}

bool IpcServer::listen()
{
    const QString path = socketPath();
    if (serverRunning())
        return false;
    QLocalServer::removeServer(path); /* stale socket */
    return m_server.listen(path);
}

void IpcServer::handle(const QString &target, const QString &function,
                       std::function<void()> fn)
{
    m_handlers[target][function] = std::move(fn);
}

bool IpcServer::serverRunning()
{
    QLocalSocket sock;
    sock.connectToServer(socketPath());
    return sock.waitForConnected(200);
}

int IpcServer::call(const QString &target, const QString &function)
{
    QLocalSocket sock;
    sock.connectToServer(socketPath());
    if (!sock.waitForConnected(1000)) {
        fprintf(stderr, "fanhypr-qs-shell: no running instance (socket %s)\n",
                qPrintable(socketPath()));
        return 1;
    }
    sock.write((target + QLatin1Char(' ') + function + QLatin1Char('\n'))
                   .toUtf8());
    sock.flush();
    if (!sock.waitForReadyRead(2000))
        return 1;
    const QByteArray reply = sock.readAll().trimmed();
    return reply == "ok" ? 0 : 1;
}
