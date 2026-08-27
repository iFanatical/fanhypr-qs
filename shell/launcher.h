/* Centered, IPC-triggered launcher with three modes:
 *   "apps" — .desktop entries;  "run" — executables on $PATH;
 *   "emoji" — Unicode name search copied through wl-copy.
 * A fullscreen wlr-layer-shell surface on the Overlay layer with exclusive
 * keyboard focus, so it needs no compositor-side window rule at all (the dwm
 * build had to be taught to recognise it by window title). Clicking outside
 * the box dismisses it. */
#ifndef FANHYPR_QS_LAUNCHER_H
#define FANHYPR_QS_LAUNCHER_H

#include <QLineEdit>
#include <QPointer>
#include <QScreen>
#include <QWidget>

#include "desktopentry.h"
#include "procutil.h"
#include "widgets.h"

struct LauncherItem {
    QString name;
    QString sub;
    QString icon;
    /* A value copy, not an index into DesktopEntries::applications(): that
     * vector can be rescanned (and reordered/resized) by DesktopEntries'
     * live-reload at any time, including between building this list and the
     * user picking an entry from it. Indexing into it at launch time raced
     * exactly that rescan -- launching whatever now sits at the stale index,
     * or silently nothing. Carrying the resolved entry sidesteps the race
     * entirely. */
    bool isApp = false;
    bool isEmoji = false;
    DesktopEntry app;  /* apps mode */
    QString cmd;       /* binary name (run mode) */
};

struct EmojiEntry {
    QString value;
    QString name;
    QString keywords;
};

class LauncherWindow;

class AppLauncher : public QObject {
    Q_OBJECT
public:
    explicit AppLauncher(QObject *parent = nullptr);

    QString mode = QStringLiteral("apps"); /* "apps" | "run" | "emoji" */
    QStringList binaries;
    QVector<EmojiEntry> emojis;
    QVector<LauncherItem> entries;
    int selected = 0;
    static constexpr int columns = 2;
    QString searchText;

    void refilter();
    void launchSelected();
    void move(int delta);
    void movePage(int delta);
    void openMode(const QString &m);
    void toggleMode(const QString &m);
    void hideLauncher();

    QScreen *pickScreen() const;

signals:
    void entriesChanged();
    void selectedChanged();

private:
    void scanBinaries();

    LauncherWindow *m_win;
    CollectorProcess m_binScan;
};

/* The results grid: 2 columns, 48px cells, GridView-style clipping/scroll. */
class LauncherGrid : public QWidget {
    Q_OBJECT
public:
    explicit LauncherGrid(AppLauncher *l, QWidget *parent = nullptr);

    int contentHeight() const;
    int pageStep() const;
    void ensureVisible();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    int cellWidth() const { return width() / AppLauncher::columns; }
    int indexAt(const QPoint &p) const;
    void clampScroll();

    AppLauncher *m_l;
    int m_scroll = 0;
};

class LauncherWindow : public QWidget {
    Q_OBJECT
public:
    LauncherWindow(AppLauncher *l);

    void openOn(QScreen *screen);
    QLineEdit *search() const { return m_search; }

protected:
    void mousePressEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *o, QEvent *e) override;
    void keyPressEvent(QKeyEvent *) override;
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    bool handleKey(QKeyEvent *k);
    void layoutBox();

    AppLauncher *m_l;
    /* The layer surface is configured before its first map, and rebuilt when
     * the launcher moves to a different output -- see openOn(). */
    bool m_configured = false;
    QPointer<QScreen> m_screen;
    QWidget *m_box;
    QWidget *m_searchBar;
    QLineEdit *m_search;
    TextItem *m_placeholder;
    LauncherGrid *m_grid;
};

#endif
