/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "abstract_client.h"

namespace KWin
{

AbstractClient::AbstractClient()
    : Toplevel()
{
}

AbstractClient::~AbstractClient() = default;

void AbstractClient::updateMouseGrab()
{
}

bool AbstractClient::belongToSameApplication(const AbstractClient *c1, const AbstractClient *c2, bool active_hack)
{
    return c1->belongsToSameApplication(c2, active_hack);
}

bool AbstractClient::isTransient() const
{
    return false;
}

TabGroup *AbstractClient::tabGroup() const
{
    return nullptr;
}

bool AbstractClient::untab(const QRect &toGeometry, bool clientRemoved)
{
    Q_UNUSED(toGeometry)
    Q_UNUSED(clientRemoved)
    return false;
}

bool AbstractClient::isCurrentTab() const
{
    return true;
}

void AbstractClient::growHorizontal()
{
}

void AbstractClient::growVertical()
{
}

void AbstractClient::shrinkHorizontal()
{
}

void AbstractClient::shrinkVertical()
{
}

}