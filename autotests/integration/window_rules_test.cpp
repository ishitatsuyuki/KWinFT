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
#include "atoms.h"
#include "cursor.h"
#include "screenedge.h"
#include "screens.h"
#include "rules/rule_book.h"
#include "rules/rules.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/x11/window.h"

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_window_rules-0");

class WindowRuleTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testApplyInitialMaximizeVert_data();
    void testApplyInitialMaximizeVert();
    void testWindowClassChange();
};

void WindowRuleTest::initTestCase()
{
    qRegisterMetaType<KWin::win::x11::window*>();

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

void WindowRuleTest::init()
{
    screens()->setCurrent(0);
    Cursor::setPos(QPoint(640, 512));
    QVERIFY(waylandServer()->windows.empty());
}

void WindowRuleTest::cleanup()
{
    // discards old rules
    RuleBook::self()->load();
}

struct XcbConnectionDeleter
{
    static inline void cleanup(xcb_connection_t *pointer)
    {
        xcb_disconnect(pointer);
    }
};

void WindowRuleTest::testApplyInitialMaximizeVert_data()
{
    QTest::addColumn<QByteArray>("role");

    QTest::newRow("lowercase") << QByteArrayLiteral("mainwindow");
    QTest::newRow("CamelCase") << QByteArrayLiteral("MainWindow");
}

void WindowRuleTest::testApplyInitialMaximizeVert()
{
    // this test creates the situation of BUG 367554: creates a window and initial apply maximize vertical
    // the window is matched by class and role
    // load the rule
    QFile ruleFile(QFINDTESTDATA("./data/rules/maximize-vert-apply-initial"));
    QVERIFY(ruleFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QMetaObject::invokeMethod(RuleBook::self(), "temporaryRulesMessage", Q_ARG(QString, QString::fromUtf8(ruleFile.readAll())));

    // create the test window
    QScopedPointer<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.data()));

    xcb_window_t w = xcb_generate_id(c.data());
    const QRect windowGeometry = QRect(0, 0, 10, 20);
    const uint32_t values[] = {
        XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_LEAVE_WINDOW
    };
    xcb_create_window(c.data(), XCB_COPY_FROM_PARENT, w, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, values);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.data(), w, &hints);
    xcb_icccm_set_wm_class(c.data(), w, 9, "kpat\0kpat");

    QFETCH(QByteArray, role);
    xcb_change_property(c.data(), XCB_PROP_MODE_REPLACE, w, atoms->wm_window_role, XCB_ATOM_STRING, 8, role.length(), role.constData());

    NETWinInfo info(c.data(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.data(), w);
    xcb_flush(c.data());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.last().first().value<win::x11::window*>();
    QVERIFY(client);
    QVERIFY(win::decoration(client));
    QVERIFY(!client->hasStrut());
    QVERIFY(!client->isHiddenInternal());
    QTRY_VERIFY(client->readyForPainting());
    if (!client->surface()) {
        QSignalSpy surfaceChangedSpy(client, &Toplevel::surfaceChanged);
        QVERIFY(surfaceChangedSpy.isValid());
        QVERIFY(surfaceChangedSpy.wait());
    }
    QVERIFY(client->surface());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::vertical);

    // destroy window again
    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.data(), w);
    xcb_destroy_window(c.data(), w);
    xcb_flush(c.data());
    QVERIFY(windowClosedSpy.wait());
}

void WindowRuleTest::testWindowClassChange()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("General").writeEntry("count", 1);

    auto group = config->group("1");
    group.writeEntry("above", true);
    group.writeEntry("aboverule", 2);
    group.writeEntry("wmclass", "org.kde.foo");
    group.writeEntry("wmclasscomplete", false);
    group.writeEntry("wmclassmatch", 1);
    group.sync();

    RuleBook::self()->setConfig(config);
    workspace()->slotReconfigure();

    // create the test window
    QScopedPointer<xcb_connection_t, XcbConnectionDeleter> c(xcb_connect(nullptr, nullptr));
    QVERIFY(!xcb_connection_has_error(c.data()));

    xcb_window_t w = xcb_generate_id(c.data());
    const QRect windowGeometry = QRect(0, 0, 10, 20);
    const uint32_t values[] = {
        XCB_EVENT_MASK_ENTER_WINDOW |
        XCB_EVENT_MASK_LEAVE_WINDOW
    };
    xcb_create_window(c.data(), XCB_COPY_FROM_PARENT, w, rootWindow(),
                      windowGeometry.x(),
                      windowGeometry.y(),
                      windowGeometry.width(),
                      windowGeometry.height(),
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, values);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.data(), w, &hints);
    xcb_icccm_set_wm_class(c.data(), w, 23, "org.kde.bar\0org.kde.bar");

    NETWinInfo info(c.data(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Normal);
    xcb_map_window(c.data(), w);
    xcb_flush(c.data());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.last().first().value<win::x11::window*>();
    QVERIFY(client);
    QVERIFY(win::decoration(client));
    QVERIFY(!client->hasStrut());
    QVERIFY(!client->isHiddenInternal());
    QVERIFY(!client->readyForPainting());
    QTRY_VERIFY(client->readyForPainting());
    if (!client->surface()) {
        QSignalSpy surfaceChangedSpy(client, &Toplevel::surfaceChanged);
        QVERIFY(surfaceChangedSpy.isValid());
        QVERIFY(surfaceChangedSpy.wait());
    }
    QVERIFY(client->surface());
    QCOMPARE(client->control->keep_above(), false);

    // now change class
    QSignalSpy windowClassChangedSpy{client, &win::x11::window::windowClassChanged};
    QVERIFY(windowClassChangedSpy.isValid());
    xcb_icccm_set_wm_class(c.data(), w, 23, "org.kde.foo\0org.kde.foo");
    xcb_flush(c.data());
    QVERIFY(windowClassChangedSpy.wait());
    QCOMPARE(client->control->keep_above(), true);

    // destroy window
    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    xcb_unmap_window(c.data(), w);
    xcb_destroy_window(c.data(), w);
    xcb_flush(c.data());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::WindowRuleTest)
#include "window_rules_test.moc"
