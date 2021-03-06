/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

//#define QT_CLEAN_NAMESPACE

#include "group.h"
#include "workspace.h"
#include "effects.h"

#include "win/control.h"
#include "win/x11/window.h"

#include <KWindowSystem>
#include <QDebug>

namespace KWin
{

//********************************************
// Group
//********************************************

Group::Group(xcb_window_t leader_P)
    :   leader_client(nullptr),
        leader_wid(leader_P),
        leader_info(nullptr),
        user_time(-1U),
        refcount(0)
{
    if (leader_P != XCB_WINDOW_NONE) {
        leader_client = workspace()->findClient(win::x11::predicate_match::window, leader_P);
        leader_info = new NETWinInfo(connection(), leader_P, rootWindow(),
                                     NET::Properties(), NET::WM2StartupId);
    }
    effect_group = new EffectWindowGroupImpl(this);
    workspace()->addGroup(this);
}

Group::~Group()
{
    delete leader_info;
    delete effect_group;
}

QIcon Group::icon() const
{
    if (leader_client != nullptr)
        return leader_client->control->icon();
    else if (leader_wid != XCB_WINDOW_NONE) {
        QIcon ic;
        NETWinInfo info(connection(), leader_wid, rootWindow(), NET::WMIcon, NET::WM2IconPixmap);
        auto readIcon = [&ic, &info, this](int size, bool scale = true) {
            const QPixmap pix = KWindowSystem::icon(leader_wid, size, size, scale, KWindowSystem::NETWM | KWindowSystem::WMHints, &info);
            if (!pix.isNull()) {
                ic.addPixmap(pix);
            }
        };
        readIcon(16);
        readIcon(32);
        readIcon(48, false);
        readIcon(64, false);
        readIcon(128, false);
        return ic;
    }
    return QIcon();
}

void Group::addMember(win::x11::window *member_P)
{
    _members.push_back(member_P);
//    qDebug() << "GROUPADD:" << this << ":" << member_P;
//    qDebug() << kBacktrace();
}

void Group::removeMember(win::x11::window *member_P)
{
//    qDebug() << "GROUPREMOVE:" << this << ":" << member_P;
//    qDebug() << kBacktrace();
    assert(std::find(_members.cbegin(), _members.cend(), member_P) != _members.cend());
    remove_all(_members, member_P);
// there are cases when automatic deleting of groups must be delayed,
// e.g. when removing a member and doing some operation on the possibly
// other members of the group (which would be however deleted already
// if there were no other members)
    if (refcount == 0 && _members.empty()) {
        workspace()->removeGroup(this);
        delete this;
    }
}

void Group::ref()
{
    ++refcount;
}

void Group::deref()
{
    if (--refcount == 0 && _members.empty()) {
        workspace()->removeGroup(this);
        delete this;
    }
}

void Group::gotLeader(win::x11::window *leader_P)
{
    assert(leader_P->xcb_window() == leader_wid);
    leader_client = leader_P;
}

void Group::lostLeader()
{
    assert(std::find(_members.cbegin(), _members.cend(), leader_client) == _members.cend());
    leader_client = nullptr;
    if (_members.empty()) {
        workspace()->removeGroup(this);
        delete this;
    }
}

} // namespace
