/* QProcess helpers for the script bridges:
 *   CollectorProcess — Process + StdioCollector (run, buffer stdout, emit)
 *   BlockWatchProcess — Process + SplitParser (long-lived, "\n\n" blocks)
 *
 * Both reap: every helper started here gets PR_SET_PDEATHSIG so the kernel
 * kills it when the bar's main thread goes away. Without that the long-lived
 * watchers outlive us -- a bar restarted a dozen times during a session left
 * a dozen orphaned `... watch` processes behind, each still polling. PDEATHSIG
 * covers even SIGKILL, which no amount of cleanup code in here could.
 *
 * Programs the user *launches* (desktopentry, the launcher, power actions)
 * deliberately do not go through here: those must outlive the bar. */
#ifndef FANHYPR_QS_PROCUTIL_H
#define FANHYPR_QS_PROCUTIL_H

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace ProcUtil {
/* Apply the die-with-parent child setup to a QProcess. */
void reapWithParent(QProcess *proc);
/* Terminate every helper still running; wired to QCoreApplication::aboutToQuit
 * so a clean exit doesn't have to rely on the kernel's PDEATHSIG. */
void terminateAll();
} /* namespace ProcUtil */

/* One-shot command; collect stdout; `finished(text)` when it exits. Starting
 * while already running is a no-op (QML `running = true` semantics). */
class CollectorProcess : public QObject {
    Q_OBJECT
public:
    explicit CollectorProcess(QObject *parent = nullptr);

    void setCommand(const QStringList &cmd) { m_cmd = cmd; }
    void start();
    void start(const QStringList &cmd);

signals:
    void finished(const QString &stdoutText);

private:
    QProcess m_proc;
    QStringList m_cmd;
};

/* Long-running watcher; stdout split on a marker (default "\n\n"), each chunk
 * emitted via block(). */
class BlockWatchProcess : public QObject {
    Q_OBJECT
public:
    explicit BlockWatchProcess(const QStringList &cmd,
                               const QByteArray &marker = "\n\n",
                               QObject *parent = nullptr);

    void start();

signals:
    void block(const QString &data);

private:
    void onReadyRead();

    QProcess m_proc;
    QStringList m_cmd;
    QByteArray m_marker;
    QByteArray m_buf;
};

#endif
