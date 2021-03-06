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
#include "composite.h"
#include "effects.h"
#include "effectloader.h"
#include "screens.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"
#include "scripting/scriptedeffect.h"

#include "win/wayland/window.h"

#include <KDecoration2/Decoration>

#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_dont_crash_cancel_animation-0");

class DontCrashCancelAnimationFromAnimationEndedTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testScript();
};

void DontCrashCancelAnimationFromAnimationEndedTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));
    kwinApp()->start();
    QVERIFY(Compositor::self());
    QSignalSpy compositorToggledSpy(Compositor::self(), &Compositor::compositingToggled);
    QVERIFY(compositorToggledSpy.isValid());
    QVERIFY(compositorToggledSpy.wait());
    QVERIFY(effects);
}

void DontCrashCancelAnimationFromAnimationEndedTest::init()
{
    Test::setupWaylandConnection();
}

void DontCrashCancelAnimationFromAnimationEndedTest::cleanup()
{
    Test::destroyWaylandConnection();
}

void DontCrashCancelAnimationFromAnimationEndedTest::testScript()
{
    // load a scripted effect which deletes animation data
    ScriptedEffect *effect = ScriptedEffect::create(QStringLiteral("crashy"), QFINDTESTDATA("data/anim-data-delete-effect/effect.js"), 10);
    QVERIFY(effect);

    const auto children = effects->children();
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (qstrcmp((*it)->metaObject()->className(), "KWin::EffectLoader") != 0) {
            continue;
        }
        QVERIFY(QMetaObject::invokeMethod(*it, "effectLoaded", Q_ARG(KWin::Effect*, effect), Q_ARG(QString, QStringLiteral("crashy"))));
        break;
    }
    QVERIFY(static_cast<EffectsHandlerImpl*>(effects)->isEffectLoaded(QStringLiteral("crashy")));

    using namespace Wrapland::Client;
    // create a window
    Surface *surface = Test::createSurface(Test::waylandCompositor());
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface, surface);
    QVERIFY(shellSurface);
    // let's render
    auto c = Test::renderAndWaitForShown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);

    // make sure we animate
    QTest::qWait(200);

    // wait for the window to be passed to Deleted
    QSignalSpy windowDeletedSpy(c, &Toplevel::windowClosed);
    QVERIFY(windowDeletedSpy.isValid());

    surface->deleteLater();

    QVERIFY(windowDeletedSpy.wait());
    // make sure we animate
    QTest::qWait(200);
}

}

WAYLANDTEST_MAIN(KWin::DontCrashCancelAnimationFromAnimationEndedTest)
#include "dont_crash_cancel_animation.moc"
