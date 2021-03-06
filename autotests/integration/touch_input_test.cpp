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
#include "screens.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/deco.h"
#include "win/move.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_touch_input-0");

class TouchInputTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testTouchHidesCursor();
    void testMultipleTouchPoints_data();
    void testMultipleTouchPoints();
    void testCancel();
    void testTouchMouseAction();

private:
    Toplevel* showWindow(bool decorated = false);
    Wrapland::Client::Touch *m_touch = nullptr;
};

void TouchInputTest::initTestCase()
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
    waylandServer()->initWorkspace();
}

void TouchInputTest::init()
{
    using namespace Wrapland::Client;
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::Seat
                                 | Test::AdditionalWaylandInterface::XdgDecoration);
    QVERIFY(Test::waitForWaylandTouch());
    m_touch = Test::waylandSeat()->createTouch(Test::waylandSeat());
    QVERIFY(m_touch);
    QVERIFY(m_touch->isValid());

    screens()->setCurrent(0);
    Cursor::setPos(QPoint(1280, 512));
}

void TouchInputTest::cleanup()
{
    delete m_touch;
    m_touch = nullptr;
    Test::destroyWaylandConnection();
}

Toplevel* TouchInputTest::showWindow(bool decorated)
{
    using namespace Wrapland::Client;
#define VERIFY(statement) \
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))\
        return nullptr;
#define COMPARE(actual, expected) \
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return nullptr;

    Surface *surface = Test::createSurface(Test::waylandCompositor());
    VERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface, surface,
                                                        Test::CreationSetup::CreateOnly);
    VERIFY(shellSurface);
    if (decorated) {
        auto deco = Test::xdgDecorationManager()->getToplevelDecoration(shellSurface, shellSurface);
        QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
        VERIFY(decoSpy.isValid());
        deco->setMode(XdgDecoration::Mode::ServerSide);
        COMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
        Test::init_xdg_shell_toplevel(surface, shellSurface);
        COMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);
    } else {
        Test::init_xdg_shell_toplevel(surface, shellSurface);
    }
    // let's render
    auto c = Test::renderAndWaitForShown(surface, QSize(100, 50), Qt::blue);

    VERIFY(c);
    COMPARE(workspace()->activeClient(), c);

#undef VERIFY
#undef COMPARE

    return c;
}

void TouchInputTest::testTouchHidesCursor()
{
    QCOMPARE(kwinApp()->platform()->isCursorHidden(), false);
    quint32 timestamp = 1;
    kwinApp()->platform()->touchDown(1, QPointF(125, 125), timestamp++);
    QCOMPARE(kwinApp()->platform()->isCursorHidden(), true);
    kwinApp()->platform()->touchDown(2, QPointF(130, 125), timestamp++);
    kwinApp()->platform()->touchUp(2, timestamp++);
    kwinApp()->platform()->touchUp(1, timestamp++);

    // now a mouse event should show the cursor again
    kwinApp()->platform()->pointerMotion(QPointF(0, 0), timestamp++);
    QCOMPARE(kwinApp()->platform()->isCursorHidden(), false);

    // touch should hide again
    kwinApp()->platform()->touchDown(1, QPointF(125, 125), timestamp++);
    kwinApp()->platform()->touchUp(1, timestamp++);
    QCOMPARE(kwinApp()->platform()->isCursorHidden(), true);

    // wheel should also show
    kwinApp()->platform()->pointerAxisVertical(1.0, timestamp++);
    QCOMPARE(kwinApp()->platform()->isCursorHidden(), false);
}

void TouchInputTest::testMultipleTouchPoints_data()
{
    QTest::addColumn<bool>("decorated");

    QTest::newRow("undecorated") << false;
    QTest::newRow("decorated") << true;
}

void TouchInputTest::testMultipleTouchPoints()
{
    using namespace Wrapland::Client;
    QFETCH(bool, decorated);
    auto c = showWindow(decorated);
    QCOMPARE(win::decoration(c) != nullptr, decorated);
    win::move(c, QPoint(100, 100));
    QVERIFY(c);
    QSignalSpy sequenceStartedSpy(m_touch, &Touch::sequenceStarted);
    QVERIFY(sequenceStartedSpy.isValid());
    QSignalSpy pointAddedSpy(m_touch, &Touch::pointAdded);
    QVERIFY(pointAddedSpy.isValid());
    QSignalSpy pointMovedSpy(m_touch, &Touch::pointMoved);
    QVERIFY(pointMovedSpy.isValid());
    QSignalSpy pointRemovedSpy(m_touch, &Touch::pointRemoved);
    QVERIFY(pointRemovedSpy.isValid());
    QSignalSpy endedSpy(m_touch, &Touch::sequenceEnded);
    QVERIFY(endedSpy.isValid());

    quint32 timestamp = 1;
    kwinApp()->platform()->touchDown(1, QPointF(125, 125) + win::frame_to_client_pos(c, QPoint()), timestamp++);
    QVERIFY(sequenceStartedSpy.wait());
    QCOMPARE(sequenceStartedSpy.count(), 1);
    QCOMPARE(m_touch->sequence().count(), 1);
    QCOMPARE(m_touch->sequence().first()->isDown(), true);
    QCOMPARE(m_touch->sequence().first()->position(), QPointF(25, 25));
    QCOMPARE(pointAddedSpy.count(), 0);
    QCOMPARE(pointMovedSpy.count(), 0);

    // a point outside the window
    kwinApp()->platform()->touchDown(2, QPointF(0, 0) + win::frame_to_client_pos(c, QPoint()), timestamp++);
    QVERIFY(pointAddedSpy.wait());
    QCOMPARE(pointAddedSpy.count(), 1);
    QCOMPARE(m_touch->sequence().count(), 2);
    QCOMPARE(m_touch->sequence().at(1)->isDown(), true);
    QCOMPARE(m_touch->sequence().at(1)->position(), QPointF(-100, -100));
    QCOMPARE(pointMovedSpy.count(), 0);

    // let's move that one
    kwinApp()->platform()->touchMotion(2, QPointF(100, 100) + win::frame_to_client_pos(c, QPoint()), timestamp++);
    QVERIFY(pointMovedSpy.wait());
    QCOMPARE(pointMovedSpy.count(), 1);
    QCOMPARE(m_touch->sequence().count(), 2);
    QCOMPARE(m_touch->sequence().at(1)->isDown(), true);
    QCOMPARE(m_touch->sequence().at(1)->position(), QPointF(0, 0));

    kwinApp()->platform()->touchUp(1, timestamp++);
    QVERIFY(pointRemovedSpy.wait());
    QCOMPARE(pointRemovedSpy.count(), 1);
    QCOMPARE(m_touch->sequence().count(), 2);
    QCOMPARE(m_touch->sequence().first()->isDown(), false);
    QCOMPARE(endedSpy.count(), 0);

    kwinApp()->platform()->touchUp(2, timestamp++);
    QVERIFY(pointRemovedSpy.wait());
    QCOMPARE(pointRemovedSpy.count(), 2);
    QCOMPARE(m_touch->sequence().count(), 2);
    QCOMPARE(m_touch->sequence().first()->isDown(), false);
    QCOMPARE(m_touch->sequence().at(1)->isDown(), false);
    QCOMPARE(endedSpy.count(), 1);
}

void TouchInputTest::testCancel()
{
    using namespace Wrapland::Client;
    auto c = showWindow();
    win::move(c, QPoint(100, 100));
    QVERIFY(c);
    QSignalSpy sequenceStartedSpy(m_touch, &Touch::sequenceStarted);
    QVERIFY(sequenceStartedSpy.isValid());
    QSignalSpy cancelSpy(m_touch, &Touch::sequenceCanceled);
    QVERIFY(cancelSpy.isValid());
    QSignalSpy pointRemovedSpy(m_touch, &Touch::pointRemoved);
    QVERIFY(pointRemovedSpy.isValid());

    quint32 timestamp = 1;
    kwinApp()->platform()->touchDown(1, QPointF(125, 125), timestamp++);
    QVERIFY(sequenceStartedSpy.wait());
    QCOMPARE(sequenceStartedSpy.count(), 1);

    // cancel
    kwinApp()->platform()->touchCancel();
    QVERIFY(cancelSpy.wait());
    QCOMPARE(cancelSpy.count(), 1);

    kwinApp()->platform()->touchUp(1, timestamp++);
    QVERIFY(!pointRemovedSpy.wait(100));
    QCOMPARE(pointRemovedSpy.count(), 0);
}

void TouchInputTest::testTouchMouseAction()
{
    // this test verifies that a touch down on an inactive client will activate it
    using namespace Wrapland::Client;
    // create two windows
    auto c1 = showWindow();
    QVERIFY(c1);
    auto c2 = showWindow();
    QVERIFY(c2);

    QVERIFY(!c1->control->active());
    QVERIFY(c2->control->active());

    // also create a sequence started spy as the touch event should be passed through
    QSignalSpy sequenceStartedSpy(m_touch, &Touch::sequenceStarted);
    QVERIFY(sequenceStartedSpy.isValid());

    quint32 timestamp = 1;
    kwinApp()->platform()->touchDown(1, c1->frameGeometry().center(), timestamp++);
    QVERIFY(c1->control->active());

    QVERIFY(sequenceStartedSpy.wait());
    QCOMPARE(sequenceStartedSpy.count(), 1);

    // cleanup
    kwinApp()->platform()->touchCancel();
}

}

WAYLANDTEST_MAIN(KWin::TouchInputTest)
#include "touch_input_test.moc"
