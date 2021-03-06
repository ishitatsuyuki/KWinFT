/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2019 David Edmundson <davidedmundson@kde.org>

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
#include "decorations/decorationbridge.h"
#include "decorations/settings.h"
#include "effects.h"
#include "platform.h"
#include "screens.h"
#include "wayland_server.h"
#include "win/control.h"
#include "win/meta.h"
#include "win/transient.h"
#include "workspace.h"

#include "win/controlling.h"
#include "win/screen.h"
#include "win/wayland/window.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/output.h>
#include <Wrapland/Client/subsurface.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Client/appmenu.h>

#include <Wrapland/Server/client.h>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/xdg_decoration.h>

#include <QDBusConnection>

// system
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <csignal>

using namespace KWin;
using namespace Wrapland::Client;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_xdgshellclient-0");

class TestXdgShellClient : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMapUnmapMap();
    void testDesktopPresenceChanged();
    void testTransientPositionAfterRemap();
    void testWindowOutputs();
    void testMinimizeActiveWindow();
    void testFullscreen_data();
    void testFullscreen();

    void testFullscreenRestore();
    void testUserCanSetFullscreen();
    void testUserSetFullscreen_data();
    void testUserSetFullscreen();

    void testMaximizedToFullscreen_data();
    void testMaximizedToFullscreen();
    void testWindowOpensLargerThanScreen();
    void testHidden();
    void testDesktopFileName();
    void testCaptionSimplified();
    void testCaptionMultipleWindows();
    void testUnresponsiveWindow_data();
    void testUnresponsiveWindow();
    void testX11WindowId();
    void testAppMenu();
    void testNoDecorationModeRequested();
    void testSendClientWithTransientToDesktop();
    void testMinimizeWindowWithTransients();
    void testXdgDecoration_data();
    void testXdgDecoration();
    void testXdgNeverCommitted();
    void testXdgInitialState();
    void testXdgInitiallyMaximised();
    void testXdgInitiallyFullscreen();
    void testXdgInitiallyMinimized();
    void testXdgWindowGeometryIsntSet();
    void testXdgWindowGeometryAttachBuffer();
    void testSendToScreen();
    void testXdgWindowGeometryAttachSubSurface();
    void testXdgWindowGeometryInteractiveResize();
    void testXdgWindowGeometryFullScreen();
    void testXdgWindowGeometryMaximize();
    void test_multi_maximize();
};

void TestXdgShellClient::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<Wrapland::Client::Output*>();

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

void TestXdgShellClient::init()
{
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::XdgDecoration |
                                 Test::AdditionalWaylandInterface::AppMenu);

    screens()->setCurrent(0);
    KWin::Cursor::setPos(QPoint(1280, 512));
}

void TestXdgShellClient::cleanup()
{
    Test::destroyWaylandConnection();
}

void TestXdgShellClient::testMapUnmapMap()
{
    // this test verifies that mapping a previously mapped window works correctly
    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::window_added);
    QVERIFY(clientAddedSpy.isValid());
    QSignalSpy effectsWindowShownSpy(effects, &EffectsHandler::windowShown);
    QVERIFY(effectsWindowShownSpy.isValid());
    QSignalSpy effectsWindowHiddenSpy(effects, &EffectsHandler::windowHidden);
    QVERIFY(effectsWindowHiddenSpy.isValid());

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));

    // now let's render
    Test::render(surface.data(), QSize(100, 50), Qt::blue);

    QVERIFY(clientAddedSpy.isEmpty());
    QVERIFY(clientAddedSpy.wait());
    auto client = clientAddedSpy.first().first().value<win::wayland::window*>();
    QVERIFY(client);
    QVERIFY(client->isShown());
    QCOMPARE(client->isHiddenInternal(), false);
    QCOMPARE(client->readyForPainting(), true);
    QCOMPARE(client->depth(), 32);
    QVERIFY(client->hasAlpha());
    QCOMPARE(client->control->icon().name(), QStringLiteral("wayland"));
    QCOMPARE(workspace()->activeClient(), client);
    QVERIFY(effectsWindowShownSpy.isEmpty());
    QVERIFY(client->isMaximizable());
    QVERIFY(client->isMovable());
    QVERIFY(client->isMovableAcrossScreens());
    QVERIFY(client->isResizable());
    QCOMPARE(client->isInternal(), false);
    QVERIFY(client->effectWindow());
    QVERIFY(!client->effectWindow()->internalWindow());
    QCOMPARE(client->internalId().isNull(), false);
    const auto uuid = client->internalId();
    QUuid deletedUuid;
    QCOMPARE(deletedUuid.isNull(), true);

    connect(client, &win::wayland::window::windowClosed, this, [&deletedUuid] (Toplevel*, Toplevel* d)
        { deletedUuid = d->internalId(); });

    // now unmap
    QSignalSpy hiddenSpy(client, &win::wayland::window::windowHidden);
    QVERIFY(hiddenSpy.isValid());
    QSignalSpy windowClosedSpy(client, &win::wayland::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(hiddenSpy.wait());
    QCOMPARE(client->readyForPainting(), true);
    QCOMPARE(client->isHiddenInternal(), true);
    QVERIFY(windowClosedSpy.isEmpty());
    QVERIFY(!workspace()->activeClient());
    QCOMPARE(effectsWindowHiddenSpy.count(), 1);
    QCOMPARE(effectsWindowHiddenSpy.first().first().value<EffectWindow*>(), client->effectWindow());

    QSignalSpy windowShownSpy(client, &win::wayland::window::windowShown);
    QVERIFY(windowShownSpy.isValid());
    Test::render(surface.data(), QSize(100, 50), Qt::blue, QImage::Format_RGB32);
    QCOMPARE(clientAddedSpy.count(), 1);
    QVERIFY(windowShownSpy.wait());
    QCOMPARE(windowShownSpy.count(), 1);
    QCOMPARE(clientAddedSpy.count(), 1);
    QCOMPARE(client->readyForPainting(), true);
    QCOMPARE(client->isHiddenInternal(), false);
    QCOMPARE(client->depth(), 24);
    QVERIFY(!client->hasAlpha());
    QCOMPARE(workspace()->activeClient(), client);
    QCOMPARE(effectsWindowShownSpy.count(), 1);
    QCOMPARE(effectsWindowShownSpy.first().first().value<EffectWindow*>(), client->effectWindow());

    // let's unmap again
    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(hiddenSpy.wait());
    QCOMPARE(hiddenSpy.count(), 2);
    QCOMPARE(client->readyForPainting(), true);
    QCOMPARE(client->isHiddenInternal(), true);
    QCOMPARE(client->internalId(), uuid);
    QVERIFY(windowClosedSpy.isEmpty());
    QCOMPARE(effectsWindowHiddenSpy.count(), 2);
    QCOMPARE(effectsWindowHiddenSpy.last().first().value<EffectWindow*>(), client->effectWindow());

    shellSurface.reset();
    surface.reset();
    QVERIFY(windowClosedSpy.wait());
    QCOMPARE(windowClosedSpy.count(), 1);
    QCOMPARE(effectsWindowHiddenSpy.count(), 2);
    QCOMPARE(deletedUuid.isNull(), false);
    QCOMPARE(deletedUuid, uuid);
}

void TestXdgShellClient::testDesktopPresenceChanged()
{
    // this test verifies that the desktop presence changed signals are properly emitted
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(c->desktop(), 1);
    effects->setNumberOfDesktops(4);
    QSignalSpy desktopPresenceChangedClientSpy(c, &win::wayland::window::desktopPresenceChanged);
    QVERIFY(desktopPresenceChangedClientSpy.isValid());
    QSignalSpy desktopPresenceChangedWorkspaceSpy(workspace(), &Workspace::desktopPresenceChanged);
    QVERIFY(desktopPresenceChangedWorkspaceSpy.isValid());
    QSignalSpy desktopPresenceChangedEffectsSpy(effects, &EffectsHandler::desktopPresenceChanged);
    QVERIFY(desktopPresenceChangedEffectsSpy.isValid());

    // let's change the desktop
    workspace()->sendClientToDesktop(c, 2, false);
    QCOMPARE(c->desktop(), 2);
    QCOMPARE(desktopPresenceChangedClientSpy.count(), 1);
    QCOMPARE(desktopPresenceChangedWorkspaceSpy.count(), 1);
    QCOMPARE(desktopPresenceChangedEffectsSpy.count(), 1);

    // verify the arguments
    QCOMPARE(desktopPresenceChangedClientSpy.first().at(0).value<Toplevel*>(), c);
    QCOMPARE(desktopPresenceChangedClientSpy.first().at(1).toInt(), 1);
    QCOMPARE(desktopPresenceChangedWorkspaceSpy.first().at(0).value<Toplevel*>(), c);
    QCOMPARE(desktopPresenceChangedWorkspaceSpy.first().at(1).toInt(), 1);
    QCOMPARE(desktopPresenceChangedEffectsSpy.first().at(0).value<EffectWindow*>(), c->effectWindow());
    QCOMPARE(desktopPresenceChangedEffectsSpy.first().at(1).toInt(), 1);
    QCOMPARE(desktopPresenceChangedEffectsSpy.first().at(2).toInt(), 2);
}

void TestXdgShellClient::testTransientPositionAfterRemap()
{
    // this test simulates the situation that a transient window gets reused and the parent window
    // moved between the two usages
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // create the Transient window
    XdgPositioner positioner(QSize(50, 40), QRect(0, 0, 5, 10));
    positioner.setAnchorEdge(Qt::BottomEdge | Qt::RightEdge);
    positioner.setGravity(Qt::BottomEdge | Qt::RightEdge);
    QScopedPointer<Surface> transientSurface(Test::createSurface());
    QScopedPointer<XdgShellPopup> transientShellSurface(Test::create_xdg_shell_popup(transientSurface.data(), shellSurface.data(), positioner));
    auto transient = Test::renderAndWaitForShown(transientSurface.data(), positioner.initialSize(), Qt::blue);
    QVERIFY(transient);
    QCOMPARE(transient->frameGeometry(), QRect(c->frameGeometry().topLeft() + QPoint(5, 10), QSize(50, 40)));

    // unmap the transient
    QSignalSpy windowHiddenSpy(transient, &win::wayland::window::windowHidden);
    QVERIFY(windowHiddenSpy.isValid());
    transientSurface->attachBuffer(Buffer::Ptr());
    transientSurface->commit(Surface::CommitFlag::None);
    QVERIFY(windowHiddenSpy.wait());

    // now move the parent surface
    c->setFrameGeometry(c->frameGeometry().translated(5, 10));

    // now map the transient again
    QSignalSpy windowShownSpy(transient, &win::wayland::window::windowShown);
    QVERIFY(windowShownSpy.isValid());
    Test::render(transientSurface.data(), QSize(50, 40), Qt::blue);
    QVERIFY(windowShownSpy.wait());

    QCOMPARE(transient->frameGeometry(), QRect(c->frameGeometry().topLeft() + QPoint(5, 10), QSize(50, 40)));
}

void TestXdgShellClient::testWindowOutputs()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto size = QSize(200,200);

    QSignalSpy outputEnteredSpy(surface.data(), &Surface::outputEntered);
    QSignalSpy outputLeftSpy(surface.data(), &Surface::outputLeft);

    auto c = Test::renderAndWaitForShown(surface.data(), size, Qt::blue);
    //move to be in the first screen
    c->setFrameGeometry(QRect(QPoint(100,100), size));
    //we don't don't know where the compositor first placed this window,
    //this might fire, it might not
    outputEnteredSpy.wait(5);
    outputEnteredSpy.clear();

    QCOMPARE(surface->outputs().count(), 1);
    QCOMPARE(surface->outputs().first()->globalPosition(), QPoint(0,0));

    //move to overlapping both first and second screen
    c->setFrameGeometry(QRect(QPoint(1250,100), size));
    QVERIFY(outputEnteredSpy.wait());
    QCOMPARE(outputEnteredSpy.count(), 1);
    QCOMPARE(outputLeftSpy.count(), 0);
    QCOMPARE(surface->outputs().count(), 2);
    QVERIFY(surface->outputs()[0] != surface->outputs()[1]);

    //move entirely into second screen
    c->setFrameGeometry(QRect(QPoint(1400,100), size));
    QVERIFY(outputLeftSpy.wait());
    QCOMPARE(outputEnteredSpy.count(), 1);
    QCOMPARE(outputLeftSpy.count(), 1);
    QCOMPARE(surface->outputs().count(), 1);
    QCOMPARE(surface->outputs().first()->globalPosition(), QPoint(1280,0));
}

void TestXdgShellClient::testMinimizeActiveWindow()
{
    // this test verifies that when minimizing the active window it gets deactivated
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<QObject> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active());
    QCOMPARE(workspace()->activeClient(), c);
    QVERIFY(c->wantsInput());
    QVERIFY(win::wants_tab_focus(c));
    QVERIFY(c->isShown());

    workspace()->slotWindowMinimize();
    QVERIFY(!c->isShown());
    QVERIFY(c->wantsInput());
    QVERIFY(win::wants_tab_focus(c));
    QVERIFY(!c->control->active());
    QVERIFY(!workspace()->activeClient());
    QVERIFY(c->control->minimized());

    // unminimize again
    win::set_minimized(c, false);
    QVERIFY(!c->control->minimized());
    QVERIFY(c->control->active());
    QVERIFY(c->wantsInput());
    QVERIFY(win::wants_tab_focus(c));
    QVERIFY(c->isShown());
    QCOMPARE(workspace()->activeClient(), c);
}

void TestXdgShellClient::testFullscreen_data()
{
    QTest::addColumn<XdgDecoration::Mode>("decoMode");

    QTest::newRow("client-deco") << XdgDecoration::Mode::ClientSide;
    QTest::newRow("server-deco") << XdgDecoration::Mode::ServerSide;
}

void TestXdgShellClient::testFullscreen()
{
    // this test verifies that a window can be properly fullscreened
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr,
                                                                                 Test::CreationSetup::CreateOnly));
    QVERIFY(shellSurface);

    // create deco
    auto deco = Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data(), shellSurface.data());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    QVERIFY(decoSpy.isValid());
    QFETCH(XdgDecoration::Mode, decoMode);
    deco->setMode(decoMode);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
    Test::init_xdg_shell_toplevel(surface.data(), shellSurface.data());
    QCOMPARE(deco->mode(), decoMode);

    QSignalSpy sizeChangeRequestedSpy(shellSurface.data(), &XdgShellToplevel::sizeChanged);
    QVERIFY(sizeChangeRequestedSpy.isValid());

    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());

    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active());
    QCOMPARE(c->layer(), win::layer::normal);
    QVERIFY(!c->control->fullscreen());
    QCOMPARE(win::frame_to_client_size(c, c->size()), QSize(100, 50));
    QCOMPARE(win::decoration(c) != nullptr, decoMode == XdgDecoration::Mode::ServerSide);
    QCOMPARE(win::client_to_frame_size(c, win::frame_to_client_size(c, c->size())), c->frameGeometry().size());

    QVERIFY(sizeChangeRequestedSpy.wait());
    QCOMPARE(sizeChangeRequestedSpy.count(), 1);
    QCOMPARE(configureRequestedSpy.count(), 1);
    QCOMPARE(sizeChangeRequestedSpy.first().first().toSize(), QSize(100, 50));

    QSignalSpy fullscreenChangedSpy(c, &win::wayland::window::fullScreenChanged);
    QVERIFY(fullscreenChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(c, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    shellSurface->setFullscreen(true);

    // After round-trip the server configures the window to the size of the screen.
    QVERIFY(sizeChangeRequestedSpy.wait());
    QCOMPARE(sizeChangeRequestedSpy.count(), 2);
    QCOMPARE(sizeChangeRequestedSpy.last().first().toSize(), screens()->size(0));

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), sizeChangeRequestedSpy.last().first().toSize(), Qt::red);

    // This is the server-side change.
    QVERIFY(fullscreenChangedSpy.wait());

    QVERIFY(c->control->fullscreen());
    QVERIFY(!win::decoration(c));
    QCOMPARE(win::frame_to_client_size(c, c->size()), screens()->size(0));
    QVERIFY(!geometryChangedSpy.isEmpty());

    QVERIFY(c->control->fullscreen());
    QVERIFY(!win::decoration(c));
    QCOMPARE(c->frameGeometry(), QRect(QPoint(0, 0), sizeChangeRequestedSpy.last().first().toSize()));
    QCOMPARE(c->layer(), win::layer::active);

    // swap back to normal
    shellSurface->setFullscreen(false);

    QVERIFY(sizeChangeRequestedSpy.wait());
    QCOMPARE(sizeChangeRequestedSpy.count(), 3);
    QCOMPARE(sizeChangeRequestedSpy.last().first().toSize(), QSize(100, 50));

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), sizeChangeRequestedSpy.last().first().toSize(), Qt::red);

    QVERIFY(fullscreenChangedSpy.wait());

    QVERIFY(!c->control->fullscreen());
    QCOMPARE(c->layer(), win::layer::normal);
    QCOMPARE(win::decoration(c) != nullptr, decoMode == XdgDecoration::Mode::ServerSide);
}

void TestXdgShellClient::testFullscreenRestore()
{
    // this test verifies that windows created fullscreen can be later properly restored
    QScopedPointer<Surface> surface(Test::createSurface());
    auto shell_surface = Test::create_xdg_shell_toplevel(surface.data(), surface.data(),
                                                                       Test::CreationSetup::CreateOnly);
    QSignalSpy configureRequestedSpy(shell_surface, &XdgShellToplevel::configureRequested);

    // fullscreen the window
    shell_surface->setFullscreen(true);
    surface->commit(Surface::CommitFlag::None);

    configureRequestedSpy.wait();
    QCOMPARE(configureRequestedSpy.count(), 1);

    const auto size = configureRequestedSpy.first()[0].value<QSize>();
    const auto state = configureRequestedSpy.first()[1].value<Wrapland::Client::XdgShellToplevel::States>();

    QCOMPARE(size, screens()->size(0));
    QVERIFY(state & Wrapland::Client::XdgShellToplevel::State::Fullscreen);
    shell_surface->ackConfigure(configureRequestedSpy.first()[2].toUInt());

    auto c = Test::renderAndWaitForShown(surface.data(), size, Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->fullscreen());

    configureRequestedSpy.wait(100);

    QSignalSpy fullscreenChangedSpy(c, &win::wayland::window::fullScreenChanged);
    QVERIFY(fullscreenChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(c, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    // swap back to normal
    configureRequestedSpy.clear();
    shell_surface->setFullscreen(false);

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.last().first().toSize(), QSize(0, 0));

    for (const auto &it: configureRequestedSpy) {
        shell_surface->ackConfigure(it[2].toUInt());
    }

    Test::render(surface.data(), QSize(100, 50), Qt::red);

    QVERIFY(fullscreenChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QVERIFY(!c->control->fullscreen());
    QCOMPARE(c->frameGeometry().size(), QSize(100, 50));
}

void TestXdgShellClient::testUserCanSetFullscreen()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active());
    QVERIFY(!c->control->fullscreen());
    QVERIFY(c->userCanSetFullScreen());
}

void TestXdgShellClient::testUserSetFullscreen_data()
{
    QTest::addColumn<bool>("send_fs_geo");

    QTest::newRow("send fs-image") << true;
    QTest::newRow("no send fs-image") << false;
}

void TestXdgShellClient::testUserSetFullscreen()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(
        surface.data(), surface.data(), Test::CreationSetup::CreateOnly));
    QVERIFY(!shellSurface.isNull());

    // wait for the initial configure event
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active());
    QVERIFY(!c->control->fullscreen());

    // The client gets activated, which gets another configure event. Though that's not relevant to the test
    configureRequestedSpy.wait(10);

    QSignalSpy fullscreenChangedSpy(c, &Toplevel::fullScreenChanged);
    QVERIFY(fullscreenChangedSpy.isValid());

    c->setFullScreen(true);

    QVERIFY(c->geometry_update.fullscreen);
    QVERIFY(!c->control->fullscreen());

    QTRY_COMPARE(configureRequestedSpy.count(), 3);
    QCOMPARE(configureRequestedSpy.at(2).at(0).toSize(), screens()->size(0));

    const auto states = configureRequestedSpy.at(2).at(1).value<Wrapland::Client::XdgShellToplevel::States>();
    QVERIFY(states.testFlag(Wrapland::Client::XdgShellToplevel::State::Fullscreen));
    QVERIFY(states.testFlag(Wrapland::Client::XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(Wrapland::Client::XdgShellToplevel::State::Maximized));
    QVERIFY(!states.testFlag(Wrapland::Client::XdgShellToplevel::State::Resizing));

    shellSurface->ackConfigure(configureRequestedSpy.at(2).at(2).value<quint32>());

    QSignalSpy geometry_spy(c, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometry_spy.isValid());

    QFETCH(bool, send_fs_geo);
    if (send_fs_geo) {
        Test::render(surface.data(), screens()->size(0), Qt::green);
    }

    QCOMPARE(geometry_spy.wait(100), send_fs_geo);
    QCOMPARE(fullscreenChangedSpy.count(), send_fs_geo);
    QCOMPARE(c->control->fullscreen(), send_fs_geo);

    configureRequestedSpy.clear();

    // unset fullscreen again
    c->setFullScreen(false);

    QVERIFY(!c->geometry_update.fullscreen);
    QCOMPARE(c->control->fullscreen(), send_fs_geo);

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QCOMPARE(configureRequestedSpy.first().at(0).toSize(), QSize(100, 50));
    QVERIFY(!configureRequestedSpy.first().at(1).value<Wrapland::Client::XdgShellToplevel::States>().testFlag(Wrapland::Client::XdgShellToplevel::State::Fullscreen));

    shellSurface->ackConfigure(configureRequestedSpy.first().at(2).value<quint32>());

    Test::render(surface.data(), configureRequestedSpy.first().at(0).toSize(), Qt::red);
    QCOMPARE(geometry_spy.wait(100), send_fs_geo);

    QCOMPARE(fullscreenChangedSpy.count(), send_fs_geo ? 2 : 0);
    QVERIFY(!c->control->fullscreen());
}

void TestXdgShellClient::testMaximizedToFullscreen_data()
{
    QTest::addColumn<XdgDecoration::Mode>("decoMode");

    QTest::newRow("no deco") << XdgDecoration::Mode::ClientSide;
    QTest::newRow("deco") << XdgDecoration::Mode::ServerSide;
}

void TestXdgShellClient::testMaximizedToFullscreen()
{
    // this test verifies that a window can be properly fullscreened after maximizing
    QScopedPointer<Wrapland::Client::Surface> surface(Test::createSurface());
    QScopedPointer<Wrapland::Client::XdgShellToplevel>
        shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr,
                                                     Test::CreationSetup::CreateOnly));
    QVERIFY(shellSurface);

    // create deco
    auto deco = Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data(), shellSurface.data());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    QVERIFY(decoSpy.isValid());
    QFETCH(XdgDecoration::Mode, decoMode);
    deco->setMode(decoMode);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
    Test::init_xdg_shell_toplevel(surface.data(), shellSurface.data());
    QCOMPARE(deco->mode(), decoMode);

    auto const has_ssd = decoMode == XdgDecoration::Mode::ServerSide;

    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active());
    QVERIFY(!client->control->fullscreen());
    QCOMPARE(win::frame_to_client_size(client, client->size()), QSize(100, 50));
    QCOMPARE(win::decoration(client) != nullptr, has_ssd);

    QSignalSpy fullscreenChangedSpy(client, &win::wayland::window::fullScreenChanged);
    QVERIFY(fullscreenChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(client, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    QSignalSpy sizeChangeRequestedSpy(shellSurface.data(), &XdgShellToplevel::sizeChanged);
    QVERIFY(sizeChangeRequestedSpy.isValid());
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());

    QSignalSpy maximize_spy(client,
        qOverload<Toplevel*, bool, bool>(&win::wayland::window::clientMaximizedStateChanged));

    QVERIFY(sizeChangeRequestedSpy.wait());
    QCOMPARE(sizeChangeRequestedSpy.count(), 1);

    shellSurface->setMaximized(true);

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());

    Test::render(surface.data(), sizeChangeRequestedSpy.last().first().toSize(), Qt::red);

    QVERIFY(sizeChangeRequestedSpy.wait());
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());

    Test::render(surface.data(), sizeChangeRequestedSpy.last().first().toSize(), Qt::red);

    maximize_spy.wait();

    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(geometryChangedSpy.isEmpty(), false);
    geometryChangedSpy.clear();
    configureRequestedSpy.clear();

    // Fullscreen the window client-side.
    shellSurface->setFullscreen(true);

    // Server sends a configure request with or without SSD so client can adapt window geometry.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    // With or without deco on fullscreen clients will be requested to provide the screeen size.
    QCOMPARE(configureRequestedSpy.last().first().toSize(), screens()->size(0));

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), sizeChangeRequestedSpy.last().first().toSize(), Qt::red);

    // Receive request server-side.
    QVERIFY(fullscreenChangedSpy.wait());
    QVERIFY(client->control->fullscreen());

    QVERIFY(client->control->fullscreen());
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->frameGeometry(), QRect(QPoint(0, 0), sizeChangeRequestedSpy.last().first().toSize()));
    sizeChangeRequestedSpy.clear();

    QCOMPARE(configureRequestedSpy.count(), 1);

    // swap back to normal
    shellSurface->setFullscreen(false);
    shellSurface->setMaximized(false);

    QVERIFY(configureRequestedSpy.wait());
    QTRY_COMPARE(configureRequestedSpy.count(), 3);

    if (decoMode == XdgDecoration::Mode::ServerSide) {
         QCOMPARE(sizeChangeRequestedSpy.count(), 2);
         QCOMPARE(sizeChangeRequestedSpy.last().first().toSize(), QSize(100, 50));
    }

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), configureRequestedSpy.last().first().toSize(), Qt::red);

    QVERIFY(fullscreenChangedSpy.wait());

    QVERIFY(!client->control->fullscreen());
    QCOMPARE(win::decoration(client) != nullptr, decoMode == XdgDecoration::Mode::ServerSide);
}

void TestXdgShellClient::testWindowOpensLargerThanScreen()
{
    // this test creates a window which is as large as the screen, but is decorated
    // the window should get resized to fit into the screen, BUG: 366632
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr,
                                                                                 Test::CreationSetup::CreateOnly));
    QSignalSpy sizeChangeRequestedSpy(shellSurface.data(), &XdgShellToplevel::sizeChanged);
    QVERIFY(sizeChangeRequestedSpy.isValid());

    // create deco
    auto deco = Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data(), shellSurface.data());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    QVERIFY(decoSpy.isValid());
    deco->setMode(XdgDecoration::Mode::ServerSide);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
    Test::init_xdg_shell_toplevel(surface.data(), shellSurface.data());
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

    auto c = Test::renderAndWaitForShown(surface.data(), screens()->size(0), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active());
    QCOMPARE(win::frame_to_client_size(c, c->size()), screens()->size(0));
    QVERIFY(win::decoration(c));
    QVERIFY(sizeChangeRequestedSpy.wait());
}

void TestXdgShellClient::testHidden()
{
    // this test verifies that when hiding window it doesn't get shown
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<QObject> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->control->active());
    QCOMPARE(workspace()->activeClient(), c);
    QVERIFY(c->wantsInput());
    QVERIFY(win::wants_tab_focus(c));
    QVERIFY(c->isShown());

    c->hideClient(true);
    QVERIFY(!c->isShown());
    QVERIFY(!c->control->active());
    QVERIFY(c->wantsInput());
    QVERIFY(win::wants_tab_focus(c));

    // unhide again
    c->hideClient(false);
    QVERIFY(c->isShown());
    QVERIFY(c->wantsInput());
    QVERIFY(win::wants_tab_focus(c));

    //QCOMPARE(workspace()->activeClient(), c);
}

void TestXdgShellClient::testDesktopFileName()
{
    QIcon::setThemeName(QStringLiteral("breeze"));
    // this test verifies that desktop file name is passed correctly to the window
    QScopedPointer<Surface> surface(Test::createSurface());
    // only xdg-shell as ShellSurface misses the setter
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    shellSurface->setAppId(QByteArrayLiteral("org.kde.foo"));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(c->control->desktop_file_name(), QByteArrayLiteral("org.kde.foo"));
    QCOMPARE(c->resourceClass(), QByteArrayLiteral("org.kde.foo"));
    QVERIFY(c->resourceName().startsWith("testXdgShellClient"));
    // the desktop file does not exist, so icon should be generic Wayland
    QCOMPARE(c->control->icon().name(), QStringLiteral("wayland"));

    QSignalSpy desktopFileNameChangedSpy(c, &Toplevel::desktopFileNameChanged);
    QVERIFY(desktopFileNameChangedSpy.isValid());
    QSignalSpy iconChangedSpy(c, &win::wayland::window::iconChanged);
    QVERIFY(iconChangedSpy.isValid());
    shellSurface->setAppId(QByteArrayLiteral("org.kde.bar"));
    QVERIFY(desktopFileNameChangedSpy.wait());
    QCOMPARE(c->control->desktop_file_name(), QByteArrayLiteral("org.kde.bar"));
    QCOMPARE(c->resourceClass(), QByteArrayLiteral("org.kde.bar"));
    QVERIFY(c->resourceName().startsWith("testXdgShellClient"));
    // icon should still be wayland
    QCOMPARE(c->control->icon().name(), QStringLiteral("wayland"));
    QVERIFY(iconChangedSpy.isEmpty());

    const QString dfPath = QFINDTESTDATA("data/example.desktop");
    shellSurface->setAppId(dfPath.toUtf8());
    QVERIFY(desktopFileNameChangedSpy.wait());
    QCOMPARE(iconChangedSpy.count(), 1);
    QCOMPARE(QString::fromUtf8(c->control->desktop_file_name()), dfPath);
    QCOMPARE(c->control->icon().name(), QStringLiteral("kwin"));
}

void TestXdgShellClient::testCaptionSimplified()
{
    // this test verifies that caption is properly trimmed
    // see BUG 323798 comment #12
    QScopedPointer<Surface> surface(Test::createSurface());
    // only done for xdg-shell as ShellSurface misses the setter
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    const QString origTitle = QString::fromUtf8(QByteArrayLiteral("Was tun, wenn Schüler Autismus haben?\342\200\250\342\200\250\342\200\250 – Marlies Hübner - Mozilla Firefox"));
    shellSurface->setTitle(origTitle);
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(win::caption(c) != origTitle);
    QCOMPARE(win::caption(c), origTitle.simplified());
}

void TestXdgShellClient::testCaptionMultipleWindows()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    shellSurface->setTitle(QStringLiteral("foo"));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(win::caption(c), QStringLiteral("foo"));
    QCOMPARE(c->caption.normal, QStringLiteral("foo"));
    QCOMPARE(c->caption.suffix, QString());

    QScopedPointer<Surface> surface2(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2.data()));
    shellSurface2->setTitle(QStringLiteral("foo"));
    auto c2 = Test::renderAndWaitForShown(surface2.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c2);
    QCOMPARE(win::caption(c2), QStringLiteral("foo <2>"));
    QCOMPARE(c2->caption.normal, QStringLiteral("foo"));
    QCOMPARE(c2->caption.suffix, QStringLiteral(" <2>"));

    QScopedPointer<Surface> surface3(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3.data()));
    shellSurface3->setTitle(QStringLiteral("foo"));
    auto c3 = Test::renderAndWaitForShown(surface3.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c3);
    QCOMPARE(win::caption(c3), QStringLiteral("foo <3>"));
    QCOMPARE(c3->caption.normal, QStringLiteral("foo"));
    QCOMPARE(c3->caption.suffix, QStringLiteral(" <3>"));

    QScopedPointer<Surface> surface4(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4.data()));
    shellSurface4->setTitle(QStringLiteral("bar"));
    auto c4 = Test::renderAndWaitForShown(surface4.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c4);
    QCOMPARE(win::caption(c4), QStringLiteral("bar"));
    QCOMPARE(c4->caption.normal, QStringLiteral("bar"));
    QCOMPARE(c4->caption.suffix, QString());
    QSignalSpy captionChangedSpy(c4, &win::wayland::window::captionChanged);
    QVERIFY(captionChangedSpy.isValid());
    shellSurface4->setTitle(QStringLiteral("foo"));
    QVERIFY(captionChangedSpy.wait());
    QCOMPARE(captionChangedSpy.count(), 1);
    QCOMPARE(win::caption(c4), QStringLiteral("foo <4>"));
    QCOMPARE(c4->caption.normal, QStringLiteral("foo"));
    QCOMPARE(c4->caption.suffix, QStringLiteral(" <4>"));
}

void TestXdgShellClient::testUnresponsiveWindow_data()
{
    QTest::addColumn<QString>("shellInterface");//see env selection in qwaylandintegration.cpp
    QTest::addColumn<bool>("socketMode");

    QTest::newRow("xdg-shell display") << "xdg-shell" << false;
    QTest::newRow("xdg-shell socket") << "xdg-shell" << true;

    //TODO add XDG WM Base when Kwin relies on Qt 5.12
}

void TestXdgShellClient::testUnresponsiveWindow()
{
    // this test verifies that killWindow properly terminates a process
    // for this an external binary is launched
    const QString kill = QFINDTESTDATA(QStringLiteral("kill"));
    QVERIFY(!kill.isEmpty());
    QSignalSpy shellClientAddedSpy(waylandServer(), &WaylandServer::window_added);
    QVERIFY(shellClientAddedSpy.isValid());

    QScopedPointer<QProcess> process(new QProcess);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    QFETCH(QString, shellInterface);
    QFETCH(bool, socketMode);
    env.insert("QT_WAYLAND_SHELL_INTEGRATION", shellInterface);
    if (socketMode) {
        int sx[2];
        QVERIFY(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) >= 0);
        waylandServer()->display()->createClient(sx[0]);
        int socket = dup(sx[1]);
        QVERIFY(socket != -1);
        env.insert(QStringLiteral("WAYLAND_SOCKET"), QByteArray::number(socket));
        env.remove("WAYLAND_DISPLAY");
    } else {
        env.insert("WAYLAND_DISPLAY", s_socketName);
    }
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->setProgram(kill);
    QSignalSpy processStartedSpy{process.data(), &QProcess::started};
    QVERIFY(processStartedSpy.isValid());
    process->start();

    Toplevel* killClient = nullptr;
    QVERIFY(shellClientAddedSpy.wait());
    QCOMPARE(processStartedSpy.count(), 1);
    QCOMPARE(shellClientAddedSpy.count(), 1);

    ::kill(process->processId(), SIGUSR1); // send a signal to freeze the process

    killClient = shellClientAddedSpy.first().first().value<Toplevel*>();
    QVERIFY(killClient);
    QSignalSpy unresponsiveSpy(killClient, &Toplevel::unresponsiveChanged);
    QSignalSpy killedSpy(process.data(), static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished));
    QSignalSpy deletedSpy(killClient, &QObject::destroyed);

    qint64 startTime = QDateTime::currentMSecsSinceEpoch();

    //wait for the process to be frozen
    QTest::qWait(10);

    //pretend the user clicked the close button
    killClient->closeWindow();

    //client should not yet be marked unresponsive nor killed
    QVERIFY(!killClient->control->unresponsive());
    QVERIFY(killedSpy.isEmpty());

    QVERIFY(unresponsiveSpy.wait());
    //client should be marked unresponsive but not killed
    auto elapsed1 = QDateTime::currentMSecsSinceEpoch() - startTime;
    QVERIFY(elapsed1 > 900  && elapsed1 < 1200); //ping timer is 1s, but coarse timers on a test across two processes means we need a fuzzy compare
    QVERIFY(killClient->control->unresponsive());
    QVERIFY(killedSpy.isEmpty());

    QVERIFY(deletedSpy.wait());
    if (!socketMode) {
        //process was killed - because we're across process this could happen in either order
        QVERIFY(killedSpy.count() || killedSpy.wait());
    }

    auto elapsed2 = QDateTime::currentMSecsSinceEpoch() - startTime;
    QVERIFY(elapsed2 > 1800); //second ping comes in a second later
}

void TestXdgShellClient::testX11WindowId()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(c->windowId() != 0);
    QCOMPARE(static_cast<Toplevel*>(c)->xcb_window(), 0u);
}

void TestXdgShellClient::testAppMenu()
{
    //register a faux appmenu client
    QVERIFY (QDBusConnection::sessionBus().registerService("org.kde.kappmenu"));

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QScopedPointer<AppMenu> menu(Test::waylandAppMenuManager()->create(surface.data()));
    QSignalSpy spy(c, &win::wayland::window::hasApplicationMenuChanged);
    menu->setAddress("service.name", "object/path");
    spy.wait();
    QCOMPARE(c->control->has_application_menu(), true);
    QCOMPARE(c->control->application_menu_service_name(), QString("service.name"));
    QCOMPARE(c->control->application_menu_object_path(), QString("object/path"));

    QVERIFY (QDBusConnection::sessionBus().unregisterService("org.kde.kappmenu"));
}

void TestXdgShellClient::testNoDecorationModeRequested()
{
    // this test verifies that the decoration follows the default mode if no mode is explicitly requested
    QScopedPointer<Surface> surface(Test::createSurface());

    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr,
                                                                                 Test::CreationSetup::CreateOnly));

    auto deco = Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data(), shellSurface.data());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    QVERIFY(decoSpy.isValid());
    deco->unsetMode();
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
    Test::init_xdg_shell_toplevel(surface.data(), shellSurface.data());
    QCOMPARE(decoSpy.count(), 1);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);

    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(c->noBorder(), false);
    QVERIFY(win::decoration(c));
}

void TestXdgShellClient::testSendClientWithTransientToDesktop()
{
    // this test verifies that when sending a client to a desktop all transients are also send to that desktop

    VirtualDesktopManager::self()->setCount(2);
    QScopedPointer<Surface> surface{Test::createSurface()};
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));

    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // let's create a transient window
    QScopedPointer<Surface> transientSurface{Test::createSurface()};
    QScopedPointer<XdgShellToplevel> transientShellSurface(Test::create_xdg_shell_toplevel(transientSurface.data()));
    transientShellSurface->setTransientFor(shellSurface.data());

    auto transient = Test::renderAndWaitForShown(transientSurface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(transient);
    QCOMPARE(workspace()->activeClient(), transient);
    QCOMPARE(transient->transient()->lead(), c);
    QVERIFY(contains(c->transient()->children, transient));

    QCOMPARE(c->desktop(), 1);
    QVERIFY(!c->isOnAllDesktops());
    QCOMPARE(transient->desktop(), 1);
    QVERIFY(!transient->isOnAllDesktops());
    workspace()->slotWindowToDesktop(2);

    QCOMPARE(c->desktop(), 1);
    QCOMPARE(transient->desktop(), 2);

    // activate c
    workspace()->activateClient(c);
    QCOMPARE(workspace()->activeClient(), c);
    QVERIFY(c->control->active());

    // and send it to the desktop it's already on
    QCOMPARE(c->desktop(), 1);
    QCOMPARE(transient->desktop(), 2);
    workspace()->slotWindowToDesktop(1);

    // which should move the transient back to the desktop
    QCOMPARE(c->desktop(), 1);
    QCOMPARE(transient->desktop(), 1);
}

void TestXdgShellClient::testMinimizeWindowWithTransients()
{
    // this test verifies that when minimizing/unminimizing a window all its
    // transients will be minimized/unminimized as well

    // create the main window
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QVERIFY(!c->control->minimized());

    // create a transient window
    QScopedPointer<Surface> transientSurface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> transientShellSurface(Test::create_xdg_shell_toplevel(transientSurface.data()));
    transientShellSurface->setTransientFor(shellSurface.data());
    auto transient = Test::renderAndWaitForShown(transientSurface.data(), QSize(100, 50), Qt::red);
    QVERIFY(transient);
    QVERIFY(!transient->control->minimized());
    QCOMPARE(transient->transient()->lead(), c);
    QVERIFY(contains(c->transient()->children, transient));

    // minimize the main window, the transient should be minimized as well
    win::set_minimized(c, true);
    QVERIFY(c->control->minimized());
    QVERIFY(transient->control->minimized());

    // unminimize the main window, the transient should be unminimized as well
    win::set_minimized(c, false);
    QVERIFY(!c->control->minimized());
    QVERIFY(!transient->control->minimized());
}

void TestXdgShellClient::testXdgDecoration_data()
{
    QTest::addColumn<Wrapland::Client::XdgDecoration::Mode>("requestedMode");
    QTest::addColumn<Wrapland::Client::XdgDecoration::Mode>("expectedMode");

    QTest::newRow("client side requested") << XdgDecoration::Mode::ClientSide << XdgDecoration::Mode::ClientSide;
    QTest::newRow("server side requested") << XdgDecoration::Mode::ServerSide << XdgDecoration::Mode::ServerSide;
}

void TestXdgShellClient::testXdgDecoration()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr,
                                                                                   Test::CreationSetup::CreateOnly));
    QScopedPointer<XdgDecoration> deco(Test::xdgDecorationManager()->getToplevelDecoration(shellSurface.data()));

    QSignalSpy decorationConfiguredSpy(deco.data(), &XdgDecoration::modeChanged);
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);

    QFETCH(Wrapland::Client::XdgDecoration::Mode, requestedMode);
    QFETCH(Wrapland::Client::XdgDecoration::Mode, expectedMode);

    deco->setMode(requestedMode);

    Test::init_xdg_shell_toplevel(surface.data(), shellSurface.data());

    QCOMPARE(configureRequestedSpy.count(), 1);
    QCOMPARE(decorationConfiguredSpy.count(), 1);
    QCOMPARE(decorationConfiguredSpy.first()[0].value<Wrapland::Client::XdgDecoration::Mode>(), expectedMode);

    shellSurface->ackConfigure(configureRequestedSpy.last()[2].toInt());

    auto c = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::blue);
    QCOMPARE(c->userCanSetNoBorder(), expectedMode == XdgDecoration::Mode::ServerSide);
    QCOMPARE(win::decoration(c) != nullptr, expectedMode == XdgDecoration::Mode::ServerSide);
}

void TestXdgShellClient::testXdgNeverCommitted()
{
    //check we don't crash if we create a shell object but delete the XdgShellClient before committing it
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr, Test::CreationSetup::CreateOnly));
}

void TestXdgShellClient::testXdgInitialState()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr, Test::CreationSetup::CreateOnly));
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);
    surface->commit(Surface::CommitFlag::None);

    configureRequestedSpy.wait();

    QCOMPARE(configureRequestedSpy.count(), 1);

    const auto size = configureRequestedSpy.first()[0].value<QSize>();

    QCOMPARE(size, QSize(0, 0)); //client should chose it's preferred size

    shellSurface->ackConfigure(configureRequestedSpy.first()[2].toUInt());

    auto c = Test::renderAndWaitForShown(surface.data(), QSize(200,100), Qt::blue);
    QCOMPARE(c->size(), QSize(200, 100));
}

void TestXdgShellClient::testXdgInitiallyMaximised()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr, Test::CreationSetup::CreateOnly));
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);

    shellSurface->setMaximized(true);
    surface->commit(Surface::CommitFlag::None);

    configureRequestedSpy.wait();

    QCOMPARE(configureRequestedSpy.count(), 1);

    const auto size = configureRequestedSpy.first()[0].value<QSize>();
    auto state = configureRequestedSpy.first()[1].value<Wrapland::Client::XdgShellToplevel::States>();

    QCOMPARE(size, QSize(1280, 1024));
    QCOMPARE(state & Wrapland::Client::XdgShellToplevel::State::Activated, false);
    QVERIFY(state & Wrapland::Client::XdgShellToplevel::State::Maximized);

    shellSurface->ackConfigure(configureRequestedSpy.first()[2].toUInt());

    auto c = Test::renderAndWaitForShown(surface.data(), size, Qt::blue);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(c->size(), QSize(1280, 1024));

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);

    state = configureRequestedSpy.last()[1].value<Wrapland::Client::XdgShellToplevel::States>();
    QVERIFY(state & Wrapland::Client::XdgShellToplevel::State::Activated);
    QVERIFY(state & Wrapland::Client::XdgShellToplevel::State::Maximized);

    // Unmaximize again, an empty size is returned, that means the client should decide.
    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);

    QVERIFY(configureRequestedSpy.last().at(0).toSize().isEmpty());
}

void TestXdgShellClient::testXdgInitiallyFullscreen()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr, Test::CreationSetup::CreateOnly));
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);

    shellSurface->setFullscreen(true);
    surface->commit(Surface::CommitFlag::None);

    configureRequestedSpy.wait();

    QCOMPARE(configureRequestedSpy.count(), 1);

    const auto size = configureRequestedSpy.first()[0].value<QSize>();
    const auto state = configureRequestedSpy.first()[1].value<Wrapland::Client::XdgShellToplevel::States>();

    QCOMPARE(size, QSize(1280, 1024));
    QVERIFY(state & Wrapland::Client::XdgShellToplevel::State::Fullscreen);

    shellSurface->ackConfigure(configureRequestedSpy.first()[2].toUInt());

    auto c = Test::renderAndWaitForShown(surface.data(), size, Qt::blue);
    QCOMPARE(c->control->fullscreen(), true);
    QCOMPARE(c->size(), QSize(1280, 1024));
}

void TestXdgShellClient::testXdgInitiallyMinimized()
{
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data(), nullptr, Test::CreationSetup::CreateOnly));
    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);

    shellSurface->requestMinimize();
    surface->commit(Surface::CommitFlag::None);

    configureRequestedSpy.wait();

    QCOMPARE(configureRequestedSpy.count(), 1);

    const auto size = configureRequestedSpy.first()[0].value<QSize>();
    const auto state = configureRequestedSpy.first()[1].value<Wrapland::Client::XdgShellToplevel::States>();

    QCOMPARE(size, QSize(0, 0));
    QCOMPARE(state, 0);

    shellSurface->ackConfigure(configureRequestedSpy.first()[2].toUInt());

    QEXPECT_FAIL("", "Client created in a minimised state is not exposed to kwin bug 404838", Abort);
    auto c = Test::renderAndWaitForShown(surface.data(), size, Qt::blue, QImage::Format_ARGB32, 10);
    QVERIFY(c);
    QVERIFY(c->control->minimized());
}

void TestXdgShellClient::testXdgWindowGeometryIsntSet()
{
    // This test verifies that the effective window geometry corresponds to the
    // bounding rectangle of the main surface and its sub-surfaces if no window
    // geometry is set by the client.

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(200, 100), Qt::red);
    QVERIFY(client);
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(200, 100));

    const QPoint oldPosition = client->pos();

    QSignalSpy geometryChangedSpy(client, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    Test::render(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->frameGeometry().topLeft(), oldPosition);
    QCOMPARE(client->frameGeometry().size(), QSize(100, 50));
    QCOMPARE(win::render_geometry(client).topLeft(), oldPosition);
    QCOMPARE(win::render_geometry(client).size(), QSize(100, 50));

    QScopedPointer<Surface> childSurface(Test::createSurface());
    QScopedPointer<SubSurface> subSurface(Test::createSubSurface(childSurface.data(), surface.data()));
    QVERIFY(subSurface);
    subSurface->setPosition(QPoint(-20, -10));
    Test::render(childSurface.data(), QSize(100, 50), Qt::blue);
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->frameGeometry().topLeft(), oldPosition);
    QCOMPARE(client->frameGeometry().size(), QSize(120, 60));
    QCOMPARE(win::render_geometry(client).topLeft(), oldPosition + QPoint(20, 10));
    QCOMPARE(win::render_geometry(client).size(), QSize(100, 50));
}

void TestXdgShellClient::testXdgWindowGeometryAttachBuffer()
{
    // This test verifies that the effective window geometry remains the same when
    // a new buffer is attached and xdg_surface.set_window_geometry is not called
    // again. Notice that the window geometry must remain the same even if the new
    // buffer is smaller.

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(200, 100), Qt::red);
    QVERIFY(client);
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(200, 100));

    client->setFrameGeometry(QRect(QPoint(100, 100), client->size()));

    auto const first_pos = client->pos();
    auto const first_win_geo = QRect(10, 10, 180, 80);
    auto const second_win_geo = QRect(5, 5, 90, 40);

    QSignalSpy geometryChangedSpy(client, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    shellSurface->setWindowGeometry(first_win_geo);
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(geometryChangedSpy.wait());

    QCOMPARE(client->frameGeometry().topLeft(), first_pos);
    QCOMPARE(client->frameGeometry().size(), QSize(180, 80));
    QCOMPARE(win::render_geometry(client).topLeft(), first_pos - QPoint(10, 10));
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));

    // Now reduce the size from 200x100 to 100x50.
    Test::render(surface.data(), QSize(100, 50), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(client->frameGeometry().topLeft(), first_pos);

    QCOMPARE(client->frameGeometry().size(),
             first_win_geo.intersected(client->surface()->expanse()).size());
    QCOMPARE(client->frameGeometry().size(), QSize(90, 40));
    QCOMPARE(win::render_geometry(client).topLeft(), first_pos - QPoint(10, 10));
    QCOMPARE(win::render_geometry(client).size(), QSize(100, 50));

    shellSurface->setWindowGeometry(second_win_geo);
    surface->commit(Surface::CommitFlag::None);

    // The frame geometry must stay the same.
    QVERIFY(!geometryChangedSpy.wait(200));
    QCOMPARE(client->frameGeometry().topLeft(), first_pos);
    QCOMPARE(client->frameGeometry().size(), QSize(90, 40));
    QCOMPARE(win::render_geometry(client).topLeft(), first_pos - QPoint(5, 5));
    QCOMPARE(win::render_geometry(client).size(), QSize(100, 50));

    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void TestXdgShellClient::testSendToScreen()
{
    // This test verifies that we can send xdg-shell toplevels and popups to other screens.

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shell_surface(
        Test::create_xdg_shell_toplevel(surface.data()));

    auto window = Test::renderAndWaitForShown(surface.data(), QSize(200, 100), Qt::red);
    QVERIFY(window);
    QCOMPARE(workspace()->activeClient(), window);
    QCOMPARE(window->frameGeometry().size(), QSize(200, 100));

    XdgPositioner positioner(QSize(50, 40), QRect(0, 0, 5, 10));
    positioner.setAnchorEdge(Qt::BottomEdge | Qt::RightEdge);
    positioner.setGravity(Qt::BottomEdge | Qt::RightEdge);

    std::unique_ptr<Surface> popup_surface(Test::createSurface());
    std::unique_ptr<XdgShellPopup> popup_shell_surface(
        Test::create_xdg_shell_popup(popup_surface.get(), shell_surface.data(), positioner));

    auto popup
        = Test::renderAndWaitForShown(popup_surface.get(), positioner.initialSize(), Qt::blue);
    QVERIFY(popup);
    QCOMPARE(popup->frameGeometry(),
             QRect(window->frameGeometry().topLeft() + QPoint(5, 10), QSize(50, 40)));

    QSignalSpy geometryChangedSpy(window, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    QCOMPARE(window->screen(), 0);
    QCOMPARE(popup->screen(), 0);
    win::send_to_screen(window, 1);
    QCOMPARE(window->screen(), 1);
    QCOMPARE(popup->screen(), 1);

    QCOMPARE(popup->frameGeometry(),
             QRect(window->frameGeometry().topLeft() + QPoint(5, 10), QSize(50, 40)));
}

void TestXdgShellClient::testXdgWindowGeometryAttachSubSurface()
{
    // This test verifies that the effective window geometry remains the same
    // when a new sub-surface is added and xdg_surface.set_window_geometry is
    // not called again.

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(200, 100), Qt::red);
    QVERIFY(client);
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(200, 100));

    client->setFrameGeometry(QRect(QPoint(100, 100), client->size()));

    auto const first_pos = client->pos();
    auto const first_win_geo = QRect(10, 10, 180, 80);
    auto const second_win_geo = QRect(-15, -15, 50, 40);
    auto const subsurface_offset = QPoint(-20, -20);

    QSignalSpy geometryChangedSpy(client, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    shellSurface->setWindowGeometry(first_win_geo);
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(geometryChangedSpy.wait());

    QCOMPARE(client->frameGeometry().topLeft(), first_pos);
    QCOMPARE(client->frameGeometry().size(), QSize(180, 80));
    QCOMPARE(win::render_geometry(client).topLeft(), first_pos - QPoint(10, 10));
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));

    QScopedPointer<Surface> childSurface(Test::createSurface());
    QScopedPointer<SubSurface> subSurface(Test::createSubSurface(childSurface.data(), surface.data()));
    QVERIFY(subSurface);

    subSurface->setPosition(subsurface_offset);
    Test::render(childSurface.data(), QSize(100, 50), Qt::blue);
    surface->commit(Surface::CommitFlag::None);

    QCOMPARE(client->frameGeometry().topLeft(), first_pos);
    QCOMPARE(client->frameGeometry().size(), QSize(180, 80));
    QCOMPARE(win::render_geometry(client).topLeft(), first_pos - QPoint(10, 10));
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));

    shellSurface->setWindowGeometry(second_win_geo);
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(geometryChangedSpy.wait());

    // TODO: Is the buffer relative to the main surface's top-left corner or to the union of it with
    // all subsurfaces?

    QCOMPARE(client->frameGeometry().topLeft(), first_pos);
    QCOMPARE(client->frameGeometry().size(), QSize(50, 40));
    QCOMPARE(win::render_geometry(client).topLeft(), first_pos - QPoint(-15, -15));
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
}

void TestXdgShellClient::testXdgWindowGeometryInteractiveResize()
{
    // This test verifies that correct window geometry is provided along each
    // configure event when an xdg-shell is being interactively resized.

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(200, 100), Qt::red);
    QVERIFY(client);
    QVERIFY(client->control->active());
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(200, 100));

    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QSignalSpy geometryChangedSpy(client, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->setWindowGeometry(QRect(10, 10, 180, 80));
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(180, 80));

    QSignalSpy clientStartMoveResizedSpy(client, &Toplevel::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QSignalSpy clientStepUserMovedResizedSpy(client, &Toplevel::clientStepUserMovedResized);
    QVERIFY(clientStepUserMovedResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(client, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    // Start interactively resizing the client.
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    workspace()->slotWindowResize();
    QCOMPARE(workspace()->moveResizeClient(), client);
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    XdgShellToplevel::States states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));

    // Go right.
    QPoint cursorPos = KWin::Cursor::pos();
    win::key_press_event(client, Qt::Key_Right);
    win::update_move_resize(client, KWin::Cursor::pos());
    QCOMPARE(KWin::Cursor::pos(), cursorPos + QPoint(8, 0));
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(188, 80));
    shellSurface->setWindowGeometry(QRect(10, 10, 188, 80));
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(208, 100), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
    QCOMPARE(win::render_geometry(client).size(), QSize(208, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(188, 80));

    // Go down.
    cursorPos = KWin::Cursor::pos();
    win::key_press_event(client, Qt::Key_Down);
    win::update_move_resize(client, KWin::Cursor::pos());
    QCOMPARE(KWin::Cursor::pos(), cursorPos + QPoint(0, 8));
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 4);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(188, 88));
    shellSurface->setWindowGeometry(QRect(10, 10, 188, 88));
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(208, 108), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 2);
    QCOMPARE(win::render_geometry(client).size(), QSize(208, 108));
    QCOMPARE(client->frameGeometry().size(), QSize(188, 88));

    // Finish resizing the client.
    win::key_press_event(client, Qt::Key_Enter);
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
#if 0
    QEXPECT_FAIL("", "XdgShellClient currently doesn't send final configure event", Abort);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 5);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Resizing));
#endif

    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void TestXdgShellClient::testXdgWindowGeometryFullScreen()
{
    // This test verifies that an xdg-shell receives correct window geometry when
    // its fullscreen state gets changed.

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(200, 100), Qt::red);
    QVERIFY(client);
    QVERIFY(client->control->active());
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(200, 100));

    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QSignalSpy window_geometry_spy(client->shell_surface, &Wrapland::Server::XdgShellSurface::window_geometry_changed);
    QVERIFY(window_geometry_spy.isValid());
    QSignalSpy geometryChangedSpy(client, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    shellSurface->setWindowGeometry(QRect(10, 10, 180, 80));
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(window_geometry_spy.count(), 1);

    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(180, 80));

    workspace()->slotWindowFullScreen();
    QCOMPARE(client->restore_geometries.maximize, QRect(0, 0, 180, 80));

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(1280, 1024));
    XdgShellToplevel::States states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Fullscreen));

    shellSurface->setWindowGeometry(QRect(0, 0, 1280, 1024));
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());

    Test::render(surface.data(), QSize(1280, 1024), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(win::render_geometry(client).size(), QSize(1280, 1024));
    QCOMPARE(client->frameGeometry().size(), QSize(1280, 1024));

    workspace()->slotWindowFullScreen();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(180, 80));
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Fullscreen));
    shellSurface->setWindowGeometry(QRect(10, 10, 180, 80));
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(200, 100), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(180, 80));

    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void TestXdgShellClient::testXdgWindowGeometryMaximize()
{
    // This test verifies that an xdg-shell receives correct window geometry when
    // its maximized state gets changed.

    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(200, 100), Qt::red);
    QVERIFY(client);
    QVERIFY(client->control->active());
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(200, 100));

    QSignalSpy configureRequestedSpy(shellSurface.data(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QSignalSpy geometryChangedSpy(client, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->setWindowGeometry(QRect(10, 10, 180, 80));
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(180, 80));

    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(1280, 1024));
    XdgShellToplevel::States states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));
    shellSurface->setWindowGeometry(QRect(0, 0, 1280, 1024));
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(1280, 1024), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(win::render_geometry(client).size(), QSize(1280, 1024));
    QCOMPARE(client->frameGeometry().size(), QSize(1280, 1024));

    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(180, 80));
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));
    shellSurface->setWindowGeometry(QRect(10, 10, 180, 80));
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface.data(), QSize(200, 100), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(win::render_geometry(client).size(), QSize(200, 100));
    QCOMPARE(client->frameGeometry().size(), QSize(180, 80));

    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void TestXdgShellClient::test_multi_maximize()
{
    // This test verifies that the case where a client issues two set_maximized() requests
    // separated by the initial commit is handled properly.

    // Create the test surface.
    std::unique_ptr<Surface> surface(Test::createSurface());
    std::unique_ptr<XdgShellToplevel> shell_surface(
        Test::create_xdg_shell_toplevel(surface.get(), nullptr, Test::CreationSetup::CreateOnly));
    shell_surface->setMaximized(true);
    surface->commit(Surface::CommitFlag::None);

    // Wait for the compositor to respond with a configure event.
    QSignalSpy configureRequestedSpy(shell_surface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    auto size = configureRequestedSpy.last().at(0).value<QSize>();
    QCOMPARE(size, QSize(1280, 1024));

    auto states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QCOMPARE(states & Wrapland::Client::XdgShellToplevel::State::Activated, false);
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Send another set_maximized() request, but do not attach any buffer yet.
    shell_surface->setMaximized(true);
    surface->commit(Surface::CommitFlag::None);

    // The compositor must respond with another configure event even if the state hasn't changed.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);

    size = configureRequestedSpy.last().at(0).value<QSize>();
    QCOMPARE(size, QSize(1280, 1024));

    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));
    QCOMPARE(states & Wrapland::Client::XdgShellToplevel::State::Activated, false);

    shell_surface->ackConfigure(configureRequestedSpy.last()[2].toUInt());

    auto client = Test::renderAndWaitForShown(surface.get(), size, Qt::blue);

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));
    QVERIFY(states & Wrapland::Client::XdgShellToplevel::State::Activated);

    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->size(), QSize(1280, 1024));

    // Now request to maximize again. This will change nothing, but we receive another configure
    // event.
    shell_surface->setMaximized(true);
    shell_surface->ackConfigure(configureRequestedSpy.last()[2].toUInt());
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 4);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QCOMPARE(client->size(), QSize(1280, 1024));

    // Now request to unmaximize. This will change the maximization state and we receive another
    // configure event, this time with an empty size.
    shell_surface->setMaximized(false);
    shell_surface->ackConfigure(configureRequestedSpy.last()[2].toUInt());
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 5);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    size = configureRequestedSpy.last().at(0).value<QSize>();
    QVERIFY(size.isEmpty());

    // Request to unmaximize again. This will change nothing, but we receive another configure
    // event.
    shell_surface->setMaximized(false);
    shell_surface->ackConfigure(configureRequestedSpy.last()[2].toUInt());
    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 6);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    size = configureRequestedSpy.last().at(0).value<QSize>();
    QEXPECT_FAIL("",
                 "We change the synced geometry on commit. Use other geometry or don't do that.",
                 Continue);
    QVERIFY(size.isEmpty());
}

WAYLANDTEST_MAIN(TestXdgShellClient)
#include "xdgshellclient_test.moc"
