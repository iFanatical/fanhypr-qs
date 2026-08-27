/* The top panel: one wlr-layer-shell surface per output, anchored across the
 * top edge with an exclusive zone of Theme::panelHeight so the compositor
 * tiles every other window beneath it. Nothing in hyprland.conf is required
 * for that — reserving the strip is what the exclusive zone *is*, unlike the
 * dwm build where the WM had to be patched to leave the bar alone. */
#ifndef FANHYPR_QS_PANEL_H
#define FANHYPR_QS_PANEL_H

#include <QPointer>
#include <QScreen>
#include <QWidget>

#include "hyprstate.h"
#include "widgets.h"

class QHBoxLayout;

class WorkspaceButton : public QWidget {
    Q_OBJECT
public:
    explicit WorkspaceButton(const QString &label, QWidget *parent = nullptr);

    void setLabel(const QString &l);
    void setStates(bool selected, bool occupied);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QString m_label;
    bool m_selected = false;
    bool m_occupied = false;
    bool m_hover = false;
    bool m_pressed = false;
};

class Panel : public QWidget {
    Q_OBJECT
public:
    Panel(HyprState *state, QScreen *screen);

    /* Max characters shown for the window title before shortening. */
    static constexpr int maxTitleLength = 45;
    static QString shortenTitle(const QString &t);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    const HyprState::Monitor *mon() const;
    bool hostsTray() const;
    void syncFromState();
    void syncTitle();
    /* Positions m_leftBox / m_clock / m_rightBox: the clock sits at a fixed
     * horizontal center of the bar (clamped so it never collides with the
     * right-side pills), and m_leftBox gets whatever space is left of it. */
    void relayoutBar();

    HyprState *m_state;
    QPointer<QScreen> m_screen;

    QWidget *m_leftBox;
    QWidget *m_wsBox;
    QHBoxLayout *m_wsLayout;
    QVector<WorkspaceButton *> m_wsButtons;
    /* Workspace ids currently backing m_wsButtons, so the row is only rebuilt
     * when the output's assignment actually changes. */
    QVector<int> m_wsIds;
    TextItem *m_title;

    QWidget *m_clock;
    QWidget *m_rightBox;
};

#endif
