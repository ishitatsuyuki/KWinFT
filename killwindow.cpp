/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

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
#include "killwindow.h"
#include "main.h"
#include "platform.h"
#include "osd.h"
#include "toplevel.h"

#include <KLocalizedString>

namespace KWin
{

KillWindow::KillWindow()
{
}

KillWindow::~KillWindow()
{
}

void KillWindow::start()
{
    OSD::show(i18n("Select window to force close with left click or enter.\nEscape or right click to cancel."),
              QStringLiteral("window-close"));
    kwinApp()->platform()->startInteractiveWindowSelection(
        [] (Toplevel* window) {
            OSD::hide();
            if (!window) {
                return;
            }
            if (window->control) {
                window->killWindow();
            } else if (window->xcb_window()) {
                xcb_kill_client(connection(), window->xcb_window());
            }
        }, QByteArrayLiteral("pirate")
    );
}

} // namespace
