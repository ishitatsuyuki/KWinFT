/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"

#include "platform.h"
#include "wayland_server.h"
#include "win/screen.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/layer_shell_v1.h>
#include <Wrapland/Client/output.h>
#include <Wrapland/Client/surface.h>

using namespace KWin;
namespace Clt = Wrapland::Client;

constexpr auto socket_name = "wayland_test_kwin_xdgshellclient-0";
constexpr auto output_count = 2;

Q_DECLARE_METATYPE(QMargins)

namespace KWin
{

}
class layer_shell_test : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void test_create();
    void test_geo_data();
    void test_geo();
};

void layer_shell_test::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<Clt::Output*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1000, 500));
    QVERIFY(waylandServer()->init(socket_name));
    QMetaObject::invokeMethod(
        kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1000, 500));
    QCOMPARE(screens()->geometry(1), QRect(1000, 0, 1000, 500));
    waylandServer()->initWorkspace();
}

void layer_shell_test::init()
{
    Test::setupWaylandConnection();

    screens()->setCurrent(0);
    KWin::Cursor::setPos(QPoint(1280, 512));
}

void layer_shell_test::cleanup()
{
    Test::destroyWaylandConnection();
}

Clt::LayerSurfaceV1* create_layer_surface(Clt::Surface* surface,
                                          Clt::Output* output,
                                          Clt::LayerShellV1::layer lay,
                                          std::string domain,
                                          QObject* parent = nullptr)
{
    auto layer_shell = Test::layer_shell();
    if (!layer_shell) {
        return nullptr;
    }
    auto layer_surface
        = layer_shell->get_layer_surface(surface, output, lay, std::move(domain), parent);
    if (!layer_surface->isValid()) {
        delete layer_surface;
        return nullptr;
    }
    return layer_surface;
}

struct configure_payload {
    QSize size;
    uint32_t serial;
};

/**
 *  Initializes layer surface with configure round-trip.
 *
 *  @arg payload will hold the payload of the configure callback after return.
 */
void init_ack_layer_surface(Clt::Surface* surface,
                            Clt::LayerSurfaceV1* layer_surface,
                            configure_payload& payload)
{
    QSignalSpy configure_spy(layer_surface, &Clt::LayerSurfaceV1::configure_requested);
    QVERIFY(configure_spy.isValid());
    surface->commit(Clt::Surface::CommitFlag::None);
    QVERIFY(configure_spy.wait());
    QCOMPARE(configure_spy.count(), 1);
    payload.size = configure_spy.last()[0].toSize();
    payload.serial = configure_spy.last()[1].toInt();
    layer_surface->ack_configure(payload.serial);
}

/**
 *  Initializes layer surface with configure round-trip.
 */
void init_ack_layer_surface(Clt::Surface* surface, Clt::LayerSurfaceV1* layer_surface)
{
    configure_payload payload;
    init_ack_layer_surface(surface, layer_surface, payload);
}

enum class align {
    center,
    left,
    right,
    top,
    bottom,
};
Q_DECLARE_METATYPE(align)

/**
 *  Centers surface in area when not fills out full area.
 */
QRect target_geo(QRect const& area_geo,
                 QSize const& render_size,
                 QMargins const& margin,
                 align align_horizontal,
                 align align_vertical)
{
    QPoint rel_pos;
    switch (align_horizontal) {
    case align::left:
        rel_pos.rx() = margin.left();
        break;
    case align::right:
        rel_pos.rx() = area_geo.width() - render_size.width() - margin.right();
        break;
    case align::center:
    default:
        rel_pos.rx() = area_geo.width() / 2 - render_size.width() / 2;
    };
    switch (align_vertical) {
    case align::top:
        rel_pos.ry() = margin.top();
        break;
    case align::bottom:
        rel_pos.ry() = area_geo.height() - render_size.height() - margin.bottom();
        break;
    case align::center:
    default:
        rel_pos.ry() = area_geo.height() / 2 - render_size.height() / 2;
    };
    return QRect(area_geo.topLeft() + rel_pos, render_size);
}

void layer_shell_test::test_create()
{
    // Tries to create multiple kinds of layer surfaces.
    QSignalSpy window_spy(waylandServer(), &WaylandServer::window_added);
    QVERIFY(window_spy.isValid());

    auto surface = std::unique_ptr<Clt::Surface>(Test::createSurface());
    auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(create_layer_surface(
        surface.get(), Test::outputs().at(1), Clt::LayerShellV1::layer::top, ""));

    layer_surface->set_anchor(Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge | Qt::LeftEdge);

    configure_payload payload;
    init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

    auto const output1_geo = screens()->geometry(1);
    QCOMPARE(payload.size, output1_geo.size());

    auto render_size = QSize(100, 50);
    Test::renderAndWaitForShown(surface.get(), render_size, Qt::blue);
    QVERIFY(!window_spy.isEmpty());

    auto window = window_spy.first().first().value<win::wayland::window*>();
    QVERIFY(window);
    QVERIFY(window->isShown());
    QCOMPARE(window->isHiddenInternal(), false);
    QCOMPARE(window->readyForPainting(), true);
    QCOMPARE(window->depth(), 32);
    QVERIFY(window->hasAlpha());

    // By default layer surfaces have keyboard interactivity set to none.
    QCOMPARE(workspace()->activeClient(), nullptr);

    QVERIFY(!window->isMaximizable());
    QVERIFY(!window->isMovable());
    QVERIFY(!window->isMovableAcrossScreens());
    QVERIFY(!window->isResizable());
    QVERIFY(!window->isInternal());
    QVERIFY(window->effectWindow());
    QVERIFY(!window->effectWindow()->internalWindow());

    // Surface is centered.
    QCOMPARE(window->frameGeometry(),
             target_geo(output1_geo, render_size, QMargins(), align::center, align::center));

    window_spy.clear();

    auto surface2 = std::unique_ptr<Clt::Surface>(Test::createSurface());
    auto layer_surface2 = std::unique_ptr<Clt::LayerSurfaceV1>(create_layer_surface(
        surface2.get(), Test::outputs().at(1), Clt::LayerShellV1::layer::bottom, ""));

    layer_surface2->set_anchor(Qt::TopEdge | Qt::BottomEdge);
    layer_surface2->set_size(QSize(100, 0));
    layer_surface2->set_keyboard_interactivity(
        Clt::LayerShellV1::keyboard_interactivity::on_demand);

    init_ack_layer_surface(surface2.get(), layer_surface2.get(), payload);

    QCOMPARE(payload.size, QSize(100, output1_geo.height()));

    // We render at half the size. The resulting surface should be centered.
    // Note that this is a bit of an abuse as in the set_size call we specified a different width.
    // The protocol at the moment does not forbid this.
    render_size = payload.size / 2;

    Test::renderAndWaitForShown(surface2.get(), render_size, Qt::red);
    QVERIFY(!window_spy.isEmpty());

    auto window2 = window_spy.first().first().value<win::wayland::window*>();
    QVERIFY(window2);
    QVERIFY(window2->isShown());
    QCOMPARE(window2->isHiddenInternal(), false);
    QCOMPARE(window2->readyForPainting(), true);
    QCOMPARE(workspace()->activeClient(), window2);

    // Surface is centered.
    QCOMPARE(window2->frameGeometry(),
             target_geo(output1_geo, render_size, QMargins(), align::center, align::center));
}

void layer_shell_test::test_geo_data()
{
    QTest::addColumn<int>("output");
    QTest::addColumn<Qt::Edges>("anchor");
    QTest::addColumn<QSize>("set_size");
    QTest::addColumn<QMargins>("margin");
    QTest::addColumn<QSize>("render_size");
    QTest::addColumn<align>("align_horizontal");
    QTest::addColumn<align>("align_vertical");

    struct anchor {
        Qt::Edges anchor;
        QByteArray text;
        struct {
            align horizontal;
            align vertical;
        } is_mid;
    };

    // All possible combinations of anchors.
    auto const anchors = {
        anchor{Qt::Edges(), "()", align::center, align::center},
        anchor{Qt::Edges(Qt::LeftEdge), "l", align::left, align::center},
        anchor{Qt::Edges(Qt::TopEdge), "t", align::center, align::top},
        anchor{Qt::Edges(Qt::RightEdge), "r", align::right, align::center},
        anchor{Qt::Edges(Qt::BottomEdge), "b", align::center, align::bottom},
        anchor{Qt::LeftEdge | Qt::TopEdge, "lt", align::left, align::top},
        anchor{Qt::TopEdge | Qt::RightEdge, "tr", align::right, align::top},
        anchor{Qt::RightEdge | Qt::BottomEdge, "rb", align::right, align::bottom},
        anchor{Qt::BottomEdge | Qt::LeftEdge, "bl", align::left, align::bottom},
        anchor{Qt::LeftEdge | Qt::RightEdge, "lr", align::center, align::center},
        anchor{Qt::TopEdge | Qt::BottomEdge, "tb", align::center, align::center},
        anchor{Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge, "ltr", align::center, align::top},
        anchor{Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge, "trb", align::right, align::center},
        anchor{Qt::RightEdge | Qt::BottomEdge | Qt::LeftEdge, "rbl", align::center, align::bottom},
        anchor{Qt::BottomEdge | Qt::LeftEdge | Qt::TopEdge, "blt", align::left, align::center},
        anchor{Qt::LeftEdge | Qt::TopEdge | Qt::RightEdge | Qt::BottomEdge,
               "ltrb",
               align::center,
               align::center},
    };

    struct margin {
        QMargins margin;
        QByteArray text;
    };

    // Some example margins.
    auto const margins = {
        margin{QMargins(), "0,0,0,0"},
        margin{QMargins(0, 1, 2, 3), "0,1,2,3"},
        margin{QMargins(100, 200, 300, 400), "100,200,300,400"},
    };

    auto const set_size = QSize(100, 200);
    auto const render_size = QSize(100, 50);

    for (auto output = 0; output < output_count; output++) {
        for (auto const& anchor : anchors) {
            for (auto const& margin : margins) {
                auto const text = anchor.text + "-anchor|" + margin.text + "-margin|" + "out"
                    + QString::number(output + 1).toUtf8();
                QTest::newRow(text)
                    << output << anchor.anchor << set_size << margin.margin << render_size
                    << anchor.is_mid.horizontal << anchor.is_mid.vertical;
            }
        }
    }
}

void layer_shell_test::test_geo()
{
    // Checks various standard geometries.
    QSignalSpy window_spy(waylandServer(), &WaylandServer::window_added);
    QVERIFY(window_spy.isValid());

    QFETCH(int, output);
    auto surface = std::unique_ptr<Clt::Surface>(Test::createSurface());
    auto layer_surface = std::unique_ptr<Clt::LayerSurfaceV1>(create_layer_surface(
        surface.get(), Test::outputs().at(output), Clt::LayerShellV1::layer::top, ""));

    QFETCH(Qt::Edges, anchor);
    QFETCH(QSize, set_size);
    QFETCH(QMargins, margin);
    layer_surface->set_anchor(anchor);
    layer_surface->set_size(set_size);
    layer_surface->set_margin(margin);

    configure_payload payload;
    init_ack_layer_surface(surface.get(), layer_surface.get(), payload);

    QFETCH(QSize, render_size);
    Test::renderAndWaitForShown(surface.get(), render_size, Qt::blue);
    QVERIFY(!window_spy.isEmpty());

    auto window = window_spy.first().first().value<win::wayland::window*>();
    QVERIFY(window);

    QFETCH(align, align_horizontal);
    QFETCH(align, align_vertical);
    auto geo = target_geo(Test::outputs().at(output)->geometry(),
                          render_size,
                          margin,
                          align_horizontal,
                          align_vertical);
    QCOMPARE(window->frameGeometry(), geo);
}

WAYLANDTEST_MAIN(layer_shell_test)
#include "layer_shell.moc"
