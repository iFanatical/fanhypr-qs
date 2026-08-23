/* Wallpaper picker, backed by the awww daemon.
 *
 * A fullscreen layer surface in the launcher's mould: a centred box holding a
 * grid of thumbnails, keyboard-navigable, dismissed with Escape or a click
 * outside the box. Raised over IPC so it can be bound to a key:
 *
 *     fanhypr-qs-shell ipc call wallpaper toggle|show|hide
 *
 * awww is an swww fork and speaks the same CLI: `awww img <path>` sets the
 * wallpaper on every output (the default), and `awww query` reports what each
 * output is currently displaying, which is what marks the active thumbnail.
 *
 * The directory is scanned NON-recursively on purpose -- a `backup/` subtree
 * of old wallpapers lives under it that has no business in the picker. */
#ifndef FANHYPR_QS_WALLPAPER_H
#define FANHYPR_QS_WALLPAPER_H

#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "procutil.h"

struct Wallpaper {
    QString path;
    QString name; /* basename without the extension */
    QPixmap thumb;
    bool decoded = false;
};

class WallpaperWindow;

class WallpaperPicker : public QObject {
    Q_OBJECT
public:
    static WallpaperPicker *instance();

    /* Directory scanned for wallpapers; $FANHYPR_QS_WALLPAPER_DIR overrides
     * the default of ~/Pictures/wallpapers. */
    static QString wallpaperDir();
    /* awww transition, $FANHYPR_QS_WALLPAPER_TRANSITION overrides. */
    static QString transition();

    QVector<Wallpaper> entries;
    int selected = 0;
    /* Path awww reports as currently displayed, so the picker can mark it. */
    QString current;

    void rescan();
    void refreshCurrent();
    void apply(int index);
    void move(int delta);
    void syncSelectionToCurrent();

    void openPicker();
    void togglePicker();
    void hidePicker();

    static constexpr int columns = 4;
    /* 16:9, comfortably legible without making the box fill the screen. */
    static constexpr int thumbW = 288;
    static constexpr int thumbH = 162;
    /* Room under the thumbnail for the filename. */
    static constexpr int cellPad = 8;
    static constexpr int labelH = 20;
    static int cellW() { return thumbW + 2 * cellPad; }
    static int cellH() { return thumbH + labelH + 3 * cellPad; }

signals:
    void changed();
    void selectionChanged();

private:
    explicit WallpaperPicker(QObject *parent = nullptr);
    /* Decodes one pending thumbnail per event-loop turn. Decoding all of them
     * at once costs a few hundred ms even with scaled reads, which would be a
     * visible hitch; spreading it keeps the UI responsive and, since this
     * starts at construction, they are normally ready before the picker is
     * first opened. */
    void decodeNext();

    WallpaperWindow *m_win;
    QTimer m_decoder;
    CollectorProcess m_query;
};

/* The results grid: fixed columns, scrolls when it overflows. */
class WallpaperGrid : public QWidget {
    Q_OBJECT
public:
    explicit WallpaperGrid(WallpaperPicker *p, QWidget *parent = nullptr);

    int contentHeight() const;
    void ensureVisible();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    int indexAt(const QPoint &p) const;
    void clampScroll();

    WallpaperPicker *m_p;
    int m_scroll = 0;
};

class WallpaperWindow : public QWidget {
    Q_OBJECT
public:
    explicit WallpaperWindow(WallpaperPicker *p);

    void openOn(QScreen *screen);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    void layoutBox();

    WallpaperPicker *m_p;
    WallpaperGrid *m_grid;
    /* The layer surface is bound before its first map and rebuilt when the
     * picker moves output -- see WallpaperWindow::openOn(). */
    bool m_configured = false;
    QPointer<QScreen> m_screen;
};

#endif
