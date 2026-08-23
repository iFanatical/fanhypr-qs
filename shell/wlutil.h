/* wlr-layer-shell helpers — the Wayland replacement for the dwm build's
 * x11util.
 *
 * Under X11 the panel was an override-redirect _NET_WM_WINDOW_TYPE_DOCK that
 * the WM was patched to leave alone, and popups held raw XGrabKeyboard /
 * XGrabPointer grabs. None of that exists on Wayland, and none of it is
 * needed: wlr-layer-shell is the protocol built for exactly this job. A layer
 * surface picks its layer and its screen edges, reserves an exclusive zone
 * the compositor honours for every other window (so Hyprland tiles under the
 * bar with no config on the user's side), and asks for keyboard focus through
 * the protocol instead of grabbing it behind the compositor's back.
 *
 * We bind it via LayerShellQt (kde-plasma/layer-shell-qt), which teaches
 * QtWayland to give a QWindow the layer-surface role.
 *
 * Position: a Wayland client is never told where it is on screen, so
 * QWidget::mapToGlobal() is meaningless across top-levels and popups cannot
 * be placed with it. Instead every layer surface we create records the
 * screen-relative origin we asked the compositor for, and screenPos() walks
 * a widget up to its top-level and adds that origin back -- exact, because we
 * chose the margins ourselves. */
#ifndef FANHYPR_QS_WLUTIL_H
#define FANHYPR_QS_WLUTIL_H

#include <QMargins>
#include <QPoint>
#include <QSize>
#include <QString>

class QScreen;
class QWidget;

namespace WlUtil {

enum Anchor {
    AnchorTop = 1,
    AnchorBottom = 2,
    AnchorLeft = 4,
    AnchorRight = 8,
};

enum class Layer { Background, Bottom, Top, Overlay };

enum class Keyboard {
    None,     /* never focused — the bar itself, and the popup scrim */
    /* Focused on click, and on map when SurfaceSpec::activateOnShow is set.
     * This is what popups want: Exclusive additionally makes the compositor
     * route *pointer* input to the surface, so clicks meant for the bar or
     * for the click-outside scrim never arrive and only Escape can dismiss. */
    OnDemand,
    Exclusive, /* compositor hands over keyboard *and* pointer */
};

struct SurfaceSpec {
    Layer layer = Layer::Top;
    int anchors = AnchorTop | AnchorLeft;
    QMargins margins;
    /* Size requested of the compositor. A 0 in either axis means "you
     * decide", which is the correct request whenever the surface is anchored
     * to both edges of that axis -- the bar asks for (0, panelHeight) and
     * gets the output's width, whatever another exclusive-zone surface or a
     * fractional scale has left of it. */
    QSize desiredSize;
    /* >0 reserves that many pixels along the anchored edge; 0 requests no
     * reservation but still respects other surfaces' zones; -1 ignores every
     * other exclusive zone, which is what a popup wants so its margins are
     * measured from the true screen edge and not from under the bar. */
    int exclusiveZone = -1;
    Keyboard keyboard = Keyboard::None;
    /* Ask the compositor for focus as the surface maps. Ignored when
     * keyboard is None. Lets a popup be focused on open without taking the
     * pointer grab that Exclusive implies. */
    bool activateOnShow = true;
    QString scope = QStringLiteral("fanhypr-qs");
};

/* True when we actually got a Wayland platform plugin; everything below is a
 * no-op otherwise, so the panel still runs (as a plain top-level) under a
 * nested X server for debugging. */
bool active();

/* Give a widget the layer-surface role. Call before the widget is first
 * shown; it forces the native window into existence, which is what lets the
 * LayerShellQt properties be set while QtWayland can still act on them. */
void configure(QWidget *w, QScreen *screen, const SurfaceSpec &spec);

/* Re-place / re-focus / re-size an already-configured surface (popups move
 * between anchors and grow as their content changes). */
void setMargins(QWidget *w, const QMargins &m);
void setDesiredSize(QWidget *w, const QSize &size);
void setKeyboard(QWidget *w, Keyboard k);

/* Screen-relative position of `rel` within `w`, resolved through the
 * top-level's recorded layer-surface origin. */
QPoint screenPos(const QWidget *w, const QPoint &rel = QPoint());

} /* namespace WlUtil */

#endif
