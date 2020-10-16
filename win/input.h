/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_INPUT_H
#define KWIN_WIN_INPUT_H

#include "abstract_client.h"
#include "move.h"
#include "options.h"
#include "types.h"
#include "useractions.h"
#include "workspace.h"

#include <QMouseEvent>

namespace KWin::win
{

template<typename Win>
position mouse_position(Win* win)
{
    if (!win->isDecorated()) {
        return position::center;
    }

    switch (win->decoration()->sectionUnderMouse()) {
    case Qt::BottomLeftSection:
        return position::bottom_left;
    case Qt::BottomRightSection:
        return position::bottom_right;
    case Qt::BottomSection:
        return position::bottom;
    case Qt::LeftSection:
        return position::left;
    case Qt::RightSection:
        return position::right;
    case Qt::TopSection:
        return position::top;
    case Qt::TopLeftSection:
        return position::top_left;
    case Qt::TopRightSection:
        return position::top_right;
    default:
        return position::center;
    }
}

template<typename Win>
bool wants_tab_focus(Win* win)
{
    auto const suitable_type = win->isNormalWindow() || win->isDialog();
    return suitable_type && win->wantsInput();
}

template<typename Win>
bool is_most_recently_raised(Win* win)
{
    // The last toplevel in the unconstrained stacking order is the most recently raised one.
    auto last = workspace()->topClientOnDesktop(
        VirtualDesktopManager::self()->current(), -1, true, false);
    return last == win;
}

template<typename Win>
void auto_raise(Win* win)
{
    workspace()->raiseClient(win);
    win->cancelAutoRaise();
}

template<typename Win>
void key_press_event(Win* win, uint key_code)
{
    if (!is_move(win) && !is_resize(win)) {
        return;
    }

    auto is_control = key_code & Qt::CTRL;
    auto is_alt = key_code & Qt::ALT;

    key_code = key_code & ~Qt::KeyboardModifierMask;

    auto delta = is_control ? 1 : is_alt ? 32 : 8;
    auto pos = Cursor::pos();

    switch (key_code) {
    case Qt::Key_Left:
        pos.rx() -= delta;
        break;
    case Qt::Key_Right:
        pos.rx() += delta;
        break;
    case Qt::Key_Up:
        pos.ry() -= delta;
        break;
    case Qt::Key_Down:
        pos.ry() += delta;
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        win->setMoveResizePointerButtonDown(false);
        finish_move_resize(win, false);
        win->updateCursor();
        break;
    case Qt::Key_Escape:
        win->setMoveResizePointerButtonDown(false);
        finish_move_resize(win, true);
        win->updateCursor();
        break;
    default:
        return;
    }
    Cursor::setPos(pos);
}

template<typename Win>
bool perform_mouse_command(Win* win, Options::MouseCommand cmd, QPoint const& globalPos)
{
    bool replay = false;
    switch (cmd) {
    case Options::MouseRaise:
        workspace()->raiseClient(win);
        break;
    case Options::MouseLower: {
        workspace()->lowerClient(win);
        // Used to be activateNextClient(win), then topClientOnDesktop
        // since win is a mouseOp it's however safe to use the client under the mouse instead.
        if (win->isActive() && options->focusPolicyIsReasonable()) {
            auto next = workspace()->clientUnderMouse(win->screen());
            if (next && next != win)
                workspace()->requestFocus(next, false);
        }
        break;
    }
    case Options::MouseOperationsMenu:
        if (win->isActive() && options->isClickRaise()) {
            auto_raise(win);
        }
        workspace()->showWindowMenu(QRect(globalPos, globalPos), win);
        break;
    case Options::MouseToggleRaiseAndLower:
        workspace()->raiseOrLowerClient(win);
        break;
    case Options::MouseActivateAndRaise: {
        // For clickraise mode.
        replay = win->isActive();
        bool mustReplay = !win->rules()->checkAcceptFocus(win->acceptsFocus());

        if (mustReplay) {
            auto it = workspace()->stackingOrder().constEnd(),
                 begin = workspace()->stackingOrder().constBegin();
            while (mustReplay && --it != begin && *it != win) {
                auto c = qobject_cast<AbstractClient*>(*it);
                if (!c || (c->keepAbove() && !win->keepAbove())
                    || (win->keepBelow() && !c->keepBelow())) {
                    // Can never raise above "it".
                    continue;
                }
                mustReplay = !(c->isOnCurrentDesktop() && c->isOnCurrentActivity()
                               && c->frameGeometry().intersects(win->frameGeometry()));
            }
        }

        workspace()->takeActivity_win(win, activation::focus | activation::raise);
        screens()->setCurrent(globalPos);
        replay = replay || mustReplay;
        break;
    }
    case Options::MouseActivateAndLower:
        workspace()->requestFocus(win);
        workspace()->lowerClient(win);
        screens()->setCurrent(globalPos);
        replay = replay || !win->rules()->checkAcceptFocus(win->acceptsFocus());
        break;
    case Options::MouseActivate:
        // For clickraise mode.
        replay = win->isActive();
        workspace()->takeActivity_win(win, activation::focus);
        screens()->setCurrent(globalPos);
        replay = replay || !win->rules()->checkAcceptFocus(win->acceptsFocus());
        break;
    case Options::MouseActivateRaiseAndPassClick:
        workspace()->takeActivity_win(win, activation::focus | activation::raise);
        screens()->setCurrent(globalPos);
        replay = true;
        break;
    case Options::MouseActivateAndPassClick:
        workspace()->takeActivity_win(win, activation::focus);
        screens()->setCurrent(globalPos);
        replay = true;
        break;
    case Options::MouseMaximize:
        maximize(win, maximize_mode::full);
        break;
    case Options::MouseRestore:
        maximize(win, maximize_mode::restore);
        break;
    case Options::MouseMinimize:
        win->minimize();
        break;
    case Options::MouseAbove: {
        StackingUpdatesBlocker blocker(workspace());
        if (win->keepBelow()) {
            win->setKeepBelow(false);
        } else {
            win->setKeepAbove(true);
        }
        break;
    }
    case Options::MouseBelow: {
        StackingUpdatesBlocker blocker(workspace());
        if (win->keepAbove()) {
            win->setKeepAbove(false);
        } else {
            win->setKeepBelow(true);
        }
        break;
    }
    case Options::MousePreviousDesktop:
        workspace()->windowToPreviousDesktop(win);
        break;
    case Options::MouseNextDesktop:
        workspace()->windowToNextDesktop(win);
        break;
    case Options::MouseOpacityMore:
        // No point in changing the opacity of the desktop.
        if (!win->isDesktop()) {
            win->setOpacity(qMin(win->opacity() + 0.1, 1.0));
        }
        break;
    case Options::MouseOpacityLess:
        if (!win->isDesktop()) {
            win->setOpacity(qMax(win->opacity() - 0.1, 0.1));
        }
        break;
    case Options::MouseClose:
        win->closeWindow();
        break;
    case Options::MouseActivateRaiseAndMove:
    case Options::MouseActivateRaiseAndUnrestrictedMove:
        workspace()->raiseClient(win);
        workspace()->requestFocus(win);
        screens()->setCurrent(globalPos);
        // Fallthrough
    case Options::MouseMove:
    case Options::MouseUnrestrictedMove: {
        if (!win->isMovableAcrossScreens()) {
            break;
        }
        if (win->isMoveResize()) {
            finish_move_resize(win, false);
        }
        win->setMoveResizePointerMode_win(position::center);
        win->setMoveResizePointerButtonDown(true);

        // map from global
        win->setMoveOffset(QPoint(globalPos.x() - win->x(), globalPos.y() - win->y()));

        win->setInvertedMoveOffset(win->rect().bottomRight() - win->moveOffset());
        win->setUnrestrictedMoveResize((cmd == Options::MouseActivateRaiseAndUnrestrictedMove
                                        || cmd == Options::MouseUnrestrictedMove));
        if (!start_move_resize(win)) {
            win->setMoveResizePointerButtonDown(false);
        }
        win->updateCursor();
        break;
    }
    case Options::MouseResize:
    case Options::MouseUnrestrictedResize: {
        if (!win->isResizable() || win->isShade()) {
            break;
        }
        if (win->isMoveResize()) {
            finish_move_resize(win, false);
        }
        win->setMoveResizePointerButtonDown(true);

        // Map from global
        auto const moveOffset = QPoint(globalPos.x() - win->x(), globalPos.y() - win->y());
        win->setMoveOffset(moveOffset);

        auto x = moveOffset.x();
        auto y = moveOffset.y();
        auto left = x < win->width() / 3;
        auto right = x >= 2 * win->width() / 3;
        auto top = y < win->height() / 3;
        auto bot = y >= 2 * win->height() / 3;

        position mode;
        if (top) {
            mode = left ? position::top_left : (right ? position::top_right : position::top);
        } else if (bot) {
            mode = left ? position::bottom_left
                        : (right ? position::bottom_right : position::bottom);
        } else {
            mode = (x < win->width() / 2) ? position::left : position::right;
        }
        win->setMoveResizePointerMode_win(mode);
        win->setInvertedMoveOffset(win->rect().bottomRight() - moveOffset);
        win->setUnrestrictedMoveResize((cmd == Options::MouseUnrestrictedResize));
        if (!start_move_resize(win)) {
            win->setMoveResizePointerButtonDown(false);
        }
        win->updateCursor();
        break;
    }

    case Options::MouseNothing:
    default:
        replay = true;
        break;
    }
    return replay;
}

template<typename Win>
void enter_event(Win* win, const QPoint& globalPos)
{
    // TODO: shade hover
    if (options->focusPolicy() == Options::ClickToFocus
        || workspace()->userActionsMenu()->isShown()) {
        return;
    }

    if (options->isAutoRaise() && !win->isDesktop() && !win->isDock()
        && workspace()->focusChangeEnabled() && globalPos != workspace()->focusMousePosition()
        && workspace()->topClientOnDesktop(VirtualDesktopManager::self()->current(),
                                           options->isSeparateScreenFocus() ? win->screen() : -1)
            != win) {
        win->startAutoRaise();
    }

    if (win->isDesktop() || win->isDock()) {
        return;
    }

    // For FocusFollowsMouse, change focus only if the mouse has actually been moved, not if the
    // focus change came because of window changes (e.g. closing a window) - #92290
    if (options->focusPolicy() != Options::FocusFollowsMouse
        || globalPos != workspace()->focusMousePosition()) {
        workspace()->requestDelayFocus(win);
    }
}

template<typename Win>
void leave_event(Win* win)
{
    win->cancelAutoRaise();
    workspace()->cancelDelayFocus();
    // TODO: shade hover
    // TODO: send hover leave to deco
    // TODO: handle Options::FocusStrictlyUnderMouse
}

template<typename Win>
bool titlebar_positioned_under_mouse(Win* win)
{
    if (!win->isDecorated()) {
        return false;
    }

    auto const section = win->decoration()->sectionUnderMouse();
    if (section == Qt::TitleBarArea) {
        return true;
    }

    // Check other sections based on titlebarPosition.
    switch (win->titlebarPosition_win()) {
    case position::top:
        return (section == Qt::TopLeftSection || section == Qt::TopSection
                || section == Qt::TopRightSection);
    case position::left:
        return (section == Qt::TopLeftSection || section == Qt::LeftSection
                || section == Qt::BottomLeftSection);
    case position::right:
        return (section == Qt::BottomRightSection || section == Qt::RightSection
                || section == Qt::TopRightSection);
    case position::bottom:
        return (section == Qt::BottomLeftSection || section == Qt::BottomSection
                || section == Qt::BottomRightSection);
    default:
        // Nothing
        return false;
    }
}

template<typename Win>
void process_decoration_move(Win* win, QPoint const& localPos, QPoint const& globalPos)
{
    if (win->isMoveResizePointerButtonDown()) {
        move_resize(win, localPos.x(), localPos.y(), globalPos.x(), globalPos.y());
        return;
    }

    // TODO: handle modifiers
    auto newmode = mouse_position(win);
    if (newmode != win->moveResizePointerMode_win()) {
        win->setMoveResizePointerMode_win(newmode);
        win->updateCursor();
    }
}

template<typename Win>
void process_decoration_button_release(Win* win, QMouseEvent* event)
{
    if (win->isDecorated()) {
        if (event->isAccepted() || !titlebar_positioned_under_mouse(win)) {
            // Click was for the deco and shall not init a doubleclick.
            win->invalidateDecorationDoubleClickTimer();
        }
    }

    if (event->buttons() == Qt::NoButton) {
        win->setMoveResizePointerButtonDown(false);
        win->stopDelayedMoveResize();
        if (win->isMoveResize()) {
            finish_move_resize(win, false);
            win->setMoveResizePointerMode(win->mousePosition());
        }
        win->updateCursor();
    }
}

}

#endif
