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
#include "cursor.h"
#include "internal_client.h"
#include "platform.h"
#include "pointer_input.h"
#include "touch_input.h"
#include "screenedge.h"
#include "screens.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"
#include <kwineffects.h>

#include "win/deco.h"
#include "win/move.h"
#include "win/wayland/window.h"

#include "decorations/decoratedclient.h"
#include "decorations/decorationbridge.h"
#include "decorations/settings.h"

#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

#include <linux/input.h>

Q_DECLARE_METATYPE(Qt::WindowFrameSection)

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_decoration_input-0");

class DecorationInputTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testAxis_data();
    void testAxis();
    void testDoubleClick_data();
    void testDoubleClick();
    void testDoubleTap_data();
    void testDoubleTap();
    void testHover();
    void testPressToMove_data();
    void testPressToMove();
    void testTapToMove_data();
    void testTapToMove();
    void testResizeOutsideWindow_data();
    void testResizeOutsideWindow();
    void testModifierClickUnrestrictedMove_data();
    void testModifierClickUnrestrictedMove();
    void testModifierScrollOpacity_data();
    void testModifierScrollOpacity();
    void testTouchEvents();
    void testTooltipDoesntEatKeyEvents();

private:
    Toplevel* showWindow();
};

#define MOTION(target) \
    kwinApp()->platform()->pointerMotion(target, timestamp++)

#define PRESS \
    kwinApp()->platform()->pointerButtonPressed(BTN_LEFT, timestamp++)

#define RELEASE \
    kwinApp()->platform()->pointerButtonReleased(BTN_LEFT, timestamp++)

Toplevel* DecorationInputTest::showWindow()
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

    QSignalSpy configureRequestedSpy(shellSurface, &XdgShellToplevel::configureRequested);

    auto deco = Test::xdgDecorationManager()->getToplevelDecoration(shellSurface, shellSurface);
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    VERIFY(decoSpy.isValid());
    deco->setMode(XdgDecoration::Mode::ServerSide);
    COMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
    Test::init_xdg_shell_toplevel(surface, shellSurface);
    COMPARE(decoSpy.count(), 1);
    COMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

    VERIFY(configureRequestedSpy.count() > 0 || configureRequestedSpy.wait());
    COMPARE(configureRequestedSpy.count(), 1);

    shellSurface->ackConfigure(configureRequestedSpy.last()[2].toInt());

    // let's render
    auto c = Test::renderAndWaitForShown(surface, QSize(500, 50), Qt::blue);
    VERIFY(c);
    COMPARE(workspace()->activeClient(), c);
    COMPARE(c->userCanSetNoBorder(), true);
    COMPARE(win::decoration(c) != nullptr, true);

#undef VERIFY
#undef COMPARE

    return c;
}

void DecorationInputTest::initTestCase()
{
    qRegisterMetaType<KWin::InternalClient *>();
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    // change some options
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group(QStringLiteral("MouseBindings")).writeEntry("CommandTitlebarWheel", QStringLiteral("above/below"));
    config->group(QStringLiteral("Windows")).writeEntry("TitlebarDoubleClickCommand", QStringLiteral("OnAllDesktops"));
    config->group(QStringLiteral("Desktops")).writeEntry("Number", 2);
    config->sync();

    kwinApp()->setConfig(config);

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
    setenv("QT_QPA_PLATFORM", "wayland", true);
    waylandServer()->initWorkspace();
}

void DecorationInputTest::init()
{
    using namespace Wrapland::Client;
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::Seat
                                 | Test::AdditionalWaylandInterface::XdgDecoration);
    QVERIFY(Test::waitForWaylandPointer());

    screens()->setCurrent(0);
    Cursor::setPos(QPoint(640, 512));
}

void DecorationInputTest::cleanup()
{
    Test::destroyWaylandConnection();
}

void DecorationInputTest::testAxis_data()
{
    QTest::addColumn<QPoint>("decoPoint");
    QTest::addColumn<Qt::WindowFrameSection>("expectedSection");

    QTest::newRow("topLeft|xdgWmBase") << QPoint(0, 0) << Qt::TopLeftSection;
    QTest::newRow("top|xdgWmBase") << QPoint(250, 0) << Qt::TopSection;
    QTest::newRow("topRight|xdgWmBase") << QPoint(499, 0) << Qt::TopRightSection;
}

void DecorationInputTest::testAxis()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    QVERIFY(!c->control->keep_above());
    QVERIFY(!c->control->keep_below());

    quint32 timestamp = 1;

    MOTION(QPoint(c->frameGeometry().center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));

    QVERIFY(input_redirect()->pointer()->decoration());
    QCOMPARE(input_redirect()->pointer()->decoration()->decoration()->sectionUnderMouse(), Qt::TitleBarArea);

    // TODO: mouse wheel direction looks wrong to me
    // simulate wheel
    kwinApp()->platform()->pointerAxisVertical(5.0, timestamp++);
    QVERIFY(c->control->keep_below());
    QVERIFY(!c->control->keep_above());
    kwinApp()->platform()->pointerAxisVertical(-5.0, timestamp++);
    QVERIFY(!c->control->keep_below());
    QVERIFY(!c->control->keep_above());
    kwinApp()->platform()->pointerAxisVertical(-5.0, timestamp++);
    QVERIFY(!c->control->keep_below());
    QVERIFY(c->control->keep_above());

    // test top most deco pixel, BUG: 362860
    win::move(c, QPoint(0, 0));
    QFETCH(QPoint, decoPoint);
    MOTION(decoPoint);
    QVERIFY(input_redirect()->pointer()->decoration());
    QCOMPARE(input_redirect()->pointer()->decoration()->client(), c);
    QTEST(input_redirect()->pointer()->decoration()->decoration()->sectionUnderMouse(), "expectedSection");
    kwinApp()->platform()->pointerAxisVertical(5.0, timestamp++);
    QVERIFY(!c->control->keep_below());
    QVERIFY(!c->control->keep_above());
}

void DecorationInputTest::testDoubleClick_data()
{
    QTest::addColumn<QPoint>("decoPoint");
    QTest::addColumn<Qt::WindowFrameSection>("expectedSection");

    QTest::newRow("topLeft|xdgWmBase") << QPoint(0, 0) << Qt::TopLeftSection;
    QTest::newRow("top|xdgWmBase") << QPoint(250, 0) << Qt::TopSection;
    QTest::newRow("topRight|xdgWmBase") << QPoint(499, 0) << Qt::TopRightSection;
}

void KWin::DecorationInputTest::testDoubleClick()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    QVERIFY(!c->isOnAllDesktops());
    quint32 timestamp = 1;
    MOTION(QPoint(c->frameGeometry().center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));

    // double click
    PRESS;
    RELEASE;
    PRESS;
    RELEASE;
    QVERIFY(c->isOnAllDesktops());
    // double click again
    PRESS;
    RELEASE;
    QVERIFY(c->isOnAllDesktops());
    PRESS;
    RELEASE;
    QVERIFY(!c->isOnAllDesktops());

    // test top most deco pixel, BUG: 362860
    win::move(c, QPoint(0, 0));
    QFETCH(QPoint, decoPoint);
    MOTION(decoPoint);
    QVERIFY(input_redirect()->pointer()->decoration());
    QCOMPARE(input_redirect()->pointer()->decoration()->client(), c);
    QTEST(input_redirect()->pointer()->decoration()->decoration()->sectionUnderMouse(), "expectedSection");
    // double click
    PRESS;
    RELEASE;
    QVERIFY(!c->isOnAllDesktops());
    PRESS;
    RELEASE;
    QVERIFY(c->isOnAllDesktops());
}

void DecorationInputTest::testDoubleTap_data()
{
    QTest::addColumn<QPoint>("decoPoint");
    QTest::addColumn<Qt::WindowFrameSection>("expectedSection");

    QTest::newRow("topLeft|xdgWmBase") << QPoint(10, 10) << Qt::TopLeftSection;
    QTest::newRow("top|xdgWmBase") << QPoint(260, 10) << Qt::TopSection;
    QTest::newRow("topRight|xdgWmBase") << QPoint(509, 10) << Qt::TopRightSection;
}

void KWin::DecorationInputTest::testDoubleTap()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    QVERIFY(!c->isOnAllDesktops());
    quint32 timestamp = 1;
    const QPoint tapPoint(c->frameGeometry().center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2);

    // double tap
    kwinApp()->platform()->touchDown(0, tapPoint, timestamp++);
    kwinApp()->platform()->touchUp(0, timestamp++);
    kwinApp()->platform()->touchDown(0, tapPoint, timestamp++);
    kwinApp()->platform()->touchUp(0, timestamp++);
    QVERIFY(c->isOnAllDesktops());
    // double tap again
    kwinApp()->platform()->touchDown(0, tapPoint, timestamp++);
    kwinApp()->platform()->touchUp(0, timestamp++);
    QVERIFY(c->isOnAllDesktops());
    kwinApp()->platform()->touchDown(0, tapPoint, timestamp++);
    kwinApp()->platform()->touchUp(0, timestamp++);
    QVERIFY(!c->isOnAllDesktops());

    // test top most deco pixel, BUG: 362860
    //
    // Not directly at (0, 0), otherwise ScreenEdgeInputFilter catches
    // event before DecorationEventFilter.
    win::move(c, QPoint(10, 10));
    QFETCH(QPoint, decoPoint);
    // double click
    kwinApp()->platform()->touchDown(0, decoPoint, timestamp++);
    QVERIFY(input_redirect()->touch()->decoration());
    QCOMPARE(input_redirect()->touch()->decoration()->client(), c);
    QTEST(input_redirect()->touch()->decoration()->decoration()->sectionUnderMouse(), "expectedSection");
    kwinApp()->platform()->touchUp(0, timestamp++);
    QVERIFY(!c->isOnAllDesktops());
    kwinApp()->platform()->touchDown(0, decoPoint, timestamp++);
    kwinApp()->platform()->touchUp(0, timestamp++);
    QVERIFY(c->isOnAllDesktops());
}

void DecorationInputTest::testHover()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());

    // our left border is moved out of the visible area, so move the window to a better place
    win::move(c, QPoint(20, 0));

    quint32 timestamp = 1;
    MOTION(QPoint(c->frameGeometry().center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(Qt::ArrowCursor));

    // There is a mismatch of the cursor key positions between windows
    // with and without borders (with borders one can move inside a bit and still
    // be on an edge, without not). We should make this consistent in KWin's core.
    //
    // TODO: Test input position with different border sizes.
    // TODO: We should test with the fake decoration to have a fixed test environment.
    const bool hasBorders = Decoration::DecorationBridge::self()->settings()->borderSize() != KDecoration2::BorderSize::None;
    auto deviation = [hasBorders] {
        return hasBorders ? -1 : 0;
    };

    MOTION(QPoint(c->frameGeometry().x(), 0));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(KWin::ExtendedCursor::SizeNorthWest));
    MOTION(QPoint(c->frameGeometry().x() + c->frameGeometry().width() / 2, 0));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(KWin::ExtendedCursor::SizeNorth));
    MOTION(QPoint(c->frameGeometry().x() + c->frameGeometry().width() - 1, 0));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(KWin::ExtendedCursor::SizeNorthEast));
    MOTION(QPoint(c->frameGeometry().x() + c->frameGeometry().width() + deviation(), c->size().height() / 2));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(KWin::ExtendedCursor::SizeEast));
    MOTION(QPoint(c->frameGeometry().x() + c->frameGeometry().width() + deviation(), c->size().height() - 1));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(KWin::ExtendedCursor::SizeSouthEast));
    MOTION(QPoint(c->frameGeometry().x() + c->frameGeometry().width() / 2, c->size().height() + deviation()));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(KWin::ExtendedCursor::SizeSouth));
    MOTION(QPoint(c->frameGeometry().x(), c->size().height() + deviation()));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(KWin::ExtendedCursor::SizeSouthWest));
    MOTION(QPoint(c->frameGeometry().x() - 1, c->size().height() / 2));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(KWin::ExtendedCursor::SizeWest));

    MOTION(c->frameGeometry().center());
    QEXPECT_FAIL("", "Cursor not set back on leave", Continue);
    QCOMPARE(c->control->move_resize().cursor, CursorShape(Qt::ArrowCursor));
}

void DecorationInputTest::testPressToMove_data()
{
    QTest::addColumn<QPoint>("offset");
    QTest::addColumn<QPoint>("offset2");
    QTest::addColumn<QPoint>("offset3");

    QTest::newRow("To right|xdgWmBase")  << QPoint(10, 0)  << QPoint(20, 0)  << QPoint(30, 0);
    QTest::newRow("To left|xdgWmBase")   << QPoint(-10, 0) << QPoint(-20, 0) << QPoint(-30, 0);
    QTest::newRow("To bottom|xdgWmBase") << QPoint(0, 10)  << QPoint(0, 20)  << QPoint(0, 30);
    QTest::newRow("To top|xdgWmBase")    << QPoint(0, -10) << QPoint(0, -20) << QPoint(0, -30);
}

void DecorationInputTest::testPressToMove()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c, screens()->geometry(0).center() - QPoint(c->size().width()/2, c->size().height()/2));
    QSignalSpy startMoveResizedSpy(c, &Toplevel::clientStartUserMovedResized);
    QVERIFY(startMoveResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(c, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    quint32 timestamp = 1;
    MOTION(QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));
    QCOMPARE(c->control->move_resize().cursor, CursorShape(Qt::ArrowCursor));

    PRESS;
    QVERIFY(!win::is_move(c));
    QFETCH(QPoint, offset);
    MOTION(QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2) + offset);
    const QPoint oldPos = c->pos();
    QVERIFY(win::is_move(c));
    QCOMPARE(startMoveResizedSpy.count(), 1);

    RELEASE;
    QTRY_VERIFY(!win::is_move(c));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QEXPECT_FAIL("", "Just trigger move doesn't move the window", Continue);
    QCOMPARE(c->pos(), oldPos + offset);

    // again
    PRESS;
    QVERIFY(!win::is_move(c));
    QFETCH(QPoint, offset2);
    MOTION(QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2) + offset2);
    QVERIFY(win::is_move(c));
    QCOMPARE(startMoveResizedSpy.count(), 2);
    QFETCH(QPoint, offset3);
    MOTION(QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2) + offset3);

    RELEASE;
    QTRY_VERIFY(!win::is_move(c));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 2);
    // TODO: the offset should also be included
    QCOMPARE(c->pos(), oldPos + offset2 + offset3);
}

void DecorationInputTest::testTapToMove_data()
{
    QTest::addColumn<QPoint>("offset");
    QTest::addColumn<QPoint>("offset2");
    QTest::addColumn<QPoint>("offset3");

    QTest::newRow("To right|xdgWmBase")  << QPoint(10, 0)  << QPoint(20, 0)  << QPoint(30, 0);
    QTest::newRow("To left|xdgWmBase")   << QPoint(-10, 0) << QPoint(-20, 0) << QPoint(-30, 0);
    QTest::newRow("To bottom|xdgWmBase") << QPoint(0, 10)  << QPoint(0, 20)  << QPoint(0, 30);
    QTest::newRow("To top|xdgWmBase")    << QPoint(0, -10) << QPoint(0, -20) << QPoint(0, -30);
}

void DecorationInputTest::testTapToMove()
{
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c, screens()->geometry(0).center() - QPoint(c->size().width()/2, c->size().height()/2));
    QSignalSpy startMoveResizedSpy(c, &Toplevel::clientStartUserMovedResized);
    QVERIFY(startMoveResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(c, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    quint32 timestamp = 1;
    QPoint p = QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2);

    kwinApp()->platform()->touchDown(0, p, timestamp++);
    QVERIFY(!win::is_move(c));
    QFETCH(QPoint, offset);
    QCOMPARE(input_redirect()->touch()->decorationPressId(), 0);
    kwinApp()->platform()->touchMotion(0, p + offset, timestamp++);
    const QPoint oldPos = c->pos();
    QVERIFY(win::is_move(c));
    QCOMPARE(startMoveResizedSpy.count(), 1);

    kwinApp()->platform()->touchUp(0, timestamp++);
    QTRY_VERIFY(!win::is_move(c));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QEXPECT_FAIL("", "Just trigger move doesn't move the window", Continue);
    QCOMPARE(c->pos(), oldPos + offset);

    // again
    kwinApp()->platform()->touchDown(1, p + offset, timestamp++);
    QCOMPARE(input_redirect()->touch()->decorationPressId(), 1);
    QVERIFY(!win::is_move(c));
    QFETCH(QPoint, offset2);
    kwinApp()->platform()->touchMotion(1, QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2) + offset2, timestamp++);
    QVERIFY(win::is_move(c));
    QCOMPARE(startMoveResizedSpy.count(), 2);
    QFETCH(QPoint, offset3);
    kwinApp()->platform()->touchMotion(1, QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2) + offset3, timestamp++);

    kwinApp()->platform()->touchUp(1, timestamp++);
    QTRY_VERIFY(!win::is_move(c));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 2);
    // TODO: the offset should also be included
    QCOMPARE(c->pos(), oldPos + offset2 + offset3);
}

void DecorationInputTest::testResizeOutsideWindow_data()
{
    QTest::addColumn<Qt::Edge>("edge");
    QTest::addColumn<Qt::CursorShape>("expectedCursor");

    QTest::newRow("left") << Qt::LeftEdge << Qt::SizeHorCursor;
    QTest::newRow("right") << Qt::RightEdge << Qt::SizeHorCursor;
    QTest::newRow("bottom") << Qt::BottomEdge << Qt::SizeVerCursor;
}

void DecorationInputTest::testResizeOutsideWindow()
{
    // this test verifies that one can resize the window outside the decoration with NoSideBorder

    // first adjust config
    kwinApp()->config()->group("org.kde.kdecoration2").writeEntry("BorderSize", QStringLiteral("None"));
    kwinApp()->config()->sync();
    workspace()->slotReconfigure();

    // now create window
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c, screens()->geometry(0).center() - QPoint(c->size().width()/2, c->size().height()/2));
    QVERIFY(c->frameGeometry() != win::input_geometry(c));
    QVERIFY(win::input_geometry(c).contains(c->frameGeometry()));
    QSignalSpy startMoveResizedSpy(c, &Toplevel::clientStartUserMovedResized);
    QVERIFY(startMoveResizedSpy.isValid());

    // go to border
    quint32 timestamp = 1;
    QFETCH(Qt::Edge, edge);
    switch (edge) {
    case Qt::LeftEdge:
        MOTION(QPoint(c->frameGeometry().x() -1, c->frameGeometry().center().y()));
        break;
    case Qt::RightEdge:
        MOTION(QPoint(c->frameGeometry().x() + c->frameGeometry().width() +1, c->frameGeometry().center().y()));
        break;
    case Qt::BottomEdge:
        MOTION(QPoint(c->frameGeometry().center().x(), c->frameGeometry().y() + c->frameGeometry().height() + 1));
        break;
    default:
        break;
    }
    QVERIFY(!c->frameGeometry().contains(KWin::Cursor::pos()));

    // pressing should trigger resize
    PRESS;
    QVERIFY(!win::is_resize(c));
    QVERIFY(startMoveResizedSpy.wait());
    QVERIFY(win::is_resize(c));

    RELEASE;
    QVERIFY(!win::is_resize(c));
}

void DecorationInputTest::testModifierClickUnrestrictedMove_data()
{
    QTest::addColumn<int>("modifierKey");
    QTest::addColumn<int>("mouseButton");
    QTest::addColumn<QString>("modKey");
    QTest::addColumn<bool>("capsLock");

    const QString alt = QStringLiteral("Alt");
    const QString meta = QStringLiteral("Meta");

    QTest::newRow("Left Alt + Left Click")    << KEY_LEFTALT  << BTN_LEFT   << alt << false;
    QTest::newRow("Left Alt + Right Click")   << KEY_LEFTALT  << BTN_RIGHT  << alt << false;
    QTest::newRow("Left Alt + Middle Click")  << KEY_LEFTALT  << BTN_MIDDLE << alt << false;
    QTest::newRow("Right Alt + Left Click")   << KEY_RIGHTALT << BTN_LEFT   << alt << false;
    QTest::newRow("Right Alt + Right Click")  << KEY_RIGHTALT << BTN_RIGHT  << alt << false;
    QTest::newRow("Right Alt + Middle Click") << KEY_RIGHTALT << BTN_MIDDLE << alt << false;
    // now everything with meta
    QTest::newRow("Left Meta + Left Click")    << KEY_LEFTMETA  << BTN_LEFT   << meta << false;
    QTest::newRow("Left Meta + Right Click")   << KEY_LEFTMETA  << BTN_RIGHT  << meta << false;
    QTest::newRow("Left Meta + Middle Click")  << KEY_LEFTMETA  << BTN_MIDDLE << meta << false;
    QTest::newRow("Right Meta + Left Click")   << KEY_RIGHTMETA << BTN_LEFT   << meta << false;
    QTest::newRow("Right Meta + Right Click")  << KEY_RIGHTMETA << BTN_RIGHT  << meta << false;
    QTest::newRow("Right Meta + Middle Click") << KEY_RIGHTMETA << BTN_MIDDLE << meta << false;

    // and with capslock
    QTest::newRow("Left Alt + Left Click/CapsLock")    << KEY_LEFTALT  << BTN_LEFT   << alt << true;
    QTest::newRow("Left Alt + Right Click/CapsLock")   << KEY_LEFTALT  << BTN_RIGHT  << alt << true;
    QTest::newRow("Left Alt + Middle Click/CapsLock")  << KEY_LEFTALT  << BTN_MIDDLE << alt << true;
    QTest::newRow("Right Alt + Left Click/CapsLock")   << KEY_RIGHTALT << BTN_LEFT   << alt << true;
    QTest::newRow("Right Alt + Right Click/CapsLock")  << KEY_RIGHTALT << BTN_RIGHT  << alt << true;
    QTest::newRow("Right Alt + Middle Click/CapsLock") << KEY_RIGHTALT << BTN_MIDDLE << alt << true;
    // now everything with meta
    QTest::newRow("Left Meta + Left Click/CapsLock")    << KEY_LEFTMETA  << BTN_LEFT   << meta << true;
    QTest::newRow("Left Meta + Right Click/CapsLock")   << KEY_LEFTMETA  << BTN_RIGHT  << meta << true;
    QTest::newRow("Left Meta + Middle Click/CapsLock")  << KEY_LEFTMETA  << BTN_MIDDLE << meta << true;
    QTest::newRow("Right Meta + Left Click/CapsLock")   << KEY_RIGHTMETA << BTN_LEFT   << meta << true;
    QTest::newRow("Right Meta + Right Click/CapsLock")  << KEY_RIGHTMETA << BTN_RIGHT  << meta << true;
    QTest::newRow("Right Meta + Middle Click/CapsLock") << KEY_RIGHTMETA << BTN_MIDDLE << meta << true;
}

void DecorationInputTest::testModifierClickUnrestrictedMove()
{
    // this test ensures that Alt+mouse button press triggers unrestricted move

    // first modify the config for this run
    QFETCH(QString, modKey);
    KConfigGroup group = kwinApp()->config()->group("MouseBindings");
    group.writeEntry("CommandAllKey", modKey);
    group.writeEntry("CommandAll1", "Move");
    group.writeEntry("CommandAll2", "Move");
    group.writeEntry("CommandAll3", "Move");
    group.sync();
    workspace()->slotReconfigure();
    QCOMPARE(options->commandAllModifier(), modKey == QStringLiteral("Alt") ? Qt::AltModifier : Qt::MetaModifier);
    QCOMPARE(options->commandAll1(), Options::MouseUnrestrictedMove);
    QCOMPARE(options->commandAll2(), Options::MouseUnrestrictedMove);
    QCOMPARE(options->commandAll3(), Options::MouseUnrestrictedMove);

    // create a window
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c, screens()->geometry(0).center() - QPoint(c->size().width()/2, c->size().height()/2));
    // move cursor on window
    Cursor::setPos(QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));

    // simulate modifier+click
    quint32 timestamp = 1;
    QFETCH(bool, capsLock);
    if (capsLock) {
        kwinApp()->platform()->keyboardKeyPressed(KEY_CAPSLOCK, timestamp++);
    }
    QFETCH(int, modifierKey);
    QFETCH(int, mouseButton);
    kwinApp()->platform()->keyboardKeyPressed(modifierKey, timestamp++);
    QVERIFY(!win::is_move(c));
    kwinApp()->platform()->pointerButtonPressed(mouseButton, timestamp++);
    QVERIFY(win::is_move(c));
    // release modifier should not change it
    kwinApp()->platform()->keyboardKeyReleased(modifierKey, timestamp++);
    QVERIFY(win::is_move(c));
    // but releasing the key should end move/resize
    kwinApp()->platform()->pointerButtonReleased(mouseButton, timestamp++);
    QVERIFY(!win::is_move(c));
    if (capsLock) {
        kwinApp()->platform()->keyboardKeyReleased(KEY_CAPSLOCK, timestamp++);
    }
}

void DecorationInputTest::testModifierScrollOpacity_data()
{
    QTest::addColumn<int>("modifierKey");
    QTest::addColumn<QString>("modKey");
    QTest::addColumn<bool>("capsLock");

    const QString alt = QStringLiteral("Alt");
    const QString meta = QStringLiteral("Meta");

    QTest::newRow("Left Alt")   << KEY_LEFTALT  << alt << false;
    QTest::newRow("Right Alt")  << KEY_RIGHTALT << alt << false;
    QTest::newRow("Left Meta")  << KEY_LEFTMETA  << meta << false;
    QTest::newRow("Right Meta") << KEY_RIGHTMETA << meta << false;
    QTest::newRow("Left Alt/CapsLock")   << KEY_LEFTALT  << alt << true;
    QTest::newRow("Right Alt/CapsLock")  << KEY_RIGHTALT << alt << true;
    QTest::newRow("Left Meta/CapsLock")  << KEY_LEFTMETA  << meta << true;
    QTest::newRow("Right Meta/CapsLock") << KEY_RIGHTMETA << meta << true;
}

void DecorationInputTest::testModifierScrollOpacity()
{
    // this test verifies that mod+wheel performs a window operation

    // first modify the config for this run
    QFETCH(QString, modKey);
    KConfigGroup group = kwinApp()->config()->group("MouseBindings");
    group.writeEntry("CommandAllKey", modKey);
    group.writeEntry("CommandAllWheel", "change opacity");
    group.sync();
    workspace()->slotReconfigure();

    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    win::move(c, screens()->geometry(0).center() - QPoint(c->size().width()/2, c->size().height()/2));
    // move cursor on window
    Cursor::setPos(QPoint(c->frameGeometry().center().x(), c->pos().y() + win::frame_to_client_pos(c, QPoint()).y() / 2));
    // set the opacity to 0.5
    c->setOpacity(0.5);
    QCOMPARE(c->opacity(), 0.5);

    // simulate modifier+wheel
    quint32 timestamp = 1;
    QFETCH(bool, capsLock);
    if (capsLock) {
        kwinApp()->platform()->keyboardKeyPressed(KEY_CAPSLOCK, timestamp++);
    }
    QFETCH(int, modifierKey);
    kwinApp()->platform()->keyboardKeyPressed(modifierKey, timestamp++);
    kwinApp()->platform()->pointerAxisVertical(-5, timestamp++);
    QCOMPARE(c->opacity(), 0.6);
    kwinApp()->platform()->pointerAxisVertical(5, timestamp++);
    QCOMPARE(c->opacity(), 0.5);
    kwinApp()->platform()->keyboardKeyReleased(modifierKey, timestamp++);
    if (capsLock) {
        kwinApp()->platform()->keyboardKeyReleased(KEY_CAPSLOCK, timestamp++);
    }
}

class EventHelper : public QObject
{
    Q_OBJECT
public:
    EventHelper() : QObject() {}
    ~EventHelper() override = default;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched)
        if (event->type() == QEvent::HoverMove) {
            emit hoverMove();
        } else if (event->type() == QEvent::HoverLeave) {
            emit hoverLeave();
        }
        return false;
    }

Q_SIGNALS:
    void hoverMove();
    void hoverLeave();
};

void DecorationInputTest::testTouchEvents()
{
    // this test verifies that the decoration gets a hover leave event on touch release
    // see BUG 386231
    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());

    EventHelper helper;
    win::decoration(c)->installEventFilter(&helper);
    QSignalSpy hoverMoveSpy(&helper, &EventHelper::hoverMove);
    QVERIFY(hoverMoveSpy.isValid());
    QSignalSpy hoverLeaveSpy(&helper, &EventHelper::hoverLeave);
    QVERIFY(hoverLeaveSpy.isValid());

    quint32 timestamp = 1;
    const QPoint tapPoint(c->frameGeometry().center().x(), win::frame_to_client_pos(c, QPoint()).y() / 2);

    QVERIFY(!input_redirect()->touch()->decoration());
    kwinApp()->platform()->touchDown(0, tapPoint, timestamp++);
    QVERIFY(input_redirect()->touch()->decoration());
    QCOMPARE(input_redirect()->touch()->decoration()->decoration(), win::decoration(c));
    QCOMPARE(hoverMoveSpy.count(), 1);
    QCOMPARE(hoverLeaveSpy.count(), 0);
    kwinApp()->platform()->touchUp(0, timestamp++);
    QCOMPARE(hoverMoveSpy.count(), 1);
    QCOMPARE(hoverLeaveSpy.count(), 1);

    QCOMPARE(win::is_move(c), false);

    // let's check that a hover motion is sent if the pointer is on deco, when touch release
    Cursor::setPos(tapPoint);
    QCOMPARE(hoverMoveSpy.count(), 2);
    kwinApp()->platform()->touchDown(0, tapPoint, timestamp++);
    QCOMPARE(hoverMoveSpy.count(), 3);
    QCOMPARE(hoverLeaveSpy.count(), 1);
    kwinApp()->platform()->touchUp(0, timestamp++);
    QCOMPARE(hoverMoveSpy.count(), 3);
    QCOMPARE(hoverLeaveSpy.count(), 2);
}

void DecorationInputTest::testTooltipDoesntEatKeyEvents()
{
    // this test verifies that a tooltip on the decoration does not steal key events
    // BUG: 393253

    // first create a keyboard
    auto keyboard = Test::waylandSeat()->createKeyboard(Test::waylandSeat());
    QVERIFY(keyboard);
    QSignalSpy enteredSpy(keyboard, &Wrapland::Client::Keyboard::entered);
    QVERIFY(enteredSpy.isValid());

    auto c = showWindow();
    QVERIFY(c);
    QVERIFY(win::decoration(c));
    QVERIFY(!c->noBorder());
    QVERIFY(enteredSpy.wait());

    QSignalSpy keyEvent(keyboard, &Wrapland::Client::Keyboard::keyChanged);
    QVERIFY(keyEvent.isValid());

    QSignalSpy clientAddedSpy(workspace(), &Workspace::internalClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    c->control->deco().client->requestShowToolTip(QStringLiteral("test"));
    // now we should get an internal window
    QVERIFY(clientAddedSpy.wait());
    InternalClient *internal = clientAddedSpy.first().first().value<InternalClient *>();
    QVERIFY(internal->isInternal());
    QVERIFY(internal->internalWindow()->flags().testFlag(Qt::ToolTip));

    // now send a key
    quint32 timestamp = 0;
    kwinApp()->platform()->keyboardKeyPressed(KEY_A, timestamp++);
    QVERIFY(keyEvent.wait());
    kwinApp()->platform()->keyboardKeyReleased(KEY_A, timestamp++);
    QVERIFY(keyEvent.wait());

    c->control->deco().client->requestHideToolTip();
    Test::waitForWindowDestroyed(internal);
}

}

WAYLANDTEST_MAIN(KWin::DecorationInputTest)
#include "decoration_input_test.moc"
