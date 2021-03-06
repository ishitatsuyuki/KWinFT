/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "input.h"
#include "window.h"

#include "win/move.h"
#include "win/scene.h"

#include "atoms.h"
#include "composite.h"
#include "decorations/decorationbridge.h"
#include "decorations/window.h"

#include <KDecoration2/DecoratedClient>

namespace KWin::win::x11
{

template<typename Win>
void layout_decoration_rects(Win* win, QRect& left, QRect& top, QRect& right, QRect& bottom)
{
    if (!win::decoration(win)) {
        return;
    }

    auto rect = win::decoration(win)->rect();

    top = QRect(rect.x(), rect.y(), rect.width(), win::top_border(win));
    bottom = QRect(rect.x(),
                   rect.y() + rect.height() - win::bottom_border(win),
                   rect.width(),
                   win::bottom_border(win));
    left = QRect(rect.x(),
                 rect.y() + top.height(),
                 win::left_border(win),
                 rect.height() - top.height() - bottom.height());
    right = QRect(rect.x() + rect.width() - win::right_border(win),
                  rect.y() + top.height(),
                  win::right_border(win),
                  rect.height() - top.height() - bottom.height());
}

template<typename Win>
void detect_no_border(Win* win)
{
    if (win->shape()) {
        win->user_no_border = true;
        win->app_no_border = true;
        return;
    }

    switch (win->windowType()) {
    case NET::Desktop:
    case NET::Dock:
    case NET::TopMenu:
    case NET::Splash:
    case NET::Notification:
    case NET::OnScreenDisplay:
    case NET::CriticalNotification:
        win->user_no_border = true;
        win->app_no_border = true;
        break;
    case NET::Unknown:
    case NET::Normal:
    case NET::Toolbar:
    case NET::Menu:
    case NET::Dialog:
    case NET::Utility:
        win->user_no_border = false;
        break;
    default:
        abort();
    }

    // NET::Override is some strange beast without clear definition, usually
    // just meaning "no_border", so let's treat it only as such flag, and ignore it as
    // a window type otherwise (SUPPORTED_WINDOW_TYPES_MASK doesn't include it)
    if (win->info->windowType(NET::OverrideMask) == NET::Override) {
        win->user_no_border = true;
        win->app_no_border = true;
    }
}

template<typename Win>
void set_frame_extents(Win* win)
{
    NETStrut strut;
    strut.left = win::left_border(win);
    strut.right = win::right_border(win);
    strut.top = win::top_border(win);
    strut.bottom = win::bottom_border(win);
    win->info->setFrameExtents(strut);
}

template<typename Win>
void update_decoration(Win* win, bool check_workspace_pos, bool force = false)
{
    auto const has_no_border = win->user_no_border || win->geometry_update.fullscreen;

    if (!force
        && ((!win::decoration(win) && has_no_border) || (win::decoration(win) && !has_no_border))) {
        return;
    }

    auto old_frame_geo = win->geometry_update.frame;
    auto old_client_geo = old_frame_geo.adjusted(win::left_border(win),
                                                 win::top_border(win),
                                                 -win::right_border(win),
                                                 -win::bottom_border(win));
    win::block_geometry_updates(win, true);

    if (force) {
        win->control->destroy_decoration();
    }

    if (has_no_border) {
        win->control->destroy_decoration();
    } else {
        create_decoration(win);
    }

    win::update_shadow(win);

    if (check_workspace_pos) {
        win::check_workspace_position(win, old_frame_geo, -2, old_client_geo);
    }

    update_input_window(win, win->geometry_update.frame);
    win::block_geometry_updates(win, false);
    set_frame_extents(win);
}

template<typename Win>
void create_decoration(Win* win)
{
    if (win->noBorder()) {
        return;
    }

    win->control->deco().window = new Decoration::window(win);
    auto decoration
        = Decoration::DecorationBridge::self()->createDecoration(win->control->deco().window);

    if (decoration) {
        QMetaObject::invokeMethod(decoration, "update", Qt::QueuedConnection);

        QObject::connect(decoration, &KDecoration2::Decoration::shadowChanged, win, [win] {
            win::update_shadow(win);
        });
        QObject::connect(decoration,
                         &KDecoration2::Decoration::resizeOnlyBordersChanged,
                         win,
                         [win] { update_input_window(win, win->frameGeometry()); });

        QObject::connect(decoration, &KDecoration2::Decoration::bordersChanged, win, [win]() {
            set_frame_extents(win);

            update_server_geometry(win, win->frameGeometry());
            win->geometry_update.original.deco_margins = frame_margins(win);

            win->control->deco().client->update_size();
        });

        QObject::connect(win->control->deco().client->decoratedClient(),
                         &KDecoration2::DecoratedClient::widthChanged,
                         win,
                         [win] { update_input_window(win, win->frameGeometry()); });
        QObject::connect(win->control->deco().client->decoratedClient(),
                         &KDecoration2::DecoratedClient::heightChanged,
                         win,
                         [win] { update_input_window(win, win->frameGeometry()); });
    }

    win->control->deco().decoration = decoration;
    win->geometry_update.original.deco_margins = frame_margins(win);

    if (Compositor::compositing()) {
        win->discardWindowPixmap();
    }
}

template<typename Win>
void get_motif_hints(Win* win, bool initial = false)
{
    auto const wasClosable = win->motif_hints.close();
    auto const wasNoBorder = win->motif_hints.noBorder();

    if (!initial) {
        // Only on property change, initial read is prefetched.
        win->motif_hints.fetch();
    }

    win->motif_hints.read();

    if (win->motif_hints.hasDecoration() && win->motif_hints.noBorder() != wasNoBorder) {
        // If we just got a hint telling us to hide decorations, we do so but only do so if the app
        // didn't instruct us to hide decorations in some other way.
        if (win->motif_hints.noBorder()) {
            win->user_no_border = win->control->rules().checkNoBorder(true);
        } else if (!win->app_no_border) {
            win->user_no_border = win->control->rules().checkNoBorder(false);
        }
    }

    // mminimize; - Ignore, bogus - E.g. shading or sending to another desktop is "minimizing" too
    // mmaximize; - Ignore, bogus - Maximizing is basically just resizing

    auto const closabilityChanged = wasClosable != win->motif_hints.close();

    if (!initial) {
        // Check if noborder state has changed
        update_decoration(win, true);
    }
    if (closabilityChanged) {
        Q_EMIT win->closeableChanged(win->isCloseable());
    }
}

template<typename Win>
Xcb::StringProperty fetch_color_scheme(Win* win)
{
    return Xcb::StringProperty(win->xcb_windows.client, atoms->kde_color_sheme);
}

template<typename Win>
void read_color_scheme(Win* win, Xcb::StringProperty& property)
{
    win::set_color_scheme(win, win->control->rules().checkDecoColor(QString::fromUtf8(property)));
}

template<typename Win>
void update_color_scheme(Win* win)
{
    auto property = fetch_color_scheme(win);
    read_color_scheme(win, property);
}

}
