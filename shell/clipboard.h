/* Clipboard history, backed by wl-clipboard -- tracks the CLIPBOARD
 * selection only (text), not PRIMARY. A pill that opens a
 * pick-one-to-restore popup, closest in spirit to the launcher's grid rather
 * than the other widgets' stay-open control panels.
 *
 * Why not QClipboard, as the X11 build used? On Wayland a client may only
 * read the clipboard while it holds focus, so QClipboard::dataChanged never
 * fires for a copy made in another window -- which is every copy worth
 * recording. Reading it regardless is the job of the wlr-data-control
 * protocol, and `wl-paste --watch` is its reference implementation: the
 * compositor hands it each new selection whoever owns the focus.
 *
 * The watcher pipes each selection through `cat` and appends a NUL, giving a
 * NUL-delimited stream of exact bytes that BlockWatchProcess splits -- no
 * newline mangling, and no per-poll process. Restoring an entry shells out
 * to wl-copy, which daemonizes itself to serve the selection afterwards. */
#ifndef FANHYPR_QS_CLIPBOARD_H
#define FANHYPR_QS_CLIPBOARD_H

#include <QString>
#include <QVector>

#include "popup.h"
#include "procutil.h"
#include "widgets.h"

class ClipboardState : public QObject {
    Q_OBJECT
public:
    static ClipboardState *instance();

    static constexpr int maxHistory = 50;
    QVector<QString> history; /* most recent first */
    /* False when wl-paste is not installed: the widget then hides itself,
     * the same way the battery and weather widgets do without their
     * backing tool. */
    bool available = false;

    void selectEntry(int index);
    void removeEntry(int index);
    void clear();

signals:
    void changed();

private:
    explicit ClipboardState(QObject *parent = nullptr);
    void addEntry(const QString &text);

    BlockWatchProcess *m_watch = nullptr;
};

class ClipboardPopup;

class ClipboardWidget : public BarPill {
    Q_OBJECT
public:
    explicit ClipboardWidget(QWidget *parent = nullptr);

private:
    ClipboardPopup *m_popup;
};

class ClipboardPopup : public ContentPopup {
    Q_OBJECT
public:
    explicit ClipboardPopup(QWidget *anchor);

private:
    void rebuild();

    ShellButton *m_clearBtn;
    QWidget *m_listBox;
};

#endif
