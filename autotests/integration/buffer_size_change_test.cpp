/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#include "generic_scene_opengl_test.h"

#include "composite.h"
#include "wayland_server.h"

#include "win/wayland/window.h"

#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/subsurface.h>
#include <Wrapland/Client/surface.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_buffer_size_change-0");

class BufferSizeChangeTest : public GenericSceneOpenGLTest
{
    Q_OBJECT
public:
    BufferSizeChangeTest() : GenericSceneOpenGLTest(QByteArrayLiteral("O2")) {}
private Q_SLOTS:
    void init();
    void testShmBufferSizeChange();
    void testShmBufferSizeChangeOnSubSurface();
};

void BufferSizeChangeTest::init()
{
    Test::setupWaylandConnection();
}

void BufferSizeChangeTest::testShmBufferSizeChange()
{
    // This test verifies that an SHM buffer size change is handled correctly

    using namespace Wrapland::Client;

    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());

    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());

    // set buffer size
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(client);

    // add a first repaint
    Compositor::self()->addRepaintFull();

    // now change buffer size
    Test::render(surface.data(), QSize(30, 10), Qt::red);

    QSignalSpy damagedSpy(client, &win::wayland::window::damaged);
    QVERIFY(damagedSpy.isValid());
    QVERIFY(damagedSpy.wait());
    KWin::Compositor::self()->addRepaintFull();
}

void BufferSizeChangeTest::testShmBufferSizeChangeOnSubSurface()
{
    using namespace Wrapland::Client;

    // setup parent surface
    QScopedPointer<Surface> parentSurface(Test::createSurface());
    QVERIFY(!parentSurface.isNull());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(parentSurface.data()));
    QVERIFY(!shellSurface.isNull());

    // setup sub surface
    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<SubSurface> subSurface(Test::createSubSurface(surface.data(), parentSurface.data()));
    QVERIFY(!subSurface.isNull());

    // set buffer sizes
    Test::render(surface.data(), QSize(30, 10), Qt::red);
    auto parent = Test::renderAndWaitForShown(parentSurface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(parent);

    // add a first repaint
    Compositor::self()->addRepaintFull();

    // change buffer size of sub surface
    QSignalSpy damagedParentSpy(parent, &win::wayland::window::damaged);
    QVERIFY(damagedParentSpy.isValid());
    Test::render(surface.data(), QSize(20, 10), Qt::red);
    parentSurface->commit(Surface::CommitFlag::None);

    QVERIFY(damagedParentSpy.count() == 1 || damagedParentSpy.wait());
    QCOMPARE(damagedParentSpy.count(), 1);

    // add a second repaint
    KWin::Compositor::self()->addRepaintFull();
}

}

WAYLANDTEST_MAIN(KWin::BufferSizeChangeTest)
#include "buffer_size_change_test.moc"
