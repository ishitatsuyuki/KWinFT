/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2018 Roman Gilg <subdiff@gmail.com>

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
#ifndef KWIN_XWL_DATABRIDGE
#define KWIN_XWL_DATABRIDGE

#include <kwin_export.h>

#include <QObject>
#include <QPoint>

#include <xcb/xcb.h>

struct xcb_xfixes_selection_notify_event_t;

namespace Wrapland
{
namespace Client
{
class DataDevice;
}
namespace Server
{
class DataDevice;
class Surface;
}
}

namespace KWin
{
class Toplevel;

namespace Xwl
{
class Xwayland;
class Clipboard;
class Dnd;
enum class DragEventReply;

/**
 * Interface class for all data sharing in the context of X selections
 * and Wayland's internal mechanism.
 *
 * Exists only once per Xwayland session.
 */
class KWIN_EXPORT DataBridge : public QObject
{
    Q_OBJECT

public:
    static DataBridge *self();

    explicit DataBridge(QObject *parent = nullptr);
    ~DataBridge() override;

    bool filterEvent(xcb_generic_event_t *event);
    DragEventReply dragMoveFilter(Toplevel *target, const QPoint &pos);

    Wrapland::Client::DataDevice *dataDevice() const
    {
        return m_dataDevice;
    }
    Wrapland::Server::DataDevice *dataDeviceIface() const
    {
        return m_dataDeviceInterface;
    }
    Dnd *dnd() const
    {
        return m_dnd;
    }

private:
    void init();

    bool handleXfixesNotify(xcb_xfixes_selection_notify_event_t *event);

    Clipboard *m_clipboard = nullptr;
    Dnd *m_dnd = nullptr;

    /* Internal data device interface */
    Wrapland::Client::DataDevice *m_dataDevice = nullptr;
    Wrapland::Server::DataDevice *m_dataDeviceInterface = nullptr;

    Q_DISABLE_COPY(DataBridge)
};

} // namespace Xwl
} // namespace KWin

#endif
