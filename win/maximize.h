/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/move.h"
#include "win/net.h"
#include "win/types.h"

#include "placement.h"

namespace KWin::win
{

template<typename Win>
void update_no_border(Win* win)
{
    if (!options->borderlessMaximizedWindows()) {
        // If maximized windows can have borders there is no change implied.
        return;
    }

    auto no_border = win->geometry_update.max_mode == maximize_mode::full;
    win->setNoBorder(win->control->rules().checkNoBorder(no_border));
}

template<typename Win>
void set_restore_geometry(Win* win, QRect const& restore_geo)
{
    if (win->geometry_update.fullscreen) {
        // We keep the restore geometry for later fullscreen restoration.
        return;
    }
    if (win->control->quicktiling() != quicktiles::none) {
        // We keep the restore geometry for later quicktile restoration.
        return;
    }
    if (is_move(win)) {
        // We keep the restore geometry from the move.
        return;
    }

    win->restore_geometries.maximize = restore_geo;
}

template<typename Win>
QRect get_maximizing_area(Win* win)
{
    QRect area;

    if (win->control->electric_maximizing()) {
        area = workspace()->clientArea(MaximizeArea, Cursor::pos(), win->desktop());
    } else {
        area = workspace()->clientArea(MaximizeArea, win);
    }

    return area;
}

template<typename Win>
QRect rectify_restore_geometry(Win* win, QRect restore_geo)
{
    if (restore_geo.isValid()) {
        return restore_geo;
    }

    auto area = get_maximizing_area(win);

    auto frame_size = QSize(area.width() * 2 / 3, area.height() * 2 / 3);
    if (restore_geo.width() > 0) {
        frame_size.setWidth(restore_geo.width());
    }
    if (restore_geo.height() > 0) {
        frame_size.setHeight(restore_geo.height());
    }

    geometry_updates_blocker blocker(win);
    auto const old_frame_geo = win->geometry_update.frame;

    // We need to do a temporary placement to find the right coordinates.
    win->setFrameGeometry(QRect(QPoint(), frame_size));
    Placement::self()->placeSmart(win, area);

    // Get the geometry and reset back to original geometry.
    restore_geo = win->geometry_update.frame;
    win->setFrameGeometry(old_frame_geo);

    if (restore_geo.width() > 0) {
        restore_geo.moveLeft(restore_geo.x());
    }
    if (restore_geo.height() > 0) {
        restore_geo.moveTop(restore_geo.y());
    }

    return restore_geo;
}

template<typename Win>
void update_frame_from_restore_geometry(Win* win, QRect const& restore_geo)
{
    win->setFrameGeometry(rectify_restore_geometry(win, restore_geo));
}

template<typename Win>
void maximize_restore(Win* win)
{
    auto const old_mode = win->geometry_update.max_mode;
    auto const restore_geo = win->restore_geometries.maximize;
    auto final_restore_geo = win->geometry_update.frame;

    if (flags(old_mode & maximize_mode::vertical)) {
        final_restore_geo.setTop(restore_geo.top());
        final_restore_geo.setBottom(restore_geo.bottom());
    }
    if (flags(old_mode & maximize_mode::horizontal)) {
        final_restore_geo.setLeft(restore_geo.left());
        final_restore_geo.setRight(restore_geo.right());
    }

    geometry_updates_blocker blocker(win);
    update_frame_from_restore_geometry(win, final_restore_geo);

    if (win->info) {
        // TODO(romangg): That is x11::window only. Put it in a template specialization?
        win->info->setState(NET::States(), NET::Max);
    }
    win->geometry_update.max_mode = maximize_mode::restore;
    update_no_border(win);
    set_restore_geometry(win, QRect());
}

template<typename Win>
void maximize_vertically(Win* win)
{
    auto& geo_update = win->geometry_update;
    auto const old_frame_geo = geo_update.frame;
    auto const area = get_maximizing_area(win);

    auto pos = QPoint(old_frame_geo.x(), area.top());
    pos = win->control->rules().checkPosition(pos);

    auto size = QSize(old_frame_geo.width(), area.height());
    size = win->control->adjusted_frame_size(size, size_mode::fixed_height);

    geometry_updates_blocker blocker(win);
    win->setFrameGeometry(QRect(pos, size));

    if (win->info) {
        auto net_state
            = flags(geo_update.max_mode & maximize_mode::horizontal) ? NET::Max : NET::MaxVert;
        win->info->setState(net_state, NET::Max);
    }
    geo_update.max_mode |= maximize_mode::vertical;
    update_no_border(win);
    set_restore_geometry(win, old_frame_geo);
}

template<typename Win>
void maximize_horizontally(Win* win)
{
    auto& geo_update = win->geometry_update;
    auto const old_frame_geo = geo_update.frame;
    auto const area = get_maximizing_area(win);

    auto pos = QPoint(area.left(), old_frame_geo.y());
    pos = win->control->rules().checkPosition(pos);

    auto size = QSize(area.width(), old_frame_geo.height());
    size = win->control->adjusted_frame_size(size, size_mode::fixed_width);

    geometry_updates_blocker blocker(win);
    win->setFrameGeometry(QRect(pos, size));

    if (win->info) {
        auto net_state
            = flags(geo_update.max_mode & maximize_mode::vertical) ? NET::Max : NET::MaxHoriz;
        win->info->setState(net_state, NET::Max);
    }
    geo_update.max_mode |= maximize_mode::horizontal;
    update_no_border(win);
    set_restore_geometry(win, old_frame_geo);
}

template<typename Win>
void update_maximized_impl(Win* win, maximize_mode mode)
{
    assert(win->geometry_update.max_mode != mode);

    if (mode == maximize_mode::restore) {
        maximize_restore(win);
        return;
    }

    auto const old_frame_geo = win->geometry_update.frame;
    auto const old_mode = win->geometry_update.max_mode;

    if (flags(mode & maximize_mode::vertical)) {
        if (flags(old_mode & maximize_mode::horizontal) && !(mode & maximize_mode::horizontal)) {
            // We switch from horizontal or full maximization to vertical maximization.
            // Restore first to get the right horizontal position.
            maximize_restore(win);
        }
        maximize_vertically(win);
    }
    if (flags(mode & maximize_mode::horizontal)) {
        if (flags(old_mode & maximize_mode::vertical) && !(mode & maximize_mode::vertical)) {
            // We switch from vertical or full maximization to horizontal maximization.
            // Restore first to get the right vertical position.
            maximize_restore(win);
        }
        maximize_horizontally(win);
    }

    set_restore_geometry(win, old_frame_geo);
}

template<typename Win>
void respect_maximizing_aspect([[maybe_unused]] Win* win, [[maybe_unused]] maximize_mode& mode)
{
}

template<typename Win>
void update_maximized(Win* win, maximize_mode mode)
{
    if (!win->isResizable() || is_toolbar(win)) {
        return;
    }

    respect_maximizing_aspect(win, mode);
    mode = win->control->rules().checkMaximize(mode);

    geometry_updates_blocker blocker(win);
    auto const old_mode = win->geometry_update.max_mode;

    if (mode == old_mode) {
        // Just update the current size.
        auto const restore_geo = win->restore_geometries.maximize;
        if (flags(mode & maximize_mode::vertical)) {
            maximize_vertically(win);
        }
        if (flags(mode & maximize_mode::horizontal)) {
            maximize_horizontally(win);
        }
        set_restore_geometry(win, restore_geo);
        return;
    }

    if (old_mode != maximize_mode::restore && mode != maximize_mode::restore) {
        // We switch between different (partial) maximization modes. First restore the previous one.
        // The call will reset the restore geometry. So undo this change.
        auto const restore_geo = win->restore_geometries.maximize;
        update_maximized_impl(win, maximize_mode::restore);
        win->restore_geometries.maximize = restore_geo;
    }

    update_maximized_impl(win, mode);

    // TODO(romangg): This quicktiling logic is ill-fitted in update_maximized(..). We need to
    //                rework the relation between quicktiling and maximization in general.
    auto old_quicktiling = win->control->quicktiling();
    if (mode == maximize_mode::full) {
        win->control->set_quicktiling(quicktiles::maximize);
    } else {
        win->control->set_quicktiling(quicktiles::none);
    }
    if (old_quicktiling != win->control->quicktiling()) {
        // Send changed signal but ensure we do not override our frame geometry.
        auto const frame_geo = win->geometry_update.frame;
        Q_EMIT win->quicktiling_changed();
        win->setFrameGeometry(frame_geo);
    }
}

}
