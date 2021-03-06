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
#include "kwin_wayland_test.h"
#include "screenlockerwatcher.h"
#include "wayland_server.h"

#include "win/wayland/window.h"

#include <Wrapland/Client/appmenu.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/idleinhibit.h>
#include <Wrapland/Client/layer_shell_v1.h>
#include <Wrapland/Client/output.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/plasmawindowmanagement.h>
#include <Wrapland/Client/pointerconstraints.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shadow.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/subcompositor.h>
#include <Wrapland/Client/subsurface.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Server/display.h>

#include <KScreenLocker/KsldApp>

#include <QThread>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace Clt = Wrapland::Client;

namespace KWin
{
namespace Test
{

static struct {
    Clt::ConnectionThread* connection = nullptr;
    Clt::EventQueue* queue = nullptr;
    Clt::Compositor* compositor = nullptr;
    Clt::LayerShellV1* layer_shell{nullptr};
    Clt::SubCompositor* subCompositor = nullptr;
    Clt::ShadowManager* shadowManager = nullptr;
    Clt::XdgShell* xdg_shell = {nullptr};
    Clt::ShmPool* shm = nullptr;
    Clt::Seat* seat = nullptr;
    Clt::PlasmaShell* plasmaShell = nullptr;
    Clt::PlasmaWindowManagement* windowManagement = nullptr;
    Clt::PointerConstraints* pointerConstraints = nullptr;
    Clt::Registry* registry = nullptr;
    QThread* thread = nullptr;
    QVector<Clt::Output*> outputs;
    Clt::IdleInhibitManager* idleInhibit = nullptr;
    Clt::AppMenuManager* appMenu = nullptr;
    Clt::XdgDecorationManager* xdgDecoration = nullptr;
} s_waylandConnection;

void setupWaylandConnection(AdditionalWaylandInterfaces flags)
{
    QVERIFY(!s_waylandConnection.connection);

    int sx[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) >= 0);

    KWin::waylandServer()->display()->createClient(sx[0]);

    // Setup connection.
    s_waylandConnection.connection = new Clt::ConnectionThread;

    QSignalSpy connectedSpy(s_waylandConnection.connection,
                            &Clt::ConnectionThread::establishedChanged);
    QVERIFY(connectedSpy.isValid());

    s_waylandConnection.connection->setSocketFd(sx[1]);

    s_waylandConnection.thread = new QThread(kwinApp());
    s_waylandConnection.connection->moveToThread(s_waylandConnection.thread);
    s_waylandConnection.thread->start();

    s_waylandConnection.connection->establishConnection();
    QVERIFY(connectedSpy.count() || connectedSpy.wait());
    QCOMPARE(connectedSpy.count(), 1);
    QVERIFY(s_waylandConnection.connection->established());

    s_waylandConnection.queue = new Clt::EventQueue;
    s_waylandConnection.queue->setup(s_waylandConnection.connection);
    QVERIFY(s_waylandConnection.queue->isValid());

    auto registry = new Clt::Registry;
    s_waylandConnection.registry = registry;
    registry->setEventQueue(s_waylandConnection.queue);

    QObject::connect(registry, &Clt::Registry::outputAnnounced, [=](quint32 name, quint32 version) {
        auto output = registry->createOutput(name, version, s_waylandConnection.registry);
        s_waylandConnection.outputs << output;
        QObject::connect(output, &Clt::Output::removed, [=]() {
            output->deleteLater();
            s_waylandConnection.outputs.removeOne(output);
        });
    });

    QSignalSpy allAnnounced(registry, &Clt::Registry::interfacesAnnounced);
    QVERIFY(allAnnounced.isValid());

    registry->create(s_waylandConnection.connection);
    QVERIFY(registry->isValid());

    registry->setup();
    QVERIFY(allAnnounced.count() || allAnnounced.wait());
    QCOMPARE(allAnnounced.count(), 1);

    s_waylandConnection.compositor = registry->createCompositor(
        registry->interface(Clt::Registry::Interface::Compositor).name,
        registry->interface(Clt::Registry::Interface::Compositor).version);
    QVERIFY(s_waylandConnection.compositor->isValid());

    s_waylandConnection.subCompositor = registry->createSubCompositor(
        registry->interface(Clt::Registry::Interface::SubCompositor).name,
        registry->interface(Clt::Registry::Interface::SubCompositor).version);
    QVERIFY(s_waylandConnection.subCompositor->isValid());

    s_waylandConnection.shm
        = registry->createShmPool(registry->interface(Clt::Registry::Interface::Shm).name,
                                  registry->interface(Clt::Registry::Interface::Shm).version);
    QVERIFY(s_waylandConnection.shm->isValid());

    s_waylandConnection.xdg_shell
        = registry->createXdgShell(registry->interface(Clt::Registry::Interface::XdgShell).name,
                                   registry->interface(Clt::Registry::Interface::XdgShell).version);
    QVERIFY(s_waylandConnection.xdg_shell->isValid());

    s_waylandConnection.layer_shell = registry->createLayerShellV1(
        registry->interface(Clt::Registry::Interface::LayerShellV1).name,
        registry->interface(Clt::Registry::Interface::LayerShellV1).version);
    QVERIFY(s_waylandConnection.layer_shell->isValid());

    if (flags.testFlag(AdditionalWaylandInterface::Seat)) {
        s_waylandConnection.seat
            = registry->createSeat(registry->interface(Clt::Registry::Interface::Seat).name,
                                   registry->interface(Clt::Registry::Interface::Seat).version);
        QVERIFY(s_waylandConnection.seat->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::ShadowManager)) {
        s_waylandConnection.shadowManager = registry->createShadowManager(
            registry->interface(Clt::Registry::Interface::Shadow).name,
            registry->interface(Clt::Registry::Interface::Shadow).version);
        QVERIFY(s_waylandConnection.shadowManager->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::PlasmaShell)) {
        s_waylandConnection.plasmaShell = registry->createPlasmaShell(
            registry->interface(Clt::Registry::Interface::PlasmaShell).name,
            registry->interface(Clt::Registry::Interface::PlasmaShell).version);
        QVERIFY(s_waylandConnection.plasmaShell->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::WindowManagement)) {
        s_waylandConnection.windowManagement = registry->createPlasmaWindowManagement(
            registry->interface(Clt::Registry::Interface::PlasmaWindowManagement).name,
            registry->interface(Clt::Registry::Interface::PlasmaWindowManagement).version);
        QVERIFY(s_waylandConnection.windowManagement->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::PointerConstraints)) {
        s_waylandConnection.pointerConstraints = registry->createPointerConstraints(
            registry->interface(Clt::Registry::Interface::PointerConstraintsUnstableV1).name,
            registry->interface(Clt::Registry::Interface::PointerConstraintsUnstableV1).version);
        QVERIFY(s_waylandConnection.pointerConstraints->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::IdleInhibition)) {
        s_waylandConnection.idleInhibit = registry->createIdleInhibitManager(
            registry->interface(Clt::Registry::Interface::IdleInhibitManagerUnstableV1).name,
            registry->interface(Clt::Registry::Interface::IdleInhibitManagerUnstableV1).version);
        QVERIFY(s_waylandConnection.idleInhibit->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::AppMenu)) {
        s_waylandConnection.appMenu = registry->createAppMenuManager(
            registry->interface(Clt::Registry::Interface::AppMenu).name,
            registry->interface(Clt::Registry::Interface::AppMenu).version);
        QVERIFY(s_waylandConnection.appMenu->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::XdgDecoration)) {
        s_waylandConnection.xdgDecoration = registry->createXdgDecorationManager(
            registry->interface(Clt::Registry::Interface::XdgDecorationUnstableV1).name,
            registry->interface(Clt::Registry::Interface::XdgDecorationUnstableV1).version);
        QVERIFY(s_waylandConnection.xdgDecoration->isValid());
    }
}

void destroyWaylandConnection()
{
    for (auto& output : s_waylandConnection.outputs) {
        delete output;
    }
    s_waylandConnection.outputs.clear();

    delete s_waylandConnection.compositor;
    s_waylandConnection.compositor = nullptr;
    delete s_waylandConnection.subCompositor;
    s_waylandConnection.subCompositor = nullptr;
    delete s_waylandConnection.windowManagement;
    s_waylandConnection.windowManagement = nullptr;
    delete s_waylandConnection.layer_shell;
    s_waylandConnection.layer_shell = nullptr;
    delete s_waylandConnection.plasmaShell;
    s_waylandConnection.plasmaShell = nullptr;
    delete s_waylandConnection.seat;
    s_waylandConnection.seat = nullptr;
    delete s_waylandConnection.pointerConstraints;
    s_waylandConnection.pointerConstraints = nullptr;
    delete s_waylandConnection.xdg_shell;
    s_waylandConnection.xdg_shell = nullptr;
    delete s_waylandConnection.shadowManager;
    s_waylandConnection.shadowManager = nullptr;
    delete s_waylandConnection.idleInhibit;
    s_waylandConnection.idleInhibit = nullptr;
    delete s_waylandConnection.shm;
    s_waylandConnection.shm = nullptr;
    delete s_waylandConnection.appMenu;
    s_waylandConnection.appMenu = nullptr;
    delete s_waylandConnection.xdgDecoration;
    s_waylandConnection.xdgDecoration = nullptr;
    delete s_waylandConnection.registry;
    s_waylandConnection.registry = nullptr;
    delete s_waylandConnection.queue;
    s_waylandConnection.queue = nullptr;

    if (s_waylandConnection.thread) {
        QSignalSpy spy(s_waylandConnection.connection, &QObject::destroyed);
        QVERIFY(spy.isValid());

        s_waylandConnection.connection->deleteLater();
        QVERIFY(!spy.isEmpty() || spy.wait());
        QCOMPARE(spy.count(), 1);

        s_waylandConnection.thread->quit();
        s_waylandConnection.thread->wait();
        delete s_waylandConnection.thread;
        s_waylandConnection.thread = nullptr;
        s_waylandConnection.connection = nullptr;
    }
}

Clt::ConnectionThread* waylandConnection()
{
    return s_waylandConnection.connection;
}

Clt::Compositor* waylandCompositor()
{
    return s_waylandConnection.compositor;
}

Clt::SubCompositor* waylandSubCompositor()
{
    return s_waylandConnection.subCompositor;
}

Clt::ShadowManager* waylandShadowManager()
{
    return s_waylandConnection.shadowManager;
}

Clt::ShmPool* waylandShmPool()
{
    return s_waylandConnection.shm;
}

Clt::Seat* waylandSeat()
{
    return s_waylandConnection.seat;
}

Clt::PlasmaShell* waylandPlasmaShell()
{
    return s_waylandConnection.plasmaShell;
}

Clt::PlasmaWindowManagement* waylandWindowManagement()
{
    return s_waylandConnection.windowManagement;
}

Clt::PointerConstraints* waylandPointerConstraints()
{
    return s_waylandConnection.pointerConstraints;
}

Clt::IdleInhibitManager* waylandIdleInhibitManager()
{
    return s_waylandConnection.idleInhibit;
}

Clt::AppMenuManager* waylandAppMenuManager()
{
    return s_waylandConnection.appMenu;
}

Clt::XdgDecorationManager* xdgDecorationManager()
{
    return s_waylandConnection.xdgDecoration;
}

Clt::LayerShellV1* layer_shell()
{
    return s_waylandConnection.layer_shell;
}

QVector<Clt::Output*> const& outputs()
{
    return s_waylandConnection.outputs;
}

bool waitForWaylandPointer()
{
    if (!s_waylandConnection.seat) {
        return false;
    }
    QSignalSpy hasPointerSpy(s_waylandConnection.seat, &Clt::Seat::hasPointerChanged);
    if (!hasPointerSpy.isValid()) {
        return false;
    }
    return hasPointerSpy.wait();
}

bool waitForWaylandTouch()
{
    if (!s_waylandConnection.seat) {
        return false;
    }
    QSignalSpy hasTouchSpy(s_waylandConnection.seat, &Clt::Seat::hasTouchChanged);
    if (!hasTouchSpy.isValid()) {
        return false;
    }
    return hasTouchSpy.wait();
}

bool waitForWaylandKeyboard()
{
    if (!s_waylandConnection.seat) {
        return false;
    }
    QSignalSpy hasKeyboardSpy(s_waylandConnection.seat, &Clt::Seat::hasKeyboardChanged);
    if (!hasKeyboardSpy.isValid()) {
        return false;
    }
    return hasKeyboardSpy.wait();
}

void render(Clt::Surface* surface,
            const QSize& size,
            const QColor& color,
            const QImage::Format& format)
{
    QImage img(size, format);
    img.fill(color);
    render(surface, img);
}

void render(Clt::Surface* surface, const QImage& img)
{
    surface->attachBuffer(s_waylandConnection.shm->createBuffer(img));
    surface->damage(QRect(QPoint(0, 0), img.size()));
    surface->commit(Clt::Surface::CommitFlag::None);
}

win::wayland::window* waitForWaylandWindowShown(int timeout)
{
    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::window_added);
    if (!clientAddedSpy.isValid()) {
        return nullptr;
    }
    if (!clientAddedSpy.wait(timeout)) {
        return nullptr;
    }
    return clientAddedSpy.first().first().value<win::wayland::window*>();
}

win::wayland::window* renderAndWaitForShown(Clt::Surface* surface,
                                            const QSize& size,
                                            const QColor& color,
                                            const QImage::Format& format,
                                            int timeout)
{
    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::window_added);
    if (!clientAddedSpy.isValid()) {
        return nullptr;
    }
    render(surface, size, color, format);
    flushWaylandConnection();
    if (!clientAddedSpy.wait(timeout)) {
        return nullptr;
    }
    return clientAddedSpy.first().first().value<win::wayland::window*>();
}

void flushWaylandConnection()
{
    if (s_waylandConnection.connection) {
        s_waylandConnection.connection->flush();
    }
}

Clt::Surface* createSurface(QObject* parent)
{
    if (!s_waylandConnection.compositor) {
        return nullptr;
    }
    auto s = s_waylandConnection.compositor->createSurface(parent);
    if (!s->isValid()) {
        delete s;
        return nullptr;
    }
    return s;
}

Clt::SubSurface*
createSubSurface(Clt::Surface* surface, Clt::Surface* parentSurface, QObject* parent)
{
    if (!s_waylandConnection.subCompositor) {
        return nullptr;
    }
    auto s = s_waylandConnection.subCompositor->createSubSurface(surface, parentSurface, parent);
    if (!s->isValid()) {
        delete s;
        return nullptr;
    }
    return s;
}

Clt::XdgShellToplevel*
create_xdg_shell_toplevel(Clt::Surface* surface, QObject* parent, CreationSetup creationSetup)
{
    if (!s_waylandConnection.xdg_shell) {
        return nullptr;
    }
    auto s = s_waylandConnection.xdg_shell->create_toplevel(surface, parent);
    if (!s->isValid()) {
        delete s;
        return nullptr;
    }
    if (creationSetup == CreationSetup::CreateAndConfigure) {
        init_xdg_shell_toplevel(surface, s);
    }
    return s;
}

Clt::XdgShellPopup* create_xdg_shell_popup(Clt::Surface* surface,
                                           Clt::XdgShellToplevel* parentSurface,
                                           const Clt::XdgPositioner& positioner,
                                           QObject* parent,
                                           CreationSetup creationSetup)
{
    if (!s_waylandConnection.xdg_shell) {
        return nullptr;
    }
    auto s
        = s_waylandConnection.xdg_shell->create_popup(surface, parentSurface, positioner, parent);
    if (!s->isValid()) {
        delete s;
        return nullptr;
    }
    if (creationSetup == CreationSetup::CreateAndConfigure) {
        init_xdg_shell_popup(surface, s);
    }
    return s;
}

void init_xdg_shell_toplevel(Clt::Surface* surface, Clt::XdgShellToplevel* shellSurface)
{
    // wait for configure
    QSignalSpy configureRequestedSpy(shellSurface, &Clt::XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Clt::Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    shellSurface->ackConfigure(configureRequestedSpy.last()[2].toInt());
}

void init_xdg_shell_popup(Clt::Surface* surface, Clt::XdgShellPopup* shellPopup)
{
    // wait for configure
    QSignalSpy configureRequestedSpy(shellPopup, &Clt::XdgShellPopup::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Clt::Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    shellPopup->ackConfigure(configureRequestedSpy.last()[1].toInt());
}

bool waitForWindowDestroyed(Toplevel* window)
{
    QSignalSpy destroyedSpy(window, &QObject::destroyed);
    if (!destroyedSpy.isValid()) {
        return false;
    }
    return destroyedSpy.wait();
}

void lockScreen()
{
    QVERIFY(!waylandServer()->isScreenLocked());

    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                   &ScreenLocker::KSldApp::lockStateChanged);
    QVERIFY(lockStateChangedSpy.isValid());
    QSignalSpy lockWatcherSpy(ScreenLockerWatcher::self(), &ScreenLockerWatcher::locked);
    QVERIFY(lockWatcherSpy.isValid());

    ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);
    QCOMPARE(lockStateChangedSpy.count(), 1);

    QVERIFY(waylandServer()->isScreenLocked());
    QVERIFY(lockWatcherSpy.wait());
    QCOMPARE(lockWatcherSpy.count(), 1);
    QCOMPARE(lockStateChangedSpy.count(), 2);

    QVERIFY(ScreenLockerWatcher::self()->isLocked());
}

void unlockScreen()
{
    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                   &ScreenLocker::KSldApp::lockStateChanged);
    QVERIFY(lockStateChangedSpy.isValid());
    QSignalSpy lockWatcherSpy(ScreenLockerWatcher::self(), &ScreenLockerWatcher::locked);
    QVERIFY(lockWatcherSpy.isValid());

    auto const children = ScreenLocker::KSldApp::self()->children();
    auto logind_integration_found{false};
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (qstrcmp((*it)->metaObject()->className(), "LogindIntegration") == 0) {
            logind_integration_found = true;

            // KScreenLocker does not handle unlock requests via logind reliable as it sends a
            // SIGTERM signal to the lock process which sometimes under high system load is not
            // received and handled by the process.
            // It is unclear why the signal is never received but we can repeat sending the signal
            // mutliple times (here 10) assuming that after few tries one of them will be received.
            int unlock_tries = 0;
            while (unlock_tries++ < 10) {
                QMetaObject::invokeMethod(*it, "requestUnlock");
                lockWatcherSpy.wait(1000);
                if (lockWatcherSpy.count()) {
                    break;
                }
            }

            break;
        }
    }
    QVERIFY(logind_integration_found);
    QCOMPARE(lockWatcherSpy.count(), 1);
    QCOMPARE(lockStateChangedSpy.count(), 1);

    QVERIFY(!waylandServer()->isScreenLocked());

    QVERIFY(!ScreenLockerWatcher::self()->isLocked());
}

}
}
