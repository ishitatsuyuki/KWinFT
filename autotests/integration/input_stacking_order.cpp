/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "cursor.h"
#include "screenedge.h"
#include "screens.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"
#include <kwineffects.h>

#include "win/move.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/seat.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_input_stacking_order-0");

class InputStackingOrderTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testPointerFocusUpdatesOnStackingOrderChange();

private:
    void render(Wrapland::Client::Surface *surface);
};

void InputStackingOrderTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
    setenv("QT_QPA_PLATFORM", "wayland", true);
    waylandServer()->initWorkspace();
}

void InputStackingOrderTest::init()
{
    using namespace Wrapland::Client;
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::Seat);
    QVERIFY(Test::waitForWaylandPointer());

    screens()->setCurrent(0);
    Cursor::setPos(QPoint(640, 512));
}

void InputStackingOrderTest::cleanup()
{
    Test::destroyWaylandConnection();
}

void InputStackingOrderTest::render(Wrapland::Client::Surface *surface)
{
    Test::render(surface, QSize(100, 50), Qt::blue);
    Test::flushWaylandConnection();
}

void InputStackingOrderTest::testPointerFocusUpdatesOnStackingOrderChange()
{
    // this test creates two windows which overlap
    // the pointer is in the overlapping area which means the top most window has focus
    // as soon as the top most window gets lowered the window should lose focus and the
    // other window should gain focus without a mouse event in between
    using namespace Wrapland::Client;
    // create pointer and signal spy for enter and leave signals
    auto pointer = Test::waylandSeat()->createPointer(Test::waylandSeat());
    QVERIFY(pointer);
    QVERIFY(pointer->isValid());
    QSignalSpy enteredSpy(pointer, &Pointer::entered);
    QVERIFY(enteredSpy.isValid());
    QSignalSpy leftSpy(pointer, &Pointer::left);
    QVERIFY(leftSpy.isValid());

    // now create the two windows and make them overlap
    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::window_added);
    QVERIFY(clientAddedSpy.isValid());
    Surface *surface1 = Test::createSurface(Test::waylandCompositor());
    QVERIFY(surface1);
    auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1, surface1);
    QVERIFY(shellSurface1);
    render(surface1);
    QVERIFY(clientAddedSpy.wait());
    auto window1 = workspace()->activeClient();
    QVERIFY(window1);

    Surface *surface2 = Test::createSurface(Test::waylandCompositor());
    QVERIFY(surface2);
    auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2, surface2);
    QVERIFY(shellSurface2);
    render(surface2);
    QVERIFY(clientAddedSpy.wait());

    auto window2 = workspace()->activeClient();
    QVERIFY(window2);
    QVERIFY(window1 != window2);

    // now make windows overlap
    win::move(window2, window1->pos());
    QCOMPARE(window1->frameGeometry(), window2->frameGeometry());

    // enter
    kwinApp()->platform()->pointerMotion(QPointF(25, 25), 1);
    QVERIFY(enteredSpy.wait());
    QCOMPARE(enteredSpy.count(), 1);
    // window 2 should have focus
    QCOMPARE(pointer->enteredSurface(), surface2);
    // also on the server
    QCOMPARE(waylandServer()->seat()->focusedPointerSurface(), window2->surface());

    // raise window 1 above window 2
    QVERIFY(leftSpy.isEmpty());
    workspace()->raise_window(window1);
    // should send leave to window2
    QVERIFY(leftSpy.wait());
    QCOMPARE(leftSpy.count(), 1);
    // and an enter to window1
    QCOMPARE(enteredSpy.count(), 2);
    QCOMPARE(pointer->enteredSurface(), surface1);
    QCOMPARE(waylandServer()->seat()->focusedPointerSurface(), window1->surface());

    // let's destroy window1, that should pass focus to window2 again
    QSignalSpy windowClosedSpy(window1, &Toplevel::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    surface1->deleteLater();
    QVERIFY(windowClosedSpy.wait());
    QVERIFY(enteredSpy.wait());
    QCOMPARE(enteredSpy.count(), 3);
    QCOMPARE(pointer->enteredSurface(), surface2);
    QCOMPARE(waylandServer()->seat()->focusedPointerSurface(), window2->surface());
}

}

WAYLANDTEST_MAIN(KWin::InputStackingOrderTest)
#include "input_stacking_order.moc"
