/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "win/maximize.h"

namespace KWin::win
{

template<>
void update_no_border(x11::window* win)
{
    if (!options->borderlessMaximizedWindows()) {
        // If maximized windows can have borders there is no change implied.
        return;
    }

    auto app_no_border = win->app_no_border;
    auto motif_no_border = win->motif_hints.hasDecoration() && win->motif_hints.noBorder();
    auto max_fully = win->geometry_update.max_mode == maximize_mode::full;
    auto no_border = app_no_border || motif_no_border || max_fully;

    win->setNoBorder(win->control->rules().checkNoBorder(no_border));
}

template<>
void respect_maximizing_aspect(x11::window* win, maximize_mode& mode)
{
    if (!win->geometry_hints.hasAspect()) {
        return;
    }
    if (mode != maximize_mode::vertical && mode != maximize_mode::horizontal) {
        return;
    }
    if (!win->control->rules().checkStrictGeometry(true)) {
        return;
    }

    // fixed aspect; on dimensional maximization obey aspect
    auto const min_aspect = win->geometry_hints.minAspect();
    auto const max_aspect = win->geometry_hints.maxAspect();

    auto const old_mode = win->geometry_update.max_mode;
    auto const area = get_maximizing_area(win);

    if (mode == maximize_mode::vertical || flags(old_mode & maximize_mode::vertical)) {
        // use doubles, because the values can be MAX_INT
        double const fx = min_aspect.width();
        double const fy = max_aspect.height();

        if (fx * area.height() / fy > area.width()) {
            // too big
            mode = flags(old_mode & maximize_mode::horizontal) ? maximize_mode::restore
                                                               : maximize_mode::full;
        }
    } else {
        // mode == maximize_mode::horizontal
        double const fx = max_aspect.width();
        double const fy = min_aspect.height();
        if (fy * area.width() / fx > area.height()) {
            // too big
            mode = flags(old_mode & maximize_mode::vertical) ? maximize_mode::restore
                                                             : maximize_mode::full;
        }
    }
}

}
