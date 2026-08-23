#include "procutil.h"

#include <QCoreApplication>
#include <QPointer>
#include <QList>

#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
QList<QPointer<QProcess>> g_helpers;
bool g_hookInstalled = false;
} /* namespace */

namespace ProcUtil {

void reapWithParent(QProcess *proc)
{
    proc->setChildProcessModifier([]() {
        /* Kernel-enforced: when the thread that forked us dies -- cleanly,
         * crashed, or SIGKILLed -- the child gets SIGTERM. */
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        /* The parent could already have died between fork() and here, in
         * which case the disposition above will never fire. */
        if (getppid() == 1)
            _exit(1);
    });

    g_helpers.append(proc);
    if (!g_hookInstalled && qApp) {
        g_hookInstalled = true;
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp,
                         []() { terminateAll(); });
    }
}

void terminateAll()
{
    for (const QPointer<QProcess> &p : std::as_const(g_helpers)) {
        if (!p || p->state() == QProcess::NotRunning)
            continue;
        p->terminate();
        if (!p->waitForFinished(300))
            p->kill();
    }
    g_helpers.clear();
}

} /* namespace ProcUtil */

CollectorProcess::CollectorProcess(QObject *parent) : QObject(parent)
{
    ProcUtil::reapWithParent(&m_proc);
    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_proc, &QProcess::finished, this, [this]() {
        emit finished(QString::fromUtf8(m_proc.readAllStandardOutput()));
    });
}

void CollectorProcess::start()
{
    if (m_cmd.isEmpty() || m_proc.state() != QProcess::NotRunning)
        return;
    m_proc.start(m_cmd.first(), m_cmd.mid(1));
}

void CollectorProcess::start(const QStringList &cmd)
{
    if (m_proc.state() != QProcess::NotRunning)
        return;
    m_cmd = cmd;
    start();
}

BlockWatchProcess::BlockWatchProcess(const QStringList &cmd,
                                     const QByteArray &marker, QObject *parent)
    : QObject(parent), m_cmd(cmd), m_marker(marker)
{
    ProcUtil::reapWithParent(&m_proc);
    connect(&m_proc, &QProcess::readyReadStandardOutput, this,
            &BlockWatchProcess::onReadyRead);
}

void BlockWatchProcess::start()
{
    if (m_cmd.isEmpty() || m_proc.state() != QProcess::NotRunning)
        return;
    m_proc.start(m_cmd.first(), m_cmd.mid(1));
}

void BlockWatchProcess::onReadyRead()
{
    m_buf += m_proc.readAllStandardOutput();
    int idx;
    while ((idx = m_buf.indexOf(m_marker)) >= 0) {
        const QByteArray chunk = m_buf.left(idx);
        m_buf.remove(0, idx + m_marker.size());
        if (!chunk.trimmed().isEmpty())
            emit block(QString::fromUtf8(chunk));
    }
}
