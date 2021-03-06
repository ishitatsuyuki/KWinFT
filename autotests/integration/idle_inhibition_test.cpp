/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>

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
#include "platform.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/screen.h"
#include "win/stacking.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/idleinhibit.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/kde_idle.h>

using namespace KWin;
using namespace Wrapland::Client;
using Wrapland::Server::KdeIdle;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_idle_inhbition_test-0");

class TestIdleInhibition : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testInhibit();
    void testDontInhibitWhenNotOnCurrentDesktop();
    void testDontInhibitWhenMinimized();
    void testDontInhibitWhenUnmapped();
    void testDontInhibitWhenLeftCurrentDesktop();
};

void TestIdleInhibition::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    waylandServer()->initWorkspace();
}

void TestIdleInhibition::init()
{
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::IdleInhibition);

}

void TestIdleInhibition::cleanup()
{
    Test::destroyWaylandConnection();

    VirtualDesktopManager::self()->setCount(1);
    QCOMPARE(VirtualDesktopManager::self()->count(), 1u);
}

void TestIdleInhibition::testInhibit()
{
    auto idle = waylandServer()->display()->findChild<KdeIdle*>();
    QVERIFY(idle);
    QVERIFY(!idle->isInhibited());
    QSignalSpy inhibitedSpy(idle, &KdeIdle::inhibitedChanged);
    QVERIFY(inhibitedSpy.isValid());

    // now create window
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));

    // now create inhibition on window
    QScopedPointer<IdleInhibitor> inhibitor(Test::waylandIdleInhibitManager()->createInhibitor(surface.data()));
    QVERIFY(inhibitor->isValid());

    // render the client
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // this should inhibit our server object
    QVERIFY(idle->isInhibited());

    // deleting the object should uninhibit again
    inhibitor.reset();
    QVERIFY(inhibitedSpy.wait());
    QVERIFY(!idle->isInhibited());

    // inhibit again and destroy window
    Test::waylandIdleInhibitManager()->createInhibitor(surface.data(), surface.data());
    QVERIFY(inhibitedSpy.wait());
    QVERIFY(idle->isInhibited());

    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(c));
    QTRY_VERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 4);
}

void TestIdleInhibition::testDontInhibitWhenNotOnCurrentDesktop()
{
    // This test verifies that the idle inhibitor object is not honored when
    // the associated surface is not on the current virtual desktop.

    VirtualDesktopManager::self()->setCount(2);
    QCOMPARE(VirtualDesktopManager::self()->count(), 2u);

    // Get reference to the idle interface.
    auto idle = waylandServer()->display()->findChild<KdeIdle *>();
    QVERIFY(idle);
    QVERIFY(!idle->isInhibited());
    QSignalSpy inhibitedSpy(idle, &KdeIdle::inhibitedChanged);
    QVERIFY(inhibitedSpy.isValid());

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // Create the inhibitor object.
    QScopedPointer<IdleInhibitor> inhibitor(Test::waylandIdleInhibitManager()->createInhibitor(surface.data()));
    QVERIFY(inhibitor->isValid());

    // Render the client.
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // The test client should be only on the first virtual desktop.
    QCOMPARE(c->desktops().count(), 1);
    QCOMPARE(c->desktops().first(), VirtualDesktopManager::self()->desktops().first());

    // This should inhibit our server object.
    QVERIFY(idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 1);

    // Switch to the second virtual desktop.
    VirtualDesktopManager::self()->setCurrent(2);

    // The surface is no longer visible, so the compositor don't have to honor the
    // idle inhibitor object.
    QVERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 2);

    // Switch back to the first virtual desktop.
    VirtualDesktopManager::self()->setCurrent(1);

    // The test client became visible again, so the compositor has to honor the idle
    // inhibitor object back again.
    QVERIFY(idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 3);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(c));
    QTRY_VERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 4);
}

void TestIdleInhibition::testDontInhibitWhenMinimized()
{
    // This test verifies that the idle inhibitor object is not honored when the
    // associated surface is minimized.

     // Get reference to the idle interface.
    auto idle = waylandServer()->display()->findChild<KdeIdle *>();
    QVERIFY(idle);
    QVERIFY(!idle->isInhibited());
    QSignalSpy inhibitedSpy(idle, &KdeIdle::inhibitedChanged);
    QVERIFY(inhibitedSpy.isValid());

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // Create the inhibitor object.
    QScopedPointer<IdleInhibitor> inhibitor(Test::waylandIdleInhibitManager()->createInhibitor(surface.data()));
    QVERIFY(inhibitor->isValid());

    // Render the client.
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // This should inhibit our server object.
    QVERIFY(idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 1);

    // Minimize the client, the idle inhibitor object should not be honored.
    win::set_minimized(c, true);
    QVERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 2);

    // Unminimize the client, the idle inhibitor object should be honored back again.
    win::set_minimized(c, false);
    QVERIFY(idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 3);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(c));
    QTRY_VERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 4);
}

void TestIdleInhibition::testDontInhibitWhenUnmapped()
{
    // This test verifies that the idle inhibitor object is not honored by KWin
    // when the associated client is unmapped.

     // Get reference to the idle interface.
    auto idle = waylandServer()->display()->findChild<KdeIdle *>();
    QVERIFY(idle);
    QVERIFY(!idle->isInhibited());
    QSignalSpy inhibitedSpy(idle, &KdeIdle::inhibitedChanged);
    QVERIFY(inhibitedSpy.isValid());

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // Create the inhibitor object.
    QScopedPointer<IdleInhibitor> inhibitor(Test::waylandIdleInhibitManager()->createInhibitor(surface.data()));
    QVERIFY(inhibitor->isValid());

    // Render the client.
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // This should inhibit our server object.
    QVERIFY(idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 1);

    // Unmap the client.
    QSignalSpy hiddenSpy(c, &win::wayland::window::windowHidden);
    QVERIFY(hiddenSpy.isValid());
    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(hiddenSpy.wait());

    // The surface is no longer visible, so the compositor don't have to honor the
    // idle inhibitor object.
    QVERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 2);

    // Map the client.
    QSignalSpy windowShownSpy(c, &win::wayland::window::windowShown);
    QVERIFY(windowShownSpy.isValid());
    Test::render(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(windowShownSpy.wait());

    // The test client became visible again, so the compositor has to honor the idle
    // inhibitor object back again.
    QVERIFY(idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 3);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(c));
    QTRY_VERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 4);
}

void TestIdleInhibition::testDontInhibitWhenLeftCurrentDesktop()
{
    // This test verifies that the idle inhibitor object is not honored by KWin
    // when the associated surface leaves the current virtual desktop.

    VirtualDesktopManager::self()->setCount(2);
    QCOMPARE(VirtualDesktopManager::self()->count(), 2u);

    // Get reference to the idle interface.
    auto idle = waylandServer()->display()->findChild<KdeIdle *>();
    QVERIFY(idle);
    QVERIFY(!idle->isInhibited());
    QSignalSpy inhibitedSpy(idle, &KdeIdle::inhibitedChanged);
    QVERIFY(inhibitedSpy.isValid());

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // Create the inhibitor object.
    QScopedPointer<IdleInhibitor> inhibitor(Test::waylandIdleInhibitManager()->createInhibitor(surface.data()));
    QVERIFY(inhibitor->isValid());

    // Render the client.
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // The test client should be only on the first virtual desktop.
    QCOMPARE(c->desktops().count(), 1);
    QCOMPARE(c->desktops().first(), VirtualDesktopManager::self()->desktops().first());

    // This should inhibit our server object.
    QVERIFY(idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 1);

    // Let the client enter the second virtual desktop.
    win::enter_desktop(c, VirtualDesktopManager::self()->desktops().at(1));
    QCOMPARE(inhibitedSpy.count(), 1);

    // If the client leaves the first virtual desktop, then the associated idle
    // inhibitor object should not be honored.
    win::leave_desktop(c, VirtualDesktopManager::self()->desktops().at(0));
    QVERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 2);

    // If the client enters the first desktop, then the associated idle inhibitor
    // object should be honored back again.
    win::enter_desktop(c, VirtualDesktopManager::self()->desktops().at(0));
    QVERIFY(idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 3);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(c));
    QTRY_VERIFY(!idle->isInhibited());
    QCOMPARE(inhibitedSpy.count(), 4);
}

WAYLANDTEST_MAIN(TestIdleInhibition)
#include "idle_inhibition_test.moc"
