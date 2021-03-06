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
#ifndef POLLER_H
#define POLLER_H

#include <KIdleTime/private/abstractsystempoller.h>

#include <QHash>

namespace Wrapland
{
namespace Client
{
class Seat;
class Idle;
class IdleTimeout;
}
}

class KWinIdleTimePoller : public AbstractSystemPoller
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID AbstractSystemPoller_iid FILE "kwin.json")
    Q_INTERFACES(AbstractSystemPoller)

public:
    KWinIdleTimePoller(QObject *parent = nullptr);
    ~KWinIdleTimePoller() override;

    bool isAvailable() override;
    bool setUpPoller() override;
    void unloadPoller() override;

public Q_SLOTS:
    void addTimeout(int nextTimeout) override;
    void removeTimeout(int nextTimeout) override;
    QList<int> timeouts() const override;
    int forcePollRequest() override;
    void catchIdleEvent() override;
    void stopCatchingIdleEvents() override;
    void simulateUserActivity() override;

private:
    Wrapland::Client::Seat *m_seat = nullptr;
    Wrapland::Client::Idle *m_idle = nullptr;
    Wrapland::Client::IdleTimeout *m_catchResumeTimeout = nullptr;
    QHash<int, Wrapland::Client::IdleTimeout*> m_timeouts;
};

#endif
