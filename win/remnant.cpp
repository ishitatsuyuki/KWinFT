/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "remnant.h"

#include "control.h"
#include "geo.h"
#include "meta.h"
#include "net.h"
#include "transient.h"
#include "wayland/window.h"
#include "x11/window.h"

#include "decorations/decorationrenderer.h"

#include <cassert>

namespace KWin::win
{

remnant::remnant(Toplevel* win, Toplevel* source)
    : win{win}
{
    assert(!win->remnant());

    frame_margins = win::frame_margins(source);
    render_region = source->render_region();
    buffer_scale = source->bufferScale();
    desk = source->desktop();
    activities = source->activities();
    frame = source->frameId();
    opacity = source->opacity();
    window_type = source->windowType();
    window_role = source->windowRole();

    if (source->control) {
        no_border = source->noBorder();
        if (!no_border) {
            source->layoutDecorationRects(
                decoration_left, decoration_top, decoration_right, decoration_bottom);
            if (win::decoration(source)) {
                if (auto renderer = source->control->deco().client->renderer()) {
                    decoration_renderer = renderer;
                    decoration_renderer->reparent(win);
                }
            }
        }
        minimized = source->control->minimized();

        fullscreen = source->control->fullscreen();
        keep_above = source->control->keep_above();
        keep_below = source->control->keep_below();
        caption = win::caption(source);

        was_active = source->control->active();
    }

    win->transient()->annexed = source->transient()->annexed;

    int alive_count = 0;
    auto const leads = source->transient()->leads();
    for (auto const& lead : leads) {
        lead->transient()->add_child(win);
        lead->transient()->remove_child(source);
        refcount++;
        if (!lead->remnant()) {
            alive_count++;
        }
    }

    if (alive_count > 0) {
        // Alive leads might go down next or not. Since we have no information about that we wait
        // for a short period and check again. All leads not being remnant until then we classify
        // as being alive and just unref the remnant.
        annexed_timeout = new QTimer;
        annexed_timeout->setSingleShot(true);
        QObject::connect(annexed_timeout, &QTimer::timeout, win, [this] {
            for (auto lead : this->win->transient()->leads()) {
                if (!lead->remnant()) {
                    unref();
                }
            }
        });
        annexed_timeout->start(100);
    }

    auto const children = source->transient()->children;
    for (auto const& child : children) {
        win->transient()->add_child(child);
        source->transient()->remove_child(child);
    }

    win->transient()->set_modal(source->transient()->modal());
    was_group_transient = source->groupTransient();

    for (auto vd : win->desktops()) {
        QObject::connect(vd, &QObject::destroyed, win, [=] {
            auto desks = win->desktops();
            desks.removeOne(vd);
            win->set_desktops(desks);
        });
    }

    was_wayland_client = qobject_cast<win::wayland::window*>(source) != nullptr;
    was_x11_client = qobject_cast<win::x11::window*>(source) != nullptr;
    was_popup_window = win::is_popup(source);
    was_outline = source->isOutline();
    was_lock_screen = source->isLockScreen();

    if (source->control) {
        control = std::make_unique<win::control>(win);
    }
}

remnant::~remnant()
{
    if (refcount != 0) {
        qCCritical(KWIN_CORE) << "Deleted client has non-zero reference count (" << refcount << ")";
    }
    assert(refcount == 0);

    if (workspace()) {
        workspace()->removeDeleted(win);
    }

    win->deleteEffectWindow();
}

void remnant::ref()
{
    ++refcount;
}

void remnant::unref()
{
    if (--refcount > 0) {
        return;
    }

    // Need to delete the timer as we delete the remnant from the event loop.
    delete annexed_timeout;
    annexed_timeout = nullptr;

    // needs to be delayed
    // a) when calling from effects, otherwise it'd be rather complicated to handle the case of the
    // window going away during a painting pass
    // b) to prevent dangeling pointers in the stacking order, see bug #317765
    win->deleteLater();
}

void remnant::discard()
{
    refcount = 0;
    delete win;
}

bool remnant::was_transient() const
{
    return win->transient()->lead();
}

bool remnant::has_lead(Toplevel const* toplevel) const
{
    return contains(win->transient()->leads(), toplevel);
}

void remnant::layout_decoration_rects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
{
    left = decoration_left;
    top = decoration_top;
    right = decoration_right;
    bottom = decoration_bottom;
}

}
