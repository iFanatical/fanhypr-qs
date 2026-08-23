#include "wlutil.h"

#include <QGuiApplication>
#include <QHash>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#include <LayerShellQt/Window>

/* Deliberately no LayerShellQt::Shell::useLayerShell(): it is deprecated as of
 * layer-shell-qt 6.6 and, per its own header, unnecessary since Qt 6.5 --
 * Window::get() now installs the shell integration for that window itself.
 * (If surfaces ever come up as ordinary xdg toplevels on an older stack, that
 * call, before the QGuiApplication, is the thing that used to arrange this.)
 * Likewise setScreen() rather than the deprecated setScreenConfiguration(). */

namespace {

using LsWindow = LayerShellQt::Window;

/* Screen-relative top-left we asked the compositor to place each surface at.
 * Keyed by QWidget because that is what screenPos() walks; entries are
 * dropped when the widget dies. */
QHash<const QWidget *, QPoint> g_origins;

LsWindow::Anchors toAnchors(int a)
{
    LsWindow::Anchors out;
    if (a & WlUtil::AnchorTop)
        out |= LsWindow::AnchorTop;
    if (a & WlUtil::AnchorBottom)
        out |= LsWindow::AnchorBottom;
    if (a & WlUtil::AnchorLeft)
        out |= LsWindow::AnchorLeft;
    if (a & WlUtil::AnchorRight)
        out |= LsWindow::AnchorRight;
    return out;
}

LsWindow::Layer toLayer(WlUtil::Layer l)
{
    switch (l) {
    case WlUtil::Layer::Background:
        return LsWindow::LayerBackground;
    case WlUtil::Layer::Bottom:
        return LsWindow::LayerBottom;
    case WlUtil::Layer::Overlay:
        return LsWindow::LayerOverlay;
    case WlUtil::Layer::Top:
        break;
    }
    return LsWindow::LayerTop;
}

LsWindow::KeyboardInteractivity toKeyboard(WlUtil::Keyboard k)
{
    switch (k) {
    case WlUtil::Keyboard::Exclusive:
        return LsWindow::KeyboardInteractivityExclusive;
    case WlUtil::Keyboard::OnDemand:
        return LsWindow::KeyboardInteractivityOnDemand;
    case WlUtil::Keyboard::None:
        break;
    }
    return LsWindow::KeyboardInteractivityNone;
}

/* The QWindow behind a widget, forcing it into existence. QtWayland does not
 * bind a shell surface (xdg_toplevel vs layer_surface) until the window is
 * first shown, so the LayerShellQt properties set between this and show()
 * are still in time to choose the role. */
QWindow *handleOf(QWidget *w)
{
    if (!w)
        return nullptr;
    w->winId();
    return w->windowHandle();
}

LsWindow *layerOf(QWidget *w)
{
    if (!WlUtil::active())
        return nullptr;
    QWindow *h = w ? w->windowHandle() : nullptr;
    return h ? LsWindow::get(h) : nullptr;
}

} /* namespace */

namespace WlUtil {

bool active()
{
    return QGuiApplication::platformName().startsWith(
        QLatin1String("wayland"), Qt::CaseInsensitive);
}

void configure(QWidget *w, QScreen *screen, const SurfaceSpec &spec)
{
    if (!w)
        return;

    /* Record the origin even without layer shell, so screenPos() stays
     * meaningful in the X11 debugging fallback. */
    g_origins.insert(w, QPoint(spec.margins.left(), spec.margins.top()));
    QObject::connect(w, &QObject::destroyed, w, [w]() { g_origins.remove(w); });

    if (!active())
        return;

    /* Pick the output before the native window exists: with no explicit
     * screen LayerShellQt falls back to QWindow::screen(), and a change after
     * the surface is bound is not re-negotiated. */
    if (screen)
        w->setScreen(screen);

    QWindow *h = handleOf(w);
    if (!h)
        return;
    if (screen)
        h->setScreen(screen);

    LsWindow *ls = LsWindow::get(h);
    if (!ls)
        return;
    if (screen)
        ls->setScreen(screen);
    ls->setScope(spec.scope);
    ls->setLayer(toLayer(spec.layer));
    ls->setAnchors(toAnchors(spec.anchors));
    ls->setMargins(spec.margins);
    if (!spec.desiredSize.isNull())
        ls->setDesiredSize(spec.desiredSize);
    ls->setExclusiveZone(spec.exclusiveZone);
    ls->setKeyboardInteractivity(toKeyboard(spec.keyboard));
    ls->setActivateOnShow(spec.activateOnShow);
    /* We drive visibility ourselves (toggle from the bar, Escape, the
     * scrim); a compositor-side dismissal must not destroy the widget. */
    ls->setCloseOnDismissed(false);
}

void setMargins(QWidget *w, const QMargins &m)
{
    if (!w)
        return;
    g_origins.insert(w, QPoint(m.left(), m.top()));
    if (LsWindow *ls = layerOf(w))
        ls->setMargins(m);
}

void setDesiredSize(QWidget *w, const QSize &size)
{
    if (LsWindow *ls = layerOf(w))
        ls->setDesiredSize(size);
}

void setKeyboard(QWidget *w, Keyboard k)
{
    if (LsWindow *ls = layerOf(w))
        ls->setKeyboardInteractivity(toKeyboard(k));
}

QPoint screenPos(const QWidget *w, const QPoint &rel)
{
    if (!w)
        return rel;
    const QWidget *top = w->window();
    /* mapTo() is pure widget-tree arithmetic, so unlike mapToGlobal() it is
     * exact on Wayland; the only unknown is where the top-level sits, which
     * is precisely what we recorded when we placed it. */
    const QPoint inTop = w->mapTo(top, rel);
    return inTop + g_origins.value(top, QPoint(0, 0));
}

} /* namespace WlUtil */
