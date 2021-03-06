/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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

#include "composite.h"
#include "effectloader.h"
#include "effects.h"
#include "platform.h"
#include "scene.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/net.h"
#include "win/stacking.h"
#include "win/wayland/window.h"

#include "effect_builtins.h"

#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/plasmawindowmanagement.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

using namespace KWin;

static const QString s_socketName = QStringLiteral("wayland_test_effects_minimize_animation-0");

class MinimizeAnimationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMinimizeUnminimize_data();
    void testMinimizeUnminimize();
};

void MinimizeAnimationTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));

    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    ScriptedEffectLoader loader;
    const auto builtinNames = BuiltInEffects::availableEffectNames() << loader.listOfKnownEffects();
    for (const QString &name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }
    config->sync();
    kwinApp()->setConfig(config);

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    waylandServer()->initWorkspace();

    auto scene = Compositor::self()->scene();
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), OpenGL2Compositing);
}

void MinimizeAnimationTest::init()
{
    Test::setupWaylandConnection(Test::AdditionalWaylandInterface::PlasmaShell
                                 | Test::AdditionalWaylandInterface::WindowManagement);
}

void MinimizeAnimationTest::cleanup()
{
    auto effectsImpl = qobject_cast<EffectsHandlerImpl *>(effects);
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::destroyWaylandConnection();
}

void MinimizeAnimationTest::testMinimizeUnminimize_data()
{
    QTest::addColumn<QString>("effectName");

    QTest::newRow("Magic Lamp") << QStringLiteral("magiclamp");
    QTest::newRow("Squash")     << QStringLiteral("kwin4_effect_squash");
}

void MinimizeAnimationTest::testMinimizeUnminimize()
{
    // This test verifies that a minimize effect tries to animate a client
    // when it's minimized or unminimized.

    using namespace Wrapland::Client;

    QSignalSpy plasmaWindowCreatedSpy(Test::waylandWindowManagement(), &PlasmaWindowManagement::windowCreated);
    QVERIFY(plasmaWindowCreatedSpy.isValid());

    // Create a panel at the top of the screen.
    const QRect panelRect = QRect(0, 0, 1280, 36);
    QScopedPointer<Surface> panelSurface(Test::createSurface());
    QVERIFY(!panelSurface.isNull());
    QScopedPointer<XdgShellToplevel> panelShellSurface(Test::create_xdg_shell_toplevel(panelSurface.data()));
    QVERIFY(!panelShellSurface.isNull());
    QScopedPointer<PlasmaShellSurface> plasmaPanelShellSurface(Test::waylandPlasmaShell()->createSurface(panelSurface.data()));
    QVERIFY(!plasmaPanelShellSurface.isNull());
    plasmaPanelShellSurface->setRole(PlasmaShellSurface::Role::Panel);
    plasmaPanelShellSurface->setPosition(panelRect.topLeft());
    plasmaPanelShellSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::AlwaysVisible);
    auto panel = Test::renderAndWaitForShown(panelSurface.data(), panelRect.size(), Qt::blue);
    QVERIFY(panel);
    QVERIFY(win::is_dock(panel));
    QCOMPARE(panel->frameGeometry(), panelRect);
    QVERIFY(plasmaWindowCreatedSpy.wait());
    QCOMPARE(plasmaWindowCreatedSpy.count(), 1);

    // Create the test client.
    QScopedPointer<Surface> surface(Test::createSurface());
    QVERIFY(!surface.isNull());
    QScopedPointer<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface.data()));
    QVERIFY(!shellSurface.isNull());
    auto client = Test::renderAndWaitForShown(surface.data(), QSize(100, 50), Qt::red);
    QVERIFY(client);
    QVERIFY(plasmaWindowCreatedSpy.wait());
    QCOMPARE(plasmaWindowCreatedSpy.count(), 2);

    // We have to set the minimized geometry because the squash effect needs it,
    // otherwise it won't start animation.
    auto window = plasmaWindowCreatedSpy.last().first().value<PlasmaWindow *>();
    QVERIFY(window);
    const QRect iconRect = QRect(0, 0, 42, 36);
    window->setMinimizedGeometry(panelSurface.data(), iconRect);
    Test::flushWaylandConnection();
    QTRY_COMPARE(client->iconGeometry(), iconRect.translated(panel->frameGeometry().topLeft()));

    // Load effect that will be tested.
    QFETCH(QString, effectName);
    auto effectsImpl = qobject_cast<EffectsHandlerImpl *>(effects);
    QVERIFY(effectsImpl);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect *effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Start the minimize animation.
    win::set_minimized(client, true);
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Start the unminimize animation.
    win::set_minimized(client, false);
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Destroy the panel.
    panelSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(panel));

    // Destroy the test client.
    surface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

WAYLANDTEST_MAIN(MinimizeAnimationTest)
#include "minimize_animation_test.moc"
