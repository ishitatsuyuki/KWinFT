/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
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
#include "abstract_client.h"

#include "appmenu.h"
#include "decorations/decoratedclient.h"
#include "decorations/decorationpalette.h"
#include "decorations/decorationbridge.h"
#include "cursor.h"
#include "effects.h"
#include "focuschain.h"
#include "outline.h"
#include "screens.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif
#include "screenedge.h"
#include "useractions.h"
#include "win/win.h"
#include "workspace.h"

#include "wayland_server.h"
#include <Wrapland/Server/plasma_window.h>

#include <KDesktopFile>

#include <QDir>
#include <QMouseEvent>
#include <QStyleHints>

namespace KWin
{

QHash<QString, std::weak_ptr<Decoration::DecorationPalette>> AbstractClient::s_palettes;
std::shared_ptr<Decoration::DecorationPalette> AbstractClient::s_defaultPalette;

AbstractClient::AbstractClient()
    : Toplevel()
#ifdef KWIN_BUILD_TABBOX
    , m_tabBoxClient(QSharedPointer<TabBox::TabBoxClientImpl>(new TabBox::TabBoxClientImpl(this)))
#endif
    , m_colorScheme(QStringLiteral("kdeglobals"))
{
    connect(this, &AbstractClient::geometryShapeChanged, this, &AbstractClient::geometryChanged);

    auto signalMaximizeChanged
        = static_cast<void (AbstractClient::*)(KWin::AbstractClient*, win::maximize_mode)>(
            &AbstractClient::clientMaximizedStateChanged);
    connect(this, signalMaximizeChanged, this, &AbstractClient::geometryChanged);

    connect(this, &AbstractClient::clientStepUserMovedResized,   this, &AbstractClient::geometryChanged);
    connect(this, &AbstractClient::clientStartUserMovedResized,  this, &AbstractClient::moveResizedChanged);
    connect(this, &AbstractClient::clientFinishUserMovedResized, this, &AbstractClient::moveResizedChanged);
    connect(this, &AbstractClient::clientStartUserMovedResized,  this, &AbstractClient::removeCheckScreenConnection);
    connect(this, &AbstractClient::clientFinishUserMovedResized, this, &AbstractClient::setupCheckScreenConnection);

    connect(this, &AbstractClient::paletteChanged, this, [this] { win::trigger_decoration_repaint(this); });

    connect(Decoration::DecorationBridge::self(), &QObject::destroyed, this, &AbstractClient::destroyDecoration);

    // If the user manually moved the window, don't restore it after the keyboard closes
    connect(this, &AbstractClient::clientFinishUserMovedResized, this, [this] () {
        m_keyboardGeometryRestore = QRect();
    });
    connect(this, qOverload<AbstractClient *, bool, bool>(&AbstractClient::clientMaximizedStateChanged), this, [this] () {
        m_keyboardGeometryRestore = QRect();
    });
    connect(this, &AbstractClient::fullScreenChanged, this, [this] () {
        m_keyboardGeometryRestore = QRect();
    });

    // replace on-screen-display on size changes
    connect(this, &AbstractClient::geometryShapeChanged, this,
        [this] (Toplevel *c, const QRect &old) {
            Q_UNUSED(c)
            if (isOnScreenDisplay() && !frameGeometry().isEmpty() && old.size() != frameGeometry().size() && !isInitialPositionSet()) {
                GeometryUpdatesBlocker blocker(this);
                const QRect area = workspace()->clientArea(PlacementArea, Screens::self()->current(), desktop());
                Placement::self()->place(this, area);
                setGeometryRestore(frameGeometry());
            }
        }
    );

    connect(this, &AbstractClient::paddingChanged, this, [this]() {
        m_visibleRectBeforeGeometryUpdate = visibleRect();
    });

    connect(ApplicationMenu::self(), &ApplicationMenu::applicationMenuEnabledChanged, this, [this] {
        emit hasApplicationMenuChanged(hasApplicationMenu());
    });
}

AbstractClient::~AbstractClient()
{
    Q_ASSERT(m_blockGeometryUpdates == 0);
    Q_ASSERT(m_decoration.decoration == nullptr);
}

void AbstractClient::updateMouseGrab()
{
}

bool AbstractClient::belongToSameApplication(const AbstractClient *c1, const AbstractClient *c2, SameApplicationChecks checks)
{
    return c1->belongsToSameApplication(c2, checks);
}

bool AbstractClient::isTransient() const
{
    return false;
}

void AbstractClient::setClientShown(bool shown)
{
    Q_UNUSED(shown)
}

win::maximize_mode AbstractClient::requestedMaximizeMode() const
{
    return maximizeMode();
}

xcb_timestamp_t AbstractClient::userTime() const
{
    return XCB_TIME_CURRENT_TIME;
}

void AbstractClient::setSkipSwitcher(bool set)
{
    set = rules()->checkSkipSwitcher(set);
    if (set == skipSwitcher())
        return;
    m_skipSwitcher = set;
    doSetSkipSwitcher();
    updateWindowRules(Rules::SkipSwitcher);
    emit skipSwitcherChanged();
}

void AbstractClient::setSkipPager(bool b)
{
    b = rules()->checkSkipPager(b);
    if (b == skipPager())
        return;
    m_skipPager = b;
    doSetSkipPager();
    updateWindowRules(Rules::SkipPager);
    emit skipPagerChanged();
}

void AbstractClient::doSetSkipPager()
{
}

void AbstractClient::setSkipTaskbar(bool b)
{
    auto const was_wants_tab_focus = win::wants_tab_focus(this);
    if (b == skipTaskbar())
        return;
    m_skipTaskbar = b;
    doSetSkipTaskbar();
    updateWindowRules(Rules::SkipTaskbar);
    if (was_wants_tab_focus != win::wants_tab_focus(this)) {
        FocusChain::self()->update(this, isActive() ? FocusChain::MakeFirst : FocusChain::Update);
    }
    emit skipTaskbarChanged();
}

void AbstractClient::setOriginalSkipTaskbar(bool b)
{
    m_originalSkipTaskbar = rules()->checkSkipTaskbar(b);
    setSkipTaskbar(m_originalSkipTaskbar);
}

void AbstractClient::doSetSkipTaskbar()
{

}

void AbstractClient::doSetSkipSwitcher()
{

}

void AbstractClient::setIcon(const QIcon &icon)
{
    m_icon = icon;
    emit iconChanged();
}

void AbstractClient::setActive(bool act)
{
    if (m_active == act) {
        return;
    }
    m_active = act;
    const int ruledOpacity = m_active
                             ? rules()->checkOpacityActive(qRound(opacity() * 100.0))
                             : rules()->checkOpacityInactive(qRound(opacity() * 100.0));
    setOpacity(ruledOpacity / 100.0);
    workspace()->setActiveClient(act ? this : nullptr);

    if (!m_active)
        cancelAutoRaise();

    if (!m_active && shadeMode() == ShadeActivated)
        setShade(ShadeNormal);

    StackingUpdatesBlocker blocker(workspace());
    workspace()->updateClientLayer(this);   // active windows may get different layer
    auto mainclients = mainClients();
    for (auto it = mainclients.constBegin();
            it != mainclients.constEnd();
            ++it)
        if ((*it)->isFullScreen())  // fullscreens go high even if their transient is active
            workspace()->updateClientLayer(*it);

    doSetActive();
    emit activeChanged();
    updateMouseGrab();
}

void AbstractClient::doSetActive()
{
}

Layer AbstractClient::layer() const
{
    if (m_layer == UnknownLayer)
        const_cast< AbstractClient* >(this)->m_layer = win::belong_to_layer(this);
    return m_layer;
}

void AbstractClient::updateLayer()
{
    if (layer() == win::belong_to_layer(this))
        return;
    StackingUpdatesBlocker blocker(workspace());
    invalidateLayer(); // invalidate, will be updated when doing restacking
    for (auto it = transients().constBegin(),
                                  end = transients().constEnd(); it != end; ++it)
        (*it)->updateLayer();
}

void AbstractClient::invalidateLayer()
{
    m_layer = UnknownLayer;
}

bool AbstractClient::belongsToDesktop() const
{
    return false;
}

Layer AbstractClient::layerForDock() const
{
    // slight hack for the 'allow window to cover panel' Kicker setting
    // don't move keepbelow docks below normal window, but only to the same
    // layer, so that both may be raised to cover the other
    if (keepBelow())
        return NormalLayer;
    if (keepAbove()) // slight hack for the autohiding panels
        return AboveLayer;
    return DockLayer;
}

void AbstractClient::setKeepAbove(bool b)
{
    b = rules()->checkKeepAbove(b);
    if (b && !rules()->checkKeepBelow(false))
        setKeepBelow(false);
    if (b == keepAbove()) {
        // force hint change if different
        if (info && bool(info->state() & NET::KeepAbove) != keepAbove())
            info->setState(keepAbove() ? NET::KeepAbove : NET::States(), NET::KeepAbove);
        return;
    }
    m_keepAbove = b;
    if (info) {
        info->setState(keepAbove() ? NET::KeepAbove : NET::States(), NET::KeepAbove);
    }
    workspace()->updateClientLayer(this);
    updateWindowRules(Rules::Above);

    doSetKeepAbove();
    emit keepAboveChanged(m_keepAbove);
}

void AbstractClient::doSetKeepAbove()
{
}

void AbstractClient::setKeepBelow(bool b)
{
    b = rules()->checkKeepBelow(b);
    if (b && !rules()->checkKeepAbove(false))
        setKeepAbove(false);
    if (b == keepBelow()) {
        // force hint change if different
        if (info && bool(info->state() & NET::KeepBelow) != keepBelow())
            info->setState(keepBelow() ? NET::KeepBelow : NET::States(), NET::KeepBelow);
        return;
    }
    m_keepBelow = b;
    if (info) {
        info->setState(keepBelow() ? NET::KeepBelow : NET::States(), NET::KeepBelow);
    }
    workspace()->updateClientLayer(this);
    updateWindowRules(Rules::Below);

    doSetKeepBelow();
    emit keepBelowChanged(m_keepBelow);
}

void AbstractClient::doSetKeepBelow()
{
}

void AbstractClient::startAutoRaise()
{
    delete m_autoRaiseTimer;
    m_autoRaiseTimer = new QTimer(this);
    connect(m_autoRaiseTimer, &QTimer::timeout, this, [this] { win::auto_raise(this); });
    m_autoRaiseTimer->setSingleShot(true);
    m_autoRaiseTimer->start(options->autoRaiseInterval());
}

void AbstractClient::cancelAutoRaise()
{
    delete m_autoRaiseTimer;
    m_autoRaiseTimer = nullptr;
}

bool AbstractClient::isSpecialWindow() const
{
    return win::is_special_window(this);
}

void AbstractClient::demandAttention(bool set)
{
    if (isActive())
        set = false;
    if (m_demandsAttention == set)
        return;
    m_demandsAttention = set;
    if (info) {
        info->setState(set ? NET::DemandsAttention : NET::States(), NET::DemandsAttention);
    }
    workspace()->clientAttentionChanged(this, set);
    emit demandsAttentionChanged();
}

void AbstractClient::setDesktops(QVector<VirtualDesktop*> desktops)
{
    //on x11 we can have only one desktop at a time
    if (kwinApp()->operationMode() == Application::OperationModeX11 && desktops.size() > 1) {
        desktops = QVector<VirtualDesktop*>({desktops.last()});
    }

    if (desktops == m_desktops) {
        return;
    }

    int was_desk = AbstractClient::desktop();
    const bool wasOnCurrentDesktop = isOnCurrentDesktop() && was_desk >= 0;

    m_desktops = desktops;

    if (windowManagementInterface()) {
        if (m_desktops.isEmpty()) {
            windowManagementInterface()->setOnAllDesktops(true);
        } else {
            windowManagementInterface()->setOnAllDesktops(false);
            auto currentDesktops = windowManagementInterface()->plasmaVirtualDesktops();
            for (auto desktop: m_desktops) {
                if (!currentDesktops.contains(desktop->id())) {
                    windowManagementInterface()->addPlasmaVirtualDesktop(desktop->id());
                } else {
                    currentDesktops.removeOne(desktop->id());
                }
            }
            for (auto desktopId: currentDesktops) {
                windowManagementInterface()->removePlasmaVirtualDesktop(desktopId);
            }
        }
    }
    if (info) {
        info->setDesktop(desktop());
    }

    if ((was_desk == NET::OnAllDesktops) != (desktop() == NET::OnAllDesktops)) {
        // onAllDesktops changed
        workspace()->updateOnAllDesktopsOfTransients(this);
    }

    auto transients_stacking_order = workspace()->ensureStackingOrder(transients());
    for (auto it = transients_stacking_order.constBegin();
            it != transients_stacking_order.constEnd();
            ++it)
        (*it)->setDesktops(desktops);

    if (isModal())  // if a modal dialog is moved, move the mainwindow with it as otherwise
        // the (just moved) modal dialog will confusingly return to the mainwindow with
        // the next desktop change
    {
        foreach (AbstractClient * c2, mainClients())
        c2->setDesktops(desktops);
    }

    doSetDesktop(desktop(), was_desk);

    FocusChain::self()->update(this, FocusChain::MakeFirst);
    updateWindowRules(Rules::Desktop);

    emit desktopChanged();
    if (wasOnCurrentDesktop != isOnCurrentDesktop())
        emit desktopPresenceChanged(this, was_desk);
    emit x11DesktopIdsChanged();
}

void AbstractClient::doSetDesktop(int desktop, int was_desk)
{
    Q_UNUSED(desktop)
    Q_UNUSED(was_desk)
}

void AbstractClient::enterDesktop(VirtualDesktop *virtualDesktop)
{
    if (m_desktops.contains(virtualDesktop)) {
        return;
    }
    auto desktops = m_desktops;
    desktops.append(virtualDesktop);
    setDesktops(desktops);
}

void AbstractClient::leaveDesktop(VirtualDesktop *virtualDesktop)
{
    QVector<VirtualDesktop*> currentDesktops;
    if (m_desktops.isEmpty()) {
        currentDesktops = VirtualDesktopManager::self()->desktops();
    } else {
        currentDesktops = m_desktops;
    }

    if (!currentDesktops.contains(virtualDesktop)) {
        return;
    }
    auto desktops = currentDesktops;
    desktops.removeOne(virtualDesktop);
    setDesktops(desktops);
}

QVector<uint> AbstractClient::x11DesktopIds() const
{
    return win::x11_desktop_ids(this);
}

bool AbstractClient::isShadeable() const
{
    return false;
}

void AbstractClient::setShade(bool set)
{
    set ? setShade(ShadeNormal) : setShade(ShadeNone);
}

void AbstractClient::setShade(ShadeMode mode)
{
    Q_UNUSED(mode)
}

ShadeMode AbstractClient::shadeMode() const
{
    return ShadeNone;
}

win::position AbstractClient::titlebarPosition() const
{
    return win::position::top;
}

win::position AbstractClient::moveResizePointerMode() const
{
    return m_moveResize.pointer;
}

void AbstractClient::setMinimized(bool set)
{
    set ? minimize() : unminimize();
}

void AbstractClient::minimize(bool avoid_animation)
{
    if (!isMinimizable() || isMinimized())
        return;

    if (isShade() && info) // NETWM restriction - KWindowInfo::isMinimized() == Hidden && !Shaded
        info->setState(NET::States(), NET::Shaded);

    m_minimized = true;

    doMinimize();

    updateWindowRules(Rules::Minimize);
    // TODO: merge signal with s_minimized
    addWorkspaceRepaint(visibleRect());
    emit clientMinimized(this, !avoid_animation);
    emit minimizedChanged();
}

void AbstractClient::unminimize(bool avoid_animation)
{
    if (!isMinimized())
        return;

    if (rules()->checkMinimize(false)) {
        return;
    }

    if (isShade() && info) // NETWM restriction - KWindowInfo::isMinimized() == Hidden && !Shaded
        info->setState(NET::Shaded, NET::Shaded);

    m_minimized = false;

    doMinimize();

    updateWindowRules(Rules::Minimize);
    emit clientUnminimized(this, !avoid_animation);
    emit minimizedChanged();
}

void AbstractClient::doMinimize()
{
}

QPalette AbstractClient::palette() const
{
    if (!m_palette) {
        return QPalette();
    }
    return m_palette->palette();
}

const Decoration::DecorationPalette *AbstractClient::decorationPalette() const
{
    return m_palette.get();
}

void AbstractClient::updateColorScheme(QString path)
{
    if (path.isEmpty()) {
        path = QStringLiteral("kdeglobals");
    }

    if (!m_palette || m_colorScheme != path) {
        m_colorScheme = path;

        if (m_palette) {
            disconnect(m_palette.get(), &Decoration::DecorationPalette::changed, this, &AbstractClient::handlePaletteChange);
        }

        auto it = s_palettes.find(m_colorScheme);

        if (it == s_palettes.end() || it->expired()) {
            m_palette = std::make_shared<Decoration::DecorationPalette>(m_colorScheme);
            if (m_palette->isValid()) {
                s_palettes[m_colorScheme] = m_palette;
            } else {
                if (!s_defaultPalette) {
                    s_defaultPalette = std::make_shared<Decoration::DecorationPalette>(QStringLiteral("kdeglobals"));
                    s_palettes[QStringLiteral("kdeglobals")] = s_defaultPalette;
                }

                m_palette = s_defaultPalette;
            }

            if (m_colorScheme == QStringLiteral("kdeglobals")) {
                s_defaultPalette = m_palette;
            }
        } else {
            m_palette = it->lock();
        }

        connect(m_palette.get(), &Decoration::DecorationPalette::changed, this, &AbstractClient::handlePaletteChange);

        emit paletteChanged(palette());
        emit colorSchemeChanged();
    }
}

void AbstractClient::handlePaletteChange()
{
    emit paletteChanged(palette());
}

QSize AbstractClient::maxSize() const
{
    return rules()->checkMaxSize(QSize(INT_MAX, INT_MAX));
}

QSize AbstractClient::minSize() const
{
    return rules()->checkMinSize(QSize(0, 0));
}

void AbstractClient::blockGeometryUpdates(bool block)
{
    if (block) {
        if (m_blockGeometryUpdates == 0)
            m_pendingGeometryUpdate = PendingGeometryNone;
        ++m_blockGeometryUpdates;
    } else {
        if (--m_blockGeometryUpdates == 0) {
            if (m_pendingGeometryUpdate != PendingGeometryNone) {
                if (isShade())
                    setFrameGeometry(QRect(pos(), win::adjusted_size(this)), win::force_geometry::no);
                else
                    setFrameGeometry(frameGeometry(), win::force_geometry::no);
                m_pendingGeometryUpdate = PendingGeometryNone;
            }
        }
    }
}

void AbstractClient::move(int x, int y, win::force_geometry force)
{
    // resuming geometry updates is handled only in setGeometry()
    Q_ASSERT(pendingGeometryUpdate() == PendingGeometryNone || areGeometryUpdatesBlocked());
    QPoint p(x, y);
    if (!areGeometryUpdatesBlocked() && p != rules()->checkPosition(p)) {
        qCDebug(KWIN_CORE) << "forced position fail:" << p << ":" << rules()->checkPosition(p);
    }
    if (force == win::force_geometry::no && m_frameGeometry.topLeft() == p)
        return;
    auto old_frame_geometry = m_frameGeometry;
    m_frameGeometry.moveTopLeft(p);
    if (areGeometryUpdatesBlocked()) {
        if (pendingGeometryUpdate() == PendingGeometryForced)
            {} // maximum, nothing needed
        else if (force == win::force_geometry::yes)
            setPendingGeometryUpdate(PendingGeometryForced);
        else
            setPendingGeometryUpdate(PendingGeometryNormal);
        return;
    }
    doMove(x, y);
    updateWindowRules(Rules::Position);
    screens()->setCurrent(this);
    workspace()->updateStackingOrder();
    // client itself is not damaged
    addRepaintDuringGeometryUpdates();
    updateGeometryBeforeUpdateBlocking();
    emit geometryChanged();
    Q_EMIT frameGeometryChanged(this, old_frame_geometry);
}

// When the user pressed mouse on the titlebar, don't activate move immediately,
// since it may be just a click. Activate instead after a delay. Move used to be
// activated only after moving by several pixels, but that looks bad.
void AbstractClient::startDelayedMoveResize()
{
    Q_ASSERT(!m_moveResize.delayedTimer);
    m_moveResize.delayedTimer = new QTimer(this);
    m_moveResize.delayedTimer->setSingleShot(true);
    connect(m_moveResize.delayedTimer, &QTimer::timeout, this,
        [this]() {
            Q_ASSERT(isMoveResizePointerButtonDown());
            if (!win::start_move_resize(this)) {
                setMoveResizePointerButtonDown(false);
            }
            updateCursor();
            stopDelayedMoveResize();
        }
    );
    m_moveResize.delayedTimer->start(QApplication::startDragTime());
}

void AbstractClient::stopDelayedMoveResize()
{
    delete m_moveResize.delayedTimer;
    m_moveResize.delayedTimer = nullptr;
}

bool AbstractClient::hasStrut() const
{
    return false;
}

void AbstractClient::setupWindowManagementInterface()
{
    if (m_windowManagementInterface) {
        // already setup
        return;
    }
    if (!waylandServer() || !surface()) {
        return;
    }
    if (!waylandServer()->windowManagement()) {
        return;
    }
    using namespace Wrapland::Server;
    auto w = waylandServer()->windowManagement()->createWindow(waylandServer()->windowManagement());
    w->setTitle(caption());
    w->setActive(isActive());
    w->setFullscreen(isFullScreen());
    w->setKeepAbove(keepAbove());
    w->setKeepBelow(keepBelow());
    w->setMaximized(maximizeMode() == win::maximize_mode::full);
    w->setMinimized(isMinimized());
    w->setOnAllDesktops(isOnAllDesktops());
    w->setDemandsAttention(isDemandingAttention());
    w->setCloseable(isCloseable());
    w->setMaximizeable(isMaximizable());
    w->setMinimizeable(isMinimizable());
    w->setFullscreenable(isFullScreenable());
    w->setIcon(icon());
    auto updateAppId = [this, w] {
        w->setAppId(QString::fromUtf8(m_desktopFileName.isEmpty() ? resourceClass() : m_desktopFileName));
    };
    updateAppId();
    w->setSkipTaskbar(skipTaskbar());
    w->setSkipSwitcher(skipSwitcher());
    w->setPid(pid());
    w->setShadeable(isShadeable());
    w->setShaded(isShade());
    w->setResizable(isResizable());
    w->setMovable(isMovable());
    w->setVirtualDesktopChangeable(true); // FIXME Matches X11Client::actionSupported(), but both should be implemented.
    w->setParentWindow(transientFor() ? transientFor()->windowManagementInterface() : nullptr);
    w->setGeometry(frameGeometry());
    connect(this, &AbstractClient::skipTaskbarChanged, w,
        [w, this] {
            w->setSkipTaskbar(skipTaskbar());
        }
    );
    connect(this, &AbstractClient::skipSwitcherChanged, w,
         [w, this] {
            w->setSkipSwitcher(skipSwitcher());
        }
    );
    connect(this, &AbstractClient::captionChanged, w, [w, this] { w->setTitle(caption()); });

    connect(this, &AbstractClient::activeChanged, w, [w, this] { w->setActive(isActive()); });
    connect(this, &AbstractClient::fullScreenChanged, w, [w, this] { w->setFullscreen(isFullScreen()); });
    connect(this, &AbstractClient::keepAboveChanged, w, &PlasmaWindow::setKeepAbove);
    connect(this, &AbstractClient::keepBelowChanged, w, &PlasmaWindow::setKeepBelow);
    connect(this, &AbstractClient::minimizedChanged, w, [w, this] { w->setMinimized(isMinimized()); });
    connect(this, static_cast<void (AbstractClient::*)(AbstractClient*,win::maximize_mode)>(&AbstractClient::clientMaximizedStateChanged), w,
        [w] (KWin::AbstractClient *c, win::maximize_mode mode) {
            Q_UNUSED(c);
            w->setMaximized(mode == win::maximize_mode::full);
        }
    );
    connect(this, &AbstractClient::demandsAttentionChanged, w, [w, this] { w->setDemandsAttention(isDemandingAttention()); });
    connect(this, &AbstractClient::iconChanged, w,
        [w, this] {
            w->setIcon(icon());
        }
    );
    connect(this, &AbstractClient::windowClassChanged, w, updateAppId);
    connect(this, &AbstractClient::desktopFileNameChanged, w, updateAppId);
    connect(this, &AbstractClient::shadeChanged, w, [w, this] { w->setShaded(isShade()); });
    connect(this, &AbstractClient::transientChanged, w,
        [w, this] {
            w->setParentWindow(transientFor() ? transientFor()->windowManagementInterface() : nullptr);
        }
    );
    connect(this, &AbstractClient::geometryChanged, w,
        [w, this] {
            w->setGeometry(frameGeometry());
        }
    );
    connect(w, &PlasmaWindow::closeRequested, this, [this] { closeWindow(); });
    connect(w, &PlasmaWindow::moveRequested, this,
        [this] {
            Cursor::setPos(frameGeometry().center());
            performMouseCommand(Options::MouseMove, Cursor::pos());
        }
    );
    connect(w, &PlasmaWindow::resizeRequested, this,
        [this] {
            Cursor::setPos(frameGeometry().bottomRight());
            performMouseCommand(Options::MouseResize, Cursor::pos());
        }
    );
    connect(w, &PlasmaWindow::fullscreenRequested, this,
        [this] (bool set) {
            setFullScreen(set, false);
        }
    );
    connect(w, &PlasmaWindow::minimizedRequested, this,
        [this] (bool set) {
            if (set) {
                minimize();
            } else {
                unminimize();
            }
        }
    );
    connect(w, &PlasmaWindow::maximizedRequested, this,
        [this] (bool set) {
            win::maximize(this, set ? win::maximize_mode::full : win::maximize_mode::restore);
        }
    );
    connect(w, &PlasmaWindow::keepAboveRequested, this,
        [this] (bool set) {
            setKeepAbove(set);
        }
    );
    connect(w, &PlasmaWindow::keepBelowRequested, this,
        [this] (bool set) {
            setKeepBelow(set);
        }
    );
    connect(w, &PlasmaWindow::demandsAttentionRequested, this,
        [this] (bool set) {
            demandAttention(set);
        }
    );
    connect(w, &PlasmaWindow::activeRequested, this,
        [this] (bool set) {
            if (set) {
                workspace()->activateClient(this, true);
            }
        }
    );
    connect(w, &PlasmaWindow::shadedRequested, this,
        [this] (bool set) {
            setShade(set);
        }
    );

    for (const auto vd : m_desktops) {
        w->addPlasmaVirtualDesktop(vd->id());
    }

    //this is only for the legacy
    connect(this, &AbstractClient::desktopChanged, w,
        [w, this] {
            if (isOnAllDesktops()) {
                w->setOnAllDesktops(true);
                return;
            }
            w->setOnAllDesktops(false);
        }
    );

    //Plasma Virtual desktop management
    //show/hide when the window enters/exits from desktop
    connect(w, &PlasmaWindow::enterPlasmaVirtualDesktopRequested, this,
        [this] (const QString &desktopId) {
            VirtualDesktop *vd = VirtualDesktopManager::self()->desktopForId(desktopId.toUtf8());
            if (vd) {
                enterDesktop(vd);
            }
        }
    );
    connect(w, &PlasmaWindow::enterNewPlasmaVirtualDesktopRequested, this,
        [this] () {
            VirtualDesktopManager::self()->setCount(VirtualDesktopManager::self()->count() + 1);
            enterDesktop(VirtualDesktopManager::self()->desktops().last());
        }
    );
    connect(w, &PlasmaWindow::leavePlasmaVirtualDesktopRequested, this,
        [this] (const QString &desktopId) {
            VirtualDesktop *vd = VirtualDesktopManager::self()->desktopForId(desktopId.toUtf8());
            if (vd) {
                leaveDesktop(vd);
            }
        }
    );

    m_windowManagementInterface = w;
}

void AbstractClient::destroyWindowManagementInterface()
{
    if (m_windowManagementInterface) {
        m_windowManagementInterface->unmap();
        m_windowManagementInterface = nullptr;
    }
}

bool AbstractClient::performMouseCommand(Options::MouseCommand cmd, const QPoint &globalPos)
{
    return win::perform_mouse_command(this, cmd, globalPos);
}

void AbstractClient::setTransientFor(AbstractClient *transientFor)
{
    if (transientFor == this) {
        // cannot be transient for one self
        return;
    }
    if (m_transientFor == transientFor) {
        return;
    }
    m_transientFor = transientFor;
    emit transientChanged();
}

const AbstractClient *AbstractClient::transientFor() const
{
    return m_transientFor;
}

AbstractClient *AbstractClient::transientFor()
{
    return m_transientFor;
}

bool AbstractClient::hasTransientPlacementHint() const
{
    return false;
}

QRect AbstractClient::transientPlacement(const QRect &bounds) const
{
    Q_UNUSED(bounds);
    Q_UNREACHABLE();
    return QRect();
}

bool AbstractClient::hasTransient(const AbstractClient *c, bool indirect) const
{
    Q_UNUSED(indirect);
    return c->transientFor() == this;
}

QList< AbstractClient* > AbstractClient::mainClients() const
{
    if (const AbstractClient *t = transientFor()) {
        return QList<AbstractClient*>{const_cast< AbstractClient* >(t)};
    }
    return QList<AbstractClient*>();
}

void AbstractClient::setModal(bool m)
{
    // Qt-3.2 can have even modal normal windows :(
    if (m_modal == m)
        return;
    m_modal = m;
    emit modalChanged();
    // Changing modality for a mapped window is weird (?)
    // _NET_WM_STATE_MODAL should possibly rather be _NET_WM_WINDOW_TYPE_MODAL_DIALOG
}

bool AbstractClient::isModal() const
{
    return m_modal;
}

void AbstractClient::addTransient(AbstractClient *cl)
{
    Q_ASSERT(!m_transients.contains(cl));
    Q_ASSERT(cl != this);
    m_transients.append(cl);
}

void AbstractClient::removeTransient(AbstractClient *cl)
{
    m_transients.removeAll(cl);
    if (cl->transientFor() == this) {
        cl->setTransientFor(nullptr);
    }
}

void AbstractClient::removeTransientFromList(AbstractClient *cl)
{
    m_transients.removeAll(cl);
}

#define BORDER(which) \
    int AbstractClient::border##which() const \
    { \
        return isDecorated() ? decoration()->border##which() : 0; \
    }

BORDER(Bottom)
BORDER(Left)
BORDER(Right)
BORDER(Top)
#undef BORDER

QSize AbstractClient::sizeForClientSize(const QSize &wsize,
                                        [[maybe_unused]] win::size_mode mode,
                                        [[maybe_unused]] bool noframe) const
{
    return wsize + QSize(borderLeft() + borderRight(), borderTop() + borderBottom());
}

void AbstractClient::addRepaintDuringGeometryUpdates()
{
    const QRect deco_rect = visibleRect();
    addLayerRepaint(m_visibleRectBeforeGeometryUpdate);
    addLayerRepaint(deco_rect);   // trigger repaint of window's new location
    m_visibleRectBeforeGeometryUpdate = deco_rect;
}

QRect AbstractClient::bufferGeometryBeforeUpdateBlocking() const
{
    return m_bufferGeometryBeforeUpdateBlocking;
}

QRect AbstractClient::frameGeometryBeforeUpdateBlocking() const
{
    return m_frameGeometryBeforeUpdateBlocking;
}

void AbstractClient::updateGeometryBeforeUpdateBlocking()
{
    m_bufferGeometryBeforeUpdateBlocking = bufferGeometry();
    m_frameGeometryBeforeUpdateBlocking = frameGeometry();
}

void AbstractClient::doMove(int, int)
{
}

void AbstractClient::updateInitialMoveResizeGeometry()
{
    m_moveResize.initialGeometry = frameGeometry();
    m_moveResize.geometry = m_moveResize.initialGeometry;
    m_moveResize.startScreen = screen();
}

void AbstractClient::updateCursor()
{
    auto m = moveResizePointerMode();
    if (!isResizable() || isShade())
        m = win::position::center;
    CursorShape c = Qt::ArrowCursor;
    switch(m) {
    case win::position::top_left:
        c = KWin::ExtendedCursor::SizeNorthWest;
        break;
    case win::position::bottom_right:
        c = KWin::ExtendedCursor::SizeSouthEast;
        break;
    case win::position::bottom_left:
        c = KWin::ExtendedCursor::SizeSouthWest;
        break;
    case win::position::top_right:
        c = KWin::ExtendedCursor::SizeNorthEast;
        break;
    case win::position::top:
        c = KWin::ExtendedCursor::SizeNorth;
        break;
    case win::position::bottom:
        c = KWin::ExtendedCursor::SizeSouth;
        break;
    case win::position::left:
        c = KWin::ExtendedCursor::SizeWest;
        break;
    case win::position::right:
        c = KWin::ExtendedCursor::SizeEast;
        break;
    default:
        if (isMoveResize())
            c = Qt::SizeAllCursor;
        else
            c = Qt::ArrowCursor;
        break;
    }
    if (c == m_moveResize.cursor)
        return;
    m_moveResize.cursor = c;
    emit moveResizeCursorChanged(c);
}

void AbstractClient::leaveMoveResize()
{
    workspace()->setMoveResizeClient(nullptr);
    setMoveResize(false);
    if (ScreenEdges::self()->isDesktopSwitchingMovingClients())
        ScreenEdges::self()->reserveDesktopSwitching(false, Qt::Vertical|Qt::Horizontal);
    if (isElectricBorderMaximizing()) {
        outline()->hide();
        win::elevate(this, false);
    }
}

bool AbstractClient::s_haveResizeEffect = false;

void AbstractClient::updateHaveResizeEffect()
{
    s_haveResizeEffect = effects && static_cast<EffectsHandlerImpl*>(effects)->provides(Effect::Resize);
}

bool AbstractClient::doStartMoveResize()
{
    return true;
}

void AbstractClient::positionGeometryTip()
{
}

void AbstractClient::doPerformMoveResize()
{
}

bool AbstractClient::isWaitingForMoveResizeSync() const
{
    return false;
}

void AbstractClient::doResizeSync()
{
}

void AbstractClient::checkQuickTilingMaximizationZones(int xroot, int yroot)
{
    QuickTileMode mode = QuickTileFlag::None;
    bool innerBorder = false;
    for (int i=0; i < screens()->count(); ++i) {

        if (!screens()->geometry(i).contains(QPoint(xroot, yroot)))
            continue;

        auto isInScreen = [i](const QPoint &pt) {
            for (int j = 0; j < screens()->count(); ++j) {
                if (j == i)
                    continue;
                if (screens()->geometry(j).contains(pt)) {
                    return true;
                }
            }
            return false;
        };

        QRect area = workspace()->clientArea(MaximizeArea, QPoint(xroot, yroot), desktop());
        if (options->electricBorderTiling()) {
            if (xroot <= area.x() + 20) {
                mode |= QuickTileFlag::Left;
                innerBorder = isInScreen(QPoint(area.x() - 1, yroot));
            } else if (xroot >= area.x() + area.width() - 20) {
                mode |= QuickTileFlag::Right;
                innerBorder = isInScreen(QPoint(area.right() + 1, yroot));
            }
        }

        if (mode != QuickTileMode(QuickTileFlag::None)) {
            if (yroot <= area.y() + area.height() * options->electricBorderCornerRatio())
                mode |= QuickTileFlag::Top;
            else if (yroot >= area.y() + area.height() - area.height()  * options->electricBorderCornerRatio())
                mode |= QuickTileFlag::Bottom;
        } else if (options->electricBorderMaximize() && yroot <= area.y() + 5 && isMaximizable()) {
            mode = QuickTileFlag::Maximize;
            innerBorder = isInScreen(QPoint(xroot, area.y() - 1));
        }
        break; // no point in checking other screens to contain this... "point"...
    }
    if (mode != electricBorderMode()) {
        setElectricBorderMode(mode);
        if (innerBorder) {
            delayed_electric_maximize();
        } else {
            setElectricBorderMaximizing(mode != QuickTileMode(QuickTileFlag::None));
        }
    }
}

void AbstractClient::delayed_electric_maximize()
{
    if (!m_electricMaximizingDelay) {
        m_electricMaximizingDelay = new QTimer(this);
        m_electricMaximizingDelay->setInterval(250);
        m_electricMaximizingDelay->setSingleShot(true);
        connect(m_electricMaximizingDelay, &QTimer::timeout, [this]() {
            if (win::is_move(this)) {
                setElectricBorderMaximizing(electricBorderMode() != QuickTileMode(QuickTileFlag::None));
            }
        });
    }
    m_electricMaximizingDelay->start();
}

void AbstractClient::keyPressEvent(uint key_code)
{
    win::key_press_event(this, key_code);
}

QSize AbstractClient::resizeIncrements() const
{
    return QSize(1, 1);
}

void AbstractClient::setMoveResizePointerMode(win::position mode) {
    m_moveResize.pointer = mode;
}

void AbstractClient::destroyDecoration()
{
    delete m_decoration.decoration;
    m_decoration.decoration = nullptr;
}

void AbstractClient::layoutDecorationRects(QRect &left, QRect &top, QRect &right, QRect &bottom) const
{
    win::layout_decoration_rects(this, left, top, right, bottom);
}

bool AbstractClient::processDecorationButtonPress(QMouseEvent *event, bool ignoreMenu)
{
    Options::MouseCommand com = Options::MouseNothing;
    bool active = isActive();
    if (!wantsInput())    // we cannot be active, use it anyway
        active = true;

    // check whether it is a double click
    if (event->button() == Qt::LeftButton && win::titlebar_positioned_under_mouse(this)) {
        if (m_decoration.doubleClickTimer.isValid()) {
            const qint64 interval = m_decoration.doubleClickTimer.elapsed();
            m_decoration.doubleClickTimer.invalidate();
            if (interval > QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
                m_decoration.doubleClickTimer.start(); // expired -> new first click and pot. init
            } else {
                Workspace::self()->performWindowOperation(this, options->operationTitlebarDblClick());
                win::dont_move_resize(this);
                return false;
            }
        }
         else {
            m_decoration.doubleClickTimer.start(); // new first click and pot. init, could be invalidated by release - see below
        }
    }

    if (event->button() == Qt::LeftButton)
        com = active ? options->commandActiveTitlebar1() : options->commandInactiveTitlebar1();
    else if (event->button() == Qt::MiddleButton)
        com = active ? options->commandActiveTitlebar2() : options->commandInactiveTitlebar2();
    else if (event->button() == Qt::RightButton)
        com = active ? options->commandActiveTitlebar3() : options->commandInactiveTitlebar3();
    if (event->button() == Qt::LeftButton
            && com != Options::MouseOperationsMenu // actions where it's not possible to get the matching
            && com != Options::MouseMinimize)  // mouse release event
    {
        setMoveResizePointerMode(win::mouse_position(this));
        setMoveResizePointerButtonDown(true);
        setMoveOffset(event->pos());
        setInvertedMoveOffset(rect().bottomRight() - moveOffset());
        setUnrestrictedMoveResize(false);
        startDelayedMoveResize();
        updateCursor();
    }
    // In the new API the decoration may process the menu action to display an inactive tab's menu.
    // If the event is unhandled then the core will create one for the active window in the group.
    if (!ignoreMenu || com != Options::MouseOperationsMenu)
        performMouseCommand(com, event->globalPos());
    return !( // Return events that should be passed to the decoration in the new API
               com == Options::MouseRaise ||
               com == Options::MouseOperationsMenu ||
               com == Options::MouseActivateAndRaise ||
               com == Options::MouseActivate ||
               com == Options::MouseActivateRaiseAndPassClick ||
               com == Options::MouseActivateAndPassClick ||
               com == Options::MouseNothing);
}

void AbstractClient::startDecorationDoubleClickTimer()
{
    m_decoration.doubleClickTimer.start();
}

void AbstractClient::invalidateDecorationDoubleClickTimer()
{
    m_decoration.doubleClickTimer.invalidate();
}

bool AbstractClient::providesContextHelp() const
{
    return false;
}

void AbstractClient::showContextHelp()
{
}

QPointer<Decoration::DecoratedClientImpl> AbstractClient::decoratedClient() const
{
    return m_decoration.client;
}

void AbstractClient::setDecoratedClient(QPointer< Decoration::DecoratedClientImpl > client)
{
    m_decoration.client = client;
}

QRect AbstractClient::iconGeometry() const
{
    if (!windowManagementInterface() || !waylandServer()) {
        // window management interface is only available if the surface is mapped
        return QRect();
    }

    int minDistance = INT_MAX;
    AbstractClient *candidatePanel = nullptr;
    QRect candidateGeom;

    for (auto i = windowManagementInterface()->minimizedGeometries().constBegin(), end = windowManagementInterface()->minimizedGeometries().constEnd(); i != end; ++i) {
        AbstractClient *client = waylandServer()->findAbstractClient(i.key());
        if (!client) {
            continue;
        }
        const int distance = QPoint(client->pos() - pos()).manhattanLength();
        if (distance < minDistance) {
            minDistance = distance;
            candidatePanel = client;
            candidateGeom = i.value();
        }
    }
    if (!candidatePanel) {
        return QRect();
    }
    return candidateGeom.translated(candidatePanel->pos());
}

QRect AbstractClient::inputGeometry() const
{
    if (isDecorated()) {
        return Toplevel::inputGeometry() + decoration()->resizeOnlyBorders();
    }
    return Toplevel::inputGeometry();
}

bool AbstractClient::dockWantsInput() const
{
    return false;
}

void AbstractClient::setDesktopFileName(QByteArray name)
{
    name = rules()->checkDesktopFile(name).toUtf8();
    if (name == m_desktopFileName) {
        return;
    }
    m_desktopFileName = name;
    updateWindowRules(Rules::DesktopFile);
    emit desktopFileNameChanged();
}

QString AbstractClient::iconFromDesktopFile() const
{
    const QString desktopFileName = QString::fromUtf8(m_desktopFileName);
    QString desktopFilePath;

    if (QDir::isAbsolutePath(desktopFileName)) {
        desktopFilePath = desktopFileName;
    }

    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 desktopFileName);
    }
    if (desktopFilePath.isEmpty()) {
        desktopFilePath = QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                                                 desktopFileName + QLatin1String(".desktop"));
    }

    KDesktopFile df(desktopFilePath);
    return df.readIcon();
}

bool AbstractClient::hasApplicationMenu() const
{
    return ApplicationMenu::self()->applicationMenuEnabled() && !m_applicationMenuServiceName.isEmpty() && !m_applicationMenuObjectPath.isEmpty();
}

void AbstractClient::updateApplicationMenuServiceName(const QString &serviceName)
{
    const bool old_hasApplicationMenu = hasApplicationMenu();

    m_applicationMenuServiceName = serviceName;

    const bool new_hasApplicationMenu = hasApplicationMenu();

    if (old_hasApplicationMenu != new_hasApplicationMenu) {
        emit hasApplicationMenuChanged(new_hasApplicationMenu);
    }
}

void AbstractClient::updateApplicationMenuObjectPath(const QString &objectPath)
{
    const bool old_hasApplicationMenu = hasApplicationMenu();

    m_applicationMenuObjectPath = objectPath;

    const bool new_hasApplicationMenu = hasApplicationMenu();

    if (old_hasApplicationMenu != new_hasApplicationMenu) {
        emit hasApplicationMenuChanged(new_hasApplicationMenu);
    }
}

void AbstractClient::setApplicationMenuActive(bool applicationMenuActive)
{
    if (m_applicationMenuActive != applicationMenuActive) {
        m_applicationMenuActive = applicationMenuActive;
        emit applicationMenuActiveChanged(applicationMenuActive);
    }
}

bool AbstractClient::unresponsive() const
{
    return m_unresponsive;
}

void AbstractClient::setUnresponsive(bool unresponsive)
{
    if (m_unresponsive != unresponsive) {
        m_unresponsive = unresponsive;
        emit unresponsiveChanged(m_unresponsive);
        emit captionChanged();
    }
}

// We need to keep this function for now because of inheritance of child classes (InternalClient).
// TODO: remove when our inheritance hierarchy is flattened.
AbstractClient *AbstractClient::findClientWithSameCaption() const
{
    return win::find_client_with_same_caption(this);
}

QString AbstractClient::caption() const
{
    QString cap = captionNormal() + captionSuffix();
    if (unresponsive()) {
        cap += QLatin1String(" ");
        cap += i18nc("Application is not responding, appended to window title", "(Not Responding)");
    }
    return cap;
}

void AbstractClient::removeRule(Rules* rule)
{
    m_rules.remove(rule);
}

void AbstractClient::discardTemporaryRules()
{
    m_rules.discardTemporary();
}

void AbstractClient::evaluateWindowRules()
{
    setupWindowRules(true);
    applyWindowRules();
}

void AbstractClient::setOnActivities(QStringList newActivitiesList)
{
    Q_UNUSED(newActivitiesList)
}

void AbstractClient::checkNoBorder()
{
    setNoBorder(false);
}

bool AbstractClient::groupTransient() const
{
    return false;
}

const Group *AbstractClient::group() const
{
    return nullptr;
}

Group *AbstractClient::group()
{
    return nullptr;
}

bool AbstractClient::isInternal() const
{
    return false;
}

bool AbstractClient::supportsWindowRules() const
{
    return true;
}

QMargins AbstractClient::frameMargins() const
{
    return QMargins(borderLeft(), borderTop(), borderRight(), borderBottom());
}

QPoint AbstractClient::framePosToClientPos(const QPoint &point) const
{
    return point + QPoint(borderLeft(), borderTop());
}

QPoint AbstractClient::clientPosToFramePos(const QPoint &point) const
{
    return point - QPoint(borderLeft(), borderTop());
}

QSize AbstractClient::frameSizeToClientSize(const QSize &size) const
{
    const int width = size.width() - borderLeft() - borderRight();
    const int height = size.height() - borderTop() - borderBottom();
    return QSize(width, height);
}

QSize AbstractClient::clientSizeToFrameSize(const QSize &size) const
{
    const int width = size.width() + borderLeft() + borderRight();
    const int height = size.height() + borderTop() + borderBottom();
    return QSize(width, height);
}

QRect AbstractClient::frameRectToClientRect(const QRect &rect) const
{
    const QPoint position = framePosToClientPos(rect.topLeft());
    const QSize size = frameSizeToClientSize(rect.size());
    return QRect(position, size);
}

QRect AbstractClient::clientRectToFrameRect(const QRect &rect) const
{
    const QPoint position = clientPosToFramePos(rect.topLeft());
    const QSize size = clientSizeToFrameSize(rect.size());
    return QRect(position, size);
}

void AbstractClient::setElectricBorderMode(QuickTileMode mode)
{
    if (mode != QuickTileMode(QuickTileFlag::Maximize)) {
        // sanitize the mode, ie. simplify "invalid" combinations
        if ((mode & QuickTileFlag::Horizontal) == QuickTileMode(QuickTileFlag::Horizontal))
            mode &= ~QuickTileMode(QuickTileFlag::Horizontal);
        if ((mode & QuickTileFlag::Vertical) == QuickTileMode(QuickTileFlag::Vertical))
            mode &= ~QuickTileMode(QuickTileFlag::Vertical);
    }
    m_electricMode = mode;
}

void AbstractClient::setElectricBorderMaximizing(bool maximizing)
{
    m_electricMaximizing = maximizing;

    if (maximizing)
        outline()->show(win::electric_border_maximize_geometry(this, Cursor::pos(), desktop()),
                        moveResizeGeometry());
    else
        outline()->hide();

    win::elevate(this, maximizing);
}

void AbstractClient::set_QuickTileMode_win(QuickTileMode mode)
{
    m_quickTileMode = mode;
}

QSize AbstractClient::basicUnit() const
{
    return QSize(1, 1);
}

void AbstractClient::setBlockingCompositing([[maybe_unused]] bool block)
{
}

bool AbstractClient::isBlockingCompositing()
{
    return false;
}

}
