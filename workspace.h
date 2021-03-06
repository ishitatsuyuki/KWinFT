/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Lucas Murray <lmurray@undefinedfire.com>
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

#ifndef KWIN_WORKSPACE_H
#define KWIN_WORKSPACE_H

#include "options.h"
#include "sm.h"
#include "utils.h"

#include <QTimer>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

class KConfig;
class KConfigGroup;
class KStartupInfo;
class KStartupInfoData;
class KStartupInfoId;
class QStringList;

namespace KWin
{

namespace Xcb
{
class Tree;
class Window;
}

namespace win
{
enum class activation;
namespace x11
{
enum class predicate_match;
class window;
}
}

class Compositor;
class Group;
class InternalClient;
class KillWindow;
class ShortcutDialog;
class Toplevel;
class UserActionsMenu;
class X11EventFilter;

class X11EventFilterContainer : public QObject
{
    Q_OBJECT

public:
    explicit X11EventFilterContainer(X11EventFilter* filter);

    X11EventFilter* filter() const;

private:
    X11EventFilter* m_filter;
};

class KWIN_EXPORT Workspace : public QObject
{
    Q_OBJECT
public:
    std::vector<Toplevel*> m_windows;

    /**
     * Stacking orders reflect how windows are configured in z-direction.
     *
     * The unconstrainstrained_stacking_order is only a preliminary one which which Worskapce buidls
     * the stacking_order from.
     */
    std::deque<Toplevel*> unconstrained_stacking_order;
    std::deque<Toplevel*> stacking_order;

    explicit Workspace();
    ~Workspace() override;

    static Workspace* self() {
        return _self;
    }

    bool workspaceEvent(xcb_generic_event_t*);
    bool workspaceEvent(QEvent*);

    bool hasClient(win::x11::window const*);
    bool hasClient(Toplevel const* window);

    /**
     * @brief Finds the first Client matching the condition expressed by passed in @p func.
     *
     * Internally findClient uses the std::find_if algorithm and that determines how the function
     * needs to be implemented. An example usage for finding a Client with a matching windowId
     * @code
     * xcb_window_t w; // our test window
     * auto client = findClient([w](win::x11::window const* c) -> bool {
     *     return c->window() == w;
     * });
     * @endcode
     *
     * For the standard cases of matching the window id with one of the Client's windows use
     * the simplified overload method findClient(win::x11::predicate_match, xcb_window_t).
     * Above example can be simplified to:
     * @code
     * xcb_window_t w; // our test window
     * auto client = findClient(win::x11::predicate_match::window, w);
     * @endcode
     *
     * @param func Unary function that accepts a win::x11::window* as argument and
     * returns a value convertible to bool. The value returned indicates whether the
     * win::x11::window* is considered a match in the context of this function.
     * The function shall not modify its argument.
     * This can either be a function pointer or a function object.
     * @return KWin::win::x11::window* The found Client or @c null
     * @see findClient(win::x11::predicate_match, xcb_window_t)
     */
    Toplevel* findAbstractClient(std::function<bool (Toplevel const*)> func) const;
    /**
     * @brief Finds the Client matching the given match @p predicate for the given window.
     *
     * @param predicate Which window should be compared
     * @param w The window id to test against
     * @return KWin::win::x11::window* The found Client or @c null
     * @see findClient(std::function<bool (win::x11::window const*)>)
     */
    win::x11::window* findClient(win::x11::predicate_match predicate, xcb_window_t w) const;
    void forEachAbstractClient(std::function<void (Toplevel*)> func);
    /**
     * @brief Finds the Unmanaged with the given window id.
     *
     * @param w The window id to search for
     * @return KWin::Unmanaged* Found Unmanaged or @c null if there is no Unmanaged with given Id.
     */
    Toplevel* findUnmanaged(xcb_window_t w) const;
    Toplevel* findToplevel(std::function<bool (Toplevel const*)> func) const;
    void forEachToplevel(std::function<void (Toplevel*)> func);
    /**
     * @brief Finds a Toplevel for the internal window @p w.
     *
     * Internal window means a window created by KWin itself. On X11 this is an Unmanaged
     * and mapped by the window id, on Wayland a XdgShellClient mapped on the internal window id.
     *
     * @returns Toplevel
     */
    Toplevel* findInternal(QWindow *w) const;

    QRect clientArea(clientAreaOption, const QPoint& p, int desktop) const;
    QRect clientArea(clientAreaOption, Toplevel const* window) const;
    QRect clientArea(clientAreaOption, int screen, int desktop) const;

    QRegion restrictedMoveArea(int desktop, StrutAreas areas = StrutAreaAll) const;

    bool initializing() const;

    /**
     * Returns the active client, i.e. the client that has the focus (or None
     * if no client has the focus)
     */
    Toplevel* activeClient() const;
    /**
     * Client that was activated, but it's not yet really activeClient(), because
     * we didn't process yet the matching FocusIn event. Used mostly in focus
     * stealing prevention code.
     */
    Toplevel* mostRecentlyActivatedClient() const;

    Toplevel* clientUnderMouse(int screen) const;

    void activateClient(Toplevel* window, bool force = false);

    /**
     * Request focus and optionally try raising the window.
     * @param window The window to focus.
     * @param raise Should additionally raise the window.
     * @param force_focus Focus even if panel, dock and so on.
     */
    void request_focus(Toplevel* window, bool raise = false, bool force_focus = false);

    bool allowClientActivation(Toplevel const* window, xcb_timestamp_t time = -1U, bool focus_in = false,
                               bool ignore_desktop = false);
    void restoreFocus();
    void gotFocusIn(Toplevel const* window);
    void setShouldGetFocus(Toplevel* window);
    bool activateNextClient(Toplevel* window);
    bool focusChangeEnabled() {
        return block_focus == 0;
    }

    /**
     * Indicates that the client c is being moved or resized by the user.
     */
    void setMoveResizeClient(Toplevel* window);

    QPoint adjustClientPosition(Toplevel* window, QPoint pos, bool unrestricted, double snapAdjust = 1.0);
    QRect adjustClientSize(Toplevel* window, QRect moveResizeGeom, win::position mode);
    void raise_window(Toplevel* window);
    void lower_window(Toplevel* window);
    void raiseClientRequest(Toplevel* c, NET::RequestSource src = NET::FromApplication, xcb_timestamp_t timestamp = 0);
    void lowerClientRequest(win::x11::window* c, NET::RequestSource src, xcb_timestamp_t timestamp);
    void lowerClientRequest(Toplevel* window);
    void restackClientUnderActive(Toplevel*);
    void restack(Toplevel* window, Toplevel* under, bool force = false);
    void updateClientLayer(Toplevel* window);
    void raiseOrLowerClient(Toplevel* window);

    void stopUpdateToolWindowsTimer();
    void resetUpdateToolWindowsTimer();

    void restoreSessionStackingOrder(win::x11::window* c);
    void updateStackingOrder(bool propagate_new_clients = false);
    void forceRestacking();
    void markXStackingOrderAsDirty();

    void clientHidden(Toplevel* window);
    void clientAttentionChanged(Toplevel* window, bool set);

    std::vector<Toplevel*> const& windows() const;

    /**
     * @return List of unmanaged "clients" currently registered in Workspace
     */
    std::vector<Toplevel*> unmanagedList() const;
    /**
     * @return Remnant windows, i.e. already closed but still kept around for closing effects.
     */
    std::vector<Toplevel*> remnants() const;
    /**
     * @returns List of all clients (either X11 or Wayland) currently managed by Workspace
     */
    std::vector<Toplevel*> const& allClientList() const {
        return m_allClients;
    }

    void stackScreenEdgesUnderOverrideRedirect();

    SessionManager *sessionManager() const;

public:
    QPoint cascadeOffset(Toplevel const* window) const;

private:
    Compositor* m_compositor{nullptr};
    QTimer* m_quickTileCombineTimer{nullptr};
    win::quicktiles m_lastTilingMode{win::quicktiles::none};

    //-------------------------------------------------
    // Unsorted

public:
    // True when performing Workspace::updateClientArea().
    // The calls below are valid only in that case.
    bool inUpdateClientArea() const;
    QRegion previousRestrictedMoveArea(int desktop, StrutAreas areas = StrutAreaAll) const;
    std::vector< QRect > previousScreenSizes() const;
    int oldDisplayWidth() const;
    int oldDisplayHeight() const;

    /**
     * Returns the list of clients sorted in stacking order, with topmost client
     * at the last position
     */
    std::deque<Toplevel*> const& stackingOrder() const;
    std::deque<Toplevel*> const& xStackingOrder() const;
    std::deque<win::x11::window*> ensureStackingOrder(std::vector<win::x11::window*> const& clients) const;
    std::deque<Toplevel*> ensureStackingOrder(std::vector<Toplevel*> const& clients) const;

    Toplevel* topClientOnDesktop(int desktop, int screen, bool unconstrained = false,
                               bool only_normal = true) const;
    Toplevel* findDesktop(bool topmost, int desktop) const;
    void sendClientToDesktop(Toplevel* window, int desktop, bool dont_activate);
    void windowToPreviousDesktop(Toplevel* window);
    void windowToNextDesktop(Toplevel* window);
    void sendClientToScreen(Toplevel* window, int screen);

    void addManualOverlay(xcb_window_t id) {
        manual_overlays.push_back(id);
    }
    void removeManualOverlay(xcb_window_t id) {
        manual_overlays.erase(find(manual_overlays, id));
    }

    /**
     * Shows the menu operations menu for the client and makes it active if
     * it's not already.
     */
    void showWindowMenu(const QRect& pos, Toplevel* window);
    const UserActionsMenu *userActionsMenu() const {
        return m_userActionsMenu;
    }

    void showApplicationMenu(const QRect &pos, Toplevel* window, int actionId);

    void updateMinimizedOfTransients(Toplevel*);
    void updateOnAllDesktopsOfTransients(Toplevel* window);
    void checkTransients(Toplevel* window);

    void storeSession(const QString &sessionName, SMSavePhase phase);
    void storeClient(KConfigGroup &cg, int num, win::x11::window* c);
    void storeSubSession(const QString &name, QSet<QByteArray> sessionIds);
    void loadSubSessionInfo(const QString &name);

    SessionInfo* takeSessionInfo(win::x11::window*);

    // D-Bus interface
    QString supportInformation() const;

    void setCurrentScreen(int new_screen);

    void setShowingDesktop(bool showing);
    bool showingDesktop() const;

    // Only called from win::x11::window::destroyClient() or win::x11::window::releaseWindow()
    void removeClient(win::x11::window*);
    void setActiveClient(Toplevel* window);
    Group* findGroup(xcb_window_t leader) const;
    void addGroup(Group* group);
    void removeGroup(Group* group);

    // Only called from Unmanaged::release().
    void removeUnmanaged(Toplevel* window);
    void removeDeleted(Toplevel* window);
    void addDeleted(Toplevel* c, Toplevel* orig);

    bool checkStartupNotification(xcb_window_t w, KStartupInfoId& id, KStartupInfoData& data);

    void focusToNull(); // SELI TODO: Public?

    void clientShortcutUpdated(Toplevel* window);
    bool shortcutAvailable(const QKeySequence &cut, Toplevel* ignore = nullptr) const;
    bool globalShortcutsDisabled() const;
    void disableGlobalShortcutsForClient(bool disable);

    void setWasUserInteraction();
    bool wasUserInteraction() const;

    int packPositionLeft(Toplevel const* window, int oldX, bool leftEdge) const;
    int packPositionRight(Toplevel const* window, int oldX, bool rightEdge) const;
    int packPositionUp(Toplevel const* window, int oldY, bool topEdge) const;
    int packPositionDown(Toplevel const* window, int oldY, bool bottomEdge) const;

    void cancelDelayFocus();
    void requestDelayFocus(Toplevel*);

    /**
     * updates the mouse position to track whether a focus follow mouse focus change was caused by
     * an actual mouse move
     * is esp. called on enter/motion events of inactive windows
     * since an active window doesn't receive mouse events, it must also be invoked if a (potentially)
     * active window might be moved/resize away from the cursor (causing a leave event)
     */
    void updateFocusMousePosition(const QPoint& pos);
    QPoint focusMousePosition() const;

    /**
     * Returns a client that is currently being moved or resized by the user.
     *
     * If none of clients is being moved or resized, @c null will be returned.
     */
    Toplevel* moveResizeClient() {
        return movingClient;
    }

    /**
     * @returns Whether we have a Compositor and it is active (Scene created)
     */
    bool compositing() const;

    void registerEventFilter(X11EventFilter *filter);
    void unregisterEventFilter(X11EventFilter *filter);

    void quickTileWindow(win::quicktiles mode);

    enum Direction {
        DirectionNorth,
        DirectionEast,
        DirectionSouth,
        DirectionWest
    };
    void switchWindow(Direction direction);

    ShortcutDialog *shortcutDialog() const {
        return client_keys_dialog;
    }

    /**
     * Adds the internal client to Workspace.
     *
     * This method will be called by InternalClient when it's mapped.
     *
     * @see internalClientAdded
     * @internal
     */
    void addInternalClient(InternalClient *client);

    /**
     * Removes the internal client from Workspace.
     *
     * This method is meant to be called only by InternalClient.
     *
     * @see internalClientRemoved
     * @internal
     */
    void removeInternalClient(InternalClient *client);

    void remove_window(Toplevel* window);

public Q_SLOTS:
    void performWindowOperation(KWin::Toplevel* window, Options::WindowOperation op);
    // Keybindings
    //void slotSwitchToWindow( int );
    void slotWindowToDesktop(uint i);

    //void slotWindowToListPosition( int );
    void slotSwitchToScreen();
    void slotWindowToScreen();
    void slotSwitchToNextScreen();
    void slotWindowToNextScreen();
    void slotSwitchToPrevScreen();
    void slotWindowToPrevScreen();
    void slotToggleShowDesktop();

    void slotWindowMaximize();
    void slotWindowMaximizeVertical();
    void slotWindowMaximizeHorizontal();
    void slotWindowMinimize();
    void slotWindowRaise();
    void slotWindowLower();
    void slotWindowRaiseOrLower();
    void slotActivateAttentionWindow();
    void slotWindowPackLeft();
    void slotWindowPackRight();
    void slotWindowPackUp();
    void slotWindowPackDown();
    void slotWindowGrowHorizontal();
    void slotWindowGrowVertical();
    void slotWindowShrinkHorizontal();
    void slotWindowShrinkVertical();

    void slotIncreaseWindowOpacity();
    void slotLowerWindowOpacity();

    void slotWindowOperations();
    void slotWindowClose();
    void slotWindowMove();
    void slotWindowResize();
    void slotWindowAbove();
    void slotWindowBelow();
    void slotWindowOnAllDesktops();
    void slotWindowFullScreen();
    void slotWindowNoBorder();

    void slotWindowToNextDesktop();
    void slotWindowToPreviousDesktop();
    void slotWindowToDesktopRight();
    void slotWindowToDesktopLeft();
    void slotWindowToDesktopUp();
    void slotWindowToDesktopDown();

    void reconfigure();
    void slotReconfigure();

    void slotKillWindow();

    void slotSetupWindowShortcut();
    void setupWindowShortcutDone(bool);

    void updateClientArea();

private Q_SLOTS:
    void desktopResized();
    void selectWmInputEventMask();
    void slotUpdateToolWindows();
    void delayFocus();
    void slotReloadConfig();
    void updateCurrentActivity(const QString &new_activity);
    // virtual desktop handling
    void slotDesktopCountChanged(uint previousCount, uint newCount);
    void slotCurrentDesktopChanged(uint oldDesktop, uint newDesktop);

Q_SIGNALS:
    /**
     * Emitted after the Workspace has setup the complete initialization process.
     * This can be used to connect to for performing post-workspace initialization.
     */
    void workspaceInitialized();

    //Signals required for the scripting interface
    void desktopPresenceChanged(KWin::Toplevel*, int);
    void currentDesktopChanged(int, KWin::Toplevel*);
    void clientAdded(KWin::win::x11::window*);
    void clientRemoved(KWin::Toplevel*);
    void clientActivated(KWin::Toplevel*);
    void clientDemandsAttentionChanged(KWin::Toplevel*, bool);
    void clientMinimizedChanged(KWin::Toplevel*);
    void groupAdded(KWin::Group*);
    void unmanagedAdded(KWin::Toplevel*);
    void unmanagedRemoved(KWin::Toplevel*);
    void deletedRemoved(KWin::Toplevel*);
    void configChanged();
    void showingDesktopChanged(bool showing);
    /**
     * This signal is emitted when the stacking order changed, i.e. a window is risen
     * or lowered
     */
    void stackingOrderChanged();

    /**
     * This signal is emitted whenever an internal client is created.
     */
    void internalClientAdded(KWin::InternalClient *client);

    /**
     * This signal is emitted whenever an internal client gets removed.
     */
    void internalClientRemoved(KWin::InternalClient *client);

private:
    void init();
    void initWithX11();
    void initShortcuts();
    template <typename Slot>
    void initShortcut(const QString &actionName, const QString &description, const QKeySequence &shortcut,
                      Slot slot, const QVariant &data = QVariant());
    template <typename T, typename Slot>
    void initShortcut(const QString &actionName, const QString &description, const QKeySequence &shortcut, T *receiver, Slot slot, const QVariant &data = QVariant());
    void setupWindowShortcut(Toplevel* window);
    bool switchWindow(Toplevel* c, Direction direction, QPoint curPos, int desktop);

    void propagateClients(bool propagate_new_clients);   // Called only from updateStackingOrder
    std::deque<Toplevel*> constrainedStackingOrder();
    void raiseClientWithinApplication(Toplevel* window);
    void lowerClientWithinApplication(Toplevel* window);
    bool allowFullClientRaising(Toplevel const* c, xcb_timestamp_t timestamp);
    bool keepTransientAbove(Toplevel const* mainwindow, Toplevel const* transient);
    bool keepDeletedTransientAbove(Toplevel const* mainWindow, Toplevel const* transient) const;
    void blockStackingUpdates(bool block);
    void fixPositionAfterCrash(xcb_window_t w, const xcb_get_geometry_reply_t *geom);
    void saveOldScreenSizes();

    /// This is the right way to create a new client
    win::x11::window* createClient(xcb_window_t w, bool is_mapped);
    void setupClientConnections(Toplevel* window);
    void addClient(win::x11::window* c);
    Toplevel* createUnmanaged(xcb_window_t w);
    void addUnmanaged(Toplevel* c);

    //---------------------------------------------------------------------

    void closeActivePopup();
    void updateClientArea(bool force);
    void resetClientAreas(uint desktopCount);
    void activateClientOnNewDesktop(uint desktop);
    Toplevel* findClientToActivateOnDesktop(uint desktop);

    QWidget* active_popup{nullptr};
    Toplevel* active_popup_client{nullptr};

    int m_initialDesktop{1};
    void loadSessionInfo(const QString &sessionName);
    void addSessionInfo(KConfigGroup &cg);

    std::vector<SessionInfo*> session;

    void updateXStackingOrder();
    void updateTabbox();

    Toplevel* active_client{nullptr};
    Toplevel* last_active_client{nullptr};
    Toplevel* most_recently_raised{nullptr}; // Used ONLY by raiseOrLowerClient()
    Toplevel* movingClient{nullptr};

    // Delay(ed) window focus timer and client
    QTimer* delayFocusTimer{nullptr};
    Toplevel* delayfocus_client{nullptr};
    QPoint focusMousePos;

    std::vector<Toplevel*> m_allClients;

    // Topmost is last.
    std::deque<xcb_window_t> manual_overlays;

    bool force_restacking{false};

    // From XQueryTree()
    std::deque<Toplevel*> x_stacking;
    std::unique_ptr<Xcb::Tree> m_xStackingQueryTree;

    bool m_xStackingDirty{false};

    // Last is most recent.
    std::deque<Toplevel*> should_get_focus;
    std::deque<Toplevel*> attention_chain;

    bool showing_desktop{false};
    int m_remnant_count{0};

    std::vector<Group*> groups;

    bool was_user_interaction{false};
    QScopedPointer<X11EventFilter> m_wasUserInteractionFilter;

    int session_active_client;
    int session_desktop;

    int block_focus{0};

    /**
     * Holds the menu containing the user actions which is shown
     * on e.g. right click the window decoration.
     */
    UserActionsMenu *m_userActionsMenu;

    void modalActionsSwitch(bool enabled);

    ShortcutDialog* client_keys_dialog{nullptr};
    Toplevel* client_keys_client{nullptr};
    bool global_shortcuts_disabled_for_client{false};

    // Timer to collect requests for 'reconfigure'
    QTimer reconfigureTimer;

    QTimer updateToolWindowsTimer;

    static Workspace* _self;

    bool workspaceInit{true};

    KStartupInfo* startup{nullptr};

    // Array of workareas for virtual desktops
    std::vector<QRect> workarea;

    // Array of restricted areas that window cannot be moved into
    std::vector<StrutRects> restrictedmovearea;

    // Array of the previous restricted areas that window cannot be moved into
    std::vector<StrutRects> oldrestrictedmovearea;

    // Array of workareas per xinerama screen for all virtual desktops
    std::vector<std::vector<QRect>> screenarea;

    // array of previous sizes of xinerama screens
    std::vector< QRect > oldscreensizes;

    // previous sizes od displayWidth()/displayHeight()
    QSize olddisplaysize;

    int set_active_client_recursion{0};

    // When > 0, stacking updates are temporarily disabled
    int block_stacking_updates{0};

    // Propagate also new clients after enabling stacking updates?
    bool blocked_propagating_new_clients;

    QScopedPointer<Xcb::Window> m_nullFocus;
    friend class StackingUpdatesBlocker;

    QScopedPointer<KillWindow> m_windowKiller;

    std::vector<QPointer<X11EventFilterContainer>> m_eventFilters;
    std::vector<QPointer<X11EventFilterContainer>> m_genericEventFilters;

    QScopedPointer<X11EventFilter> m_movingClientFilter;
    QScopedPointer<X11EventFilter> m_syncAlarmFilter;

    SessionManager *m_sessionManager;
private:
    friend bool performTransiencyCheck();
    friend Workspace *workspace();
};

/**
 * Helper for Workspace::blockStackingUpdates() being called in pairs (True/false)
 */
class StackingUpdatesBlocker
{
public:
    explicit StackingUpdatesBlocker(Workspace* w)
        : ws(w) {
        ws->blockStackingUpdates(true);
    }
    ~StackingUpdatesBlocker() {
        ws->blockStackingUpdates(false);
    }

private:
    Workspace* ws;
};

class ColorMapper : public QObject
{
    Q_OBJECT
public:
    ColorMapper(QObject *parent);
    ~ColorMapper() override;
public Q_SLOTS:
    void update();
private:
    xcb_colormap_t m_default;
    xcb_colormap_t m_installed;
};

//---------------------------------------------------------
// Unsorted

inline bool Workspace::initializing() const
{
    return workspaceInit;
}

inline Toplevel* Workspace::activeClient() const
{
    return active_client;
}

inline Toplevel* Workspace::mostRecentlyActivatedClient() const
{
    return should_get_focus.size() > 0 ? should_get_focus.back() : active_client;
}

inline void Workspace::addGroup(Group* group)
{
    emit groupAdded(group);
    groups.push_back(group);
}

inline void Workspace::removeGroup(Group* group)
{
    remove_all(groups, group);
}

inline std::deque<Toplevel*> const& Workspace::stackingOrder() const
{
    // TODO: Q_ASSERT( block_stacking_updates == 0 );
    return stacking_order;
}

inline bool Workspace::wasUserInteraction() const
{
    return was_user_interaction;
}

inline SessionManager *Workspace::sessionManager() const
{
    return m_sessionManager;
}

inline bool Workspace::showingDesktop() const
{
    return showing_desktop;
}

inline bool Workspace::globalShortcutsDisabled() const
{
    return global_shortcuts_disabled_for_client;
}

inline void Workspace::forceRestacking()
{
    force_restacking = true;
    StackingUpdatesBlocker blocker(this);   // Do restacking if not blocked
}

inline void Workspace::updateFocusMousePosition(const QPoint& pos)
{
    focusMousePos = pos;
}

inline QPoint Workspace::focusMousePosition() const
{
    return focusMousePos;
}

inline Workspace *workspace()
{
    return Workspace::_self;
}

}

#endif
