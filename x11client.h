/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

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
#pragma once

#include "options.h"
#include "rules/rules.h"
#include "toplevel.h"
#include "win/meta.h"
#include "xcbutils.h"

#include <QElapsedTimer>
#include <QFlags>
#include <QPointer>
#include <QPixmap>
#include <QWindow>

#include <xcb/sync.h>

class QTimer;
class KStartupInfoData;
class KStartupInfoId;

namespace KWin
{

class x11_control;

/**
 * @brief Defines Predicates on how to search for a Client.
 *
 * Used by Workspace::findClient.
 */
enum class Predicate {
    WindowMatch,
    WrapperIdMatch,
    FrameIdMatch,
    InputIdMatch
};

class KWIN_EXPORT X11Client : public Toplevel
{
    Q_OBJECT

public:
    explicit X11Client();
    ~X11Client() override; ///< Use destroyClient() or releaseWindow()

    win::control* control() const override;

    xcb_window_t wrapperId() const;
    xcb_window_t inputId() const { return m_decoInputExtent; }
    xcb_window_t frameId() const override;

    QRect bufferGeometry() const override;

    QRect frameRectToBufferRect(const QRect &rect) const;

    bool groupTransient() const override;
    bool wasOriginallyGroupTransient() const;

    void checkTransient(xcb_window_t w) override;
    Toplevel* findModal() override;
    const Group* group() const override;
    Group* group() override;
    void changeClientLeaderGroup(Group* gr);
    void updateWindowRules(Rules::Types selection) override;
    void applyWindowRules() override;
    void updateFullscreenMonitors(NETFullscreenMonitors topology);

    bool hasNETSupport() const;

    QSize minSize() const override;
    QSize maxSize() const override;
    QSize basicUnit() const override;
    QSize clientSize() const override;
    QPoint inputPos() const { return input_offset; } // Inside of geometry()

    bool windowEvent(xcb_generic_event_t *e);

    bool manage(xcb_window_t w, bool isMapped);
    void releaseWindow(bool on_shutdown = false);
    void destroyClient();

    QStringList activities() const override;
    void setOnActivity(const QString &activity, bool enable);
    void setOnAllActivities(bool set) override;
    void setOnActivities(QStringList newActivitiesList) override;
    void updateActivities(bool includeTransients);
    void blockActivityUpdates(bool b = true) override;

    /// Is not minimized and not hidden. I.e. normally visible on some virtual desktop.
    bool isShown(bool shaded_is_shown) const override;
    bool isHiddenInternal() const override; // For compositing

    win::shade shadeMode() const override; // Prefer isShade()
    void setShade(win::shade mode) override;
    bool isShadeable() const override;

    bool isMaximizable() const override;
    QRect geometryRestore() const override;
    win::maximize_mode maximizeMode() const override;

    bool isMinimizable() const override;
    QRect iconGeometry() const override;

    void setFullScreen(bool set, bool user = true) override;
    bool userCanSetFullScreen() const override;
    QRect geometryFSRestore() const {
        // only for session saving
        return geom_fs_restore;
    }

    bool userNoBorder() const;
    bool noBorder() const override;
    void setNoBorder(bool set) override;
    bool userCanSetNoBorder() const override;
    void checkNoBorder() override;

    int sessionStackingOrder() const;

    // Auxiliary functions, depend on the windowType
    bool wantsInput() const override;

    bool isResizable() const override;
    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool isCloseable() const override; ///< May be closed by the user (May have a close button)

    void takeFocus() override;

    void updateDecoration(bool check_workspace_pos, bool force = false) override;

    void updateShape();

    void setFrameGeometry(QRect const& rect, win::force_geometry force = win::force_geometry::no) override;
    /// plainResize() simply resizes
    void plainResize(int w, int h, win::force_geometry force = win::force_geometry::no);
    void plainResize(const QSize& s, win::force_geometry force = win::force_geometry::no);

    /// resizeWithChecks() resizes according to gravity, and checks workarea position
    void resizeWithChecks(QSize const& size, win::force_geometry force = win::force_geometry::no) override;
    void resizeWithChecks(int w, int h, xcb_gravity_t gravity, win::force_geometry force = win::force_geometry::no);
    void resizeWithChecks(const QSize& s, xcb_gravity_t gravity, win::force_geometry force = win::force_geometry::no);

    QSize sizeForClientSize(const QSize&,
                            win::size_mode mode = win::size_mode::any,
                            bool noframe = false) const override;

    bool providesContextHelp() const override;

    bool performMouseCommand(Options::MouseCommand, const QPoint& globalPos) override;

    QRect adjustedClientArea(const QRect& desktop, const QRect& area) const;

    xcb_colormap_t colormap() const;

    /// Updates visibility depending on being shaded, virtual desktop, etc.
    void updateVisibility();
    /// Hides a client - Basically like minimize, but without effects, it's simply hidden
    void hideClient(bool hide) override;
    bool hiddenPreview() const; ///< Window is mapped in order to get a window pixmap

    bool setupCompositing(bool add_full_damage) override;
    void finishCompositing(ReleaseReason releaseReason = ReleaseReason::Release) override;
    void setBlockingCompositing(bool block) override;
    inline bool isBlockingCompositing() override { return blocks_compositing; }

    QString captionNormal() const override {
        return cap_normal;
    }
    QString captionSuffix() const override {
        return cap_suffix;
    }

    void keyPressEvent(uint key_code, xcb_timestamp_t time);   // FRAME ??
    xcb_window_t moveResizeGrabWindow() const;

    QPoint gravityAdjustment(xcb_gravity_t gravity) const;
    const QPoint calculateGravitation(bool invert) const;

    void NETMoveResize(int x_root, int y_root, NET::Direction direction);
    void NETMoveResizeWindow(int flags, int x, int y, int width, int height);
    void restackWindow(xcb_window_t above, int detail, NET::RequestSource source, xcb_timestamp_t timestamp,
                       bool send_event = false);

    void gotPing(xcb_timestamp_t timestamp);

    void updateUserTime(xcb_timestamp_t time = XCB_TIME_CURRENT_TIME);
    xcb_timestamp_t userTime() const override;
    bool hasUserTimeSupport() const;

    /// Does 'delete c;'
    static void deleteClient(X11Client *c);

    // TODO: make private?
    static bool belongToSameApplication(const X11Client *c1, const X11Client *c2,
                                        win::same_client_check checks = win::flags<win::same_client_check>());
    static bool sameAppWindowRoleMatch(const X11Client *c1, const X11Client *c2, bool active_hack);

    void killWindow() override;
    void toggleShade();
    void showContextHelp() override;
    void cancelShadeHoverTimer();
    void checkActiveModal();
    StrutRect strutRect(StrutArea area) const;
    StrutRects strutRects() const;
    bool hasStrut() const override;

    /**
     * If shown is true the client is mapped and raised, if false
     * the client is unmapped and hidden, this function is called
     * when the tabbing group of the client switches its visible
     * client.
     */
    void setClientShown(bool shown) override;

    /**
     * Whether or not the window has a strut that expands through the invisible area of
     * an xinerama setup where the monitors are not the same resolution.
     */
    bool hasOffscreenXineramaStrut() const;

    bool wantsShadowToBeRendered() const override;

    void layoutDecorationRects(QRect &left, QRect &top, QRect &right, QRect &bottom) const override;

    Xcb::Property fetchFirstInTabBox() const;
    void readFirstInTabBox(Xcb::Property &property);
    void updateFirstInTabBox();
    Xcb::StringProperty fetchColorScheme() const;
    void readColorScheme(Xcb::StringProperty &property);
    void updateColorScheme() override;

    //sets whether the client should be faked as being on all activities (and be shown during session save)
    void setSessionActivityOverride(bool needed);
    bool isClient() const override;

    template <typename T>
    void print(T &stream) const;

    void cancelFocusOutTimer();

    /**
     * Restores the Client after it had been hidden due to show on screen edge functionality.
     * In addition the property gets deleted so that the Client knows that it is visible again.
     */
    void showOnScreenEdge() override;

    Xcb::StringProperty fetchApplicationMenuServiceName() const;
    void readApplicationMenuServiceName(Xcb::StringProperty &property);
    void checkApplicationMenuServiceName();

    Xcb::StringProperty fetchApplicationMenuObjectPath() const;
    void readApplicationMenuObjectPath(Xcb::StringProperty &property);
    void checkApplicationMenuObjectPath();

    struct SyncRequest {
        xcb_sync_counter_t counter{XCB_NONE};
        xcb_sync_int64_t value;
        xcb_sync_alarm_t alarm{XCB_NONE};
        xcb_timestamp_t lastTimestamp;
        QTimer* timeout{nullptr};
        QTimer* failsafeTimeout{nullptr};
        bool isPending{false};
    };

    const SyncRequest &syncRequest() const {
        return m_syncRequest;
    }

    virtual bool wantsSyncCounter() const;
    void handleSync();

    void setGeometryRestore(const QRect &geo) override;
    void changeMaximize(bool horizontal, bool vertical, bool adjust) override;
    bool doStartMoveResize() override;
    void leaveMoveResize() override;
    bool isWaitingForMoveResizeSync() const override;
    void doResizeSync() override;
    void doPerformMoveResize() override;
    void positionGeometryTip() override;

    static void cleanupX11();
    bool belongsToSameApplication(Toplevel const* other, win::same_client_check checks) const override;
    bool belongsToDesktop() const override;

    void doSetActive() override;
    void doSetKeepAbove() override;
    void doSetKeepBelow() override;
    void doMinimize() override;

    QSize resizeIncrements() const override;
    void setShortcutInternal() override;

    void doSetDesktop(int desktop, int was_desk) override;

public Q_SLOTS:
    void closeWindow() override;
    void updateCaption() override;

private Q_SLOTS:
    void shadeHover();
    void shadeUnhover();

private:
    // Handlers for X11 events
    bool mapRequestEvent(xcb_map_request_event_t *e);
    void unmapNotifyEvent(xcb_unmap_notify_event_t *e);
    void destroyNotifyEvent(xcb_destroy_notify_event_t *e);
    void configureRequestEvent(xcb_configure_request_event_t *e);
    void propertyNotifyEvent(xcb_property_notify_event_t *e) override;
    void clientMessageEvent(xcb_client_message_event_t *e) override;
    void enterNotifyEvent(xcb_enter_notify_event_t *e);
    void leaveNotifyEvent(xcb_leave_notify_event_t *e);
    void focusInEvent(xcb_focus_in_event_t *e);
    void focusOutEvent(xcb_focus_out_event_t *e);
    void damageNotifyEvent() override;

    bool buttonPressEvent(xcb_window_t w, int button, int state, int x, int y, int x_root, int y_root, xcb_timestamp_t time = XCB_CURRENT_TIME);
    bool buttonReleaseEvent(xcb_window_t w, int button, int state, int x, int y, int x_root, int y_root);
    bool motionNotifyEvent(xcb_window_t w, int state, int x, int y, int x_root, int y_root);

protected:
    void debug(QDebug& stream) const override;
    void addDamage(const QRegion &damage) override;
    bool acceptsFocus() const override;

    //Signals for the scripting interface
    //Signals make an excellent way for communication
    //in between objects as compared to simple function
    //calls
Q_SIGNALS:
    void clientManaging(KWin::X11Client *);
    void clientFullScreenSet(KWin::X11Client *, bool, bool);

    /**
     * Emitted whenever the Client want to show it menu
     */
    void showRequest();
    /**
     * Emitted whenever the Client's menu is closed
     */
    void menuHidden();
    /**
     * Emitted whenever the Client's menu is available
     */
    void appMenuAvailable();
    /**
     * Emitted whenever the Client's menu is unavailable
     */
    void appMenuUnavailable();

private:
    void exportMappingState(int s);   // ICCCM 4.1.3.1, 4.1.4, NETWM 2.5.1
    bool isManaged() const; ///< Returns false if this client is not yet managed
    void updateAllowedActions(bool force = false);
    QRect fullscreenMonitorsArea(NETFullscreenMonitors topology) const;
    void getWmNormalHints();
    void getMotifHints();
    void getIcons();
    void fetchName();
    void fetchIconicName();
    QString readName() const;
    void setCaption(const QString& s, bool force = false);

    void configureRequest(int value_mask, int rx, int ry, int rw, int rh, int gravity, bool from_tool);
    NETExtendedStrut strut() const;
    int checkShadeGeometry(int w, int h);
    void getSyncCounter();
    void sendSyncRequest();
    void establishCommandWindowGrab(uint8_t button);
    void establishCommandAllGrab(uint8_t button);
    void resizeDecoration();
    void createDecoration(const QRect &oldgeom);

    void pingWindow();
    void killProcess(bool ask, xcb_timestamp_t timestamp = XCB_TIME_CURRENT_TIME);
    void updateUrgency();
    static void sendClientMessage(xcb_window_t w, xcb_atom_t a, xcb_atom_t protocol,
                                  uint32_t data1 = 0, uint32_t data2 = 0, uint32_t data3 = 0);

    void embedClient(xcb_window_t w, xcb_visualid_t visualid, xcb_colormap_t colormap, uint8_t depth);
    void detectNoBorder();
    void updateFrameExtents();
    void setClientFrameExtents(const NETStrut &strut);

    void internalShow();
    void internalHide();
    void internalKeep();
    void map();
    void unmap();
    void updateHiddenPreview();

    void updateInputShape();
    void updateServerGeometry();
    void updateWindowPixmap();

    xcb_timestamp_t readUserTimeMapTimestamp(const KStartupInfoId* asn_id, const KStartupInfoData* asn_data,
                                  bool session) const;
    xcb_timestamp_t readUserCreationTime() const;
    void startupIdChanged();

    void updateInputWindow();

    Xcb::Property fetchShowOnScreenEdge() const;
    void readShowOnScreenEdge(Xcb::Property &property);
    /**
     * Reads the property and creates/destroys the screen edge if required
     * and shows/hides the client.
     */
    void updateShowOnScreenEdge();

    std::unique_ptr<x11_control> m_control;
    Xcb::Window m_client;
    Xcb::Window m_wrapper;
    Xcb::Window m_frame;
    QStringList activityList;
    int m_activityUpdatesBlocked{false};
    bool m_blockedActivityUpdatesRequireTransients{false};
    Xcb::Window m_moveResizeGrabWindow;
    bool move_resize_has_keyboard_grab{false};
    bool m_managed{false};

    Xcb::GeometryHints m_geometryHints;
    void sendSyntheticConfigureNotify();

    enum MappingState {
        Withdrawn, ///< Not handled, as per ICCCM WithdrawnState
        Mapped, ///< The frame is mapped
        Unmapped, ///< The frame is not mapped
        Kept ///< The frame should be unmapped, but is kept (For compositing)
    };
    MappingState mapping_state{Withdrawn};

    Xcb::TransientFor fetchTransient() const;
    void readTransientProperty(Xcb::TransientFor &transientFor);
    xcb_window_t verifyTransientFor(xcb_window_t transient_for, bool set);

    void cleanGrouping();
    void checkGroup(Group* group);
    void set_transient_lead(xcb_window_t lead_id);

    void update_group(bool add);

    xcb_window_t m_transientForId{XCB_WINDOW_NONE};
    xcb_window_t m_originalTransientForId{XCB_WINDOW_NONE};
    win::shade shade_mode{win::shade::none};
    X11Client* shade_below{nullptr};
    uint deleting{0}; ///< True when doing cleanup and destroying the client
    Xcb::MotifHints m_motif;
    uint hidden{0}; ///< Forcibly hidden by calling hide()
    uint noborder{0};
    uint app_noborder{0}; ///< App requested no border via window type, shape extension, etc.
    uint ignore_focus_stealing{0}; ///< Don't apply focus stealing prevention to this client
    bool blocks_compositing{false};

    win::maximize_mode max_mode{win::maximize_mode::restore};
    QRect m_bufferGeometry = QRect(0, 0, 100, 100);
    QRect m_clientGeometry = QRect(0, 0, 100, 100);
    QRect geom_restore;
    QRect geom_fs_restore;
    QTimer* shadeHoverTimer{nullptr};
    xcb_colormap_t m_colormap{XCB_COLORMAP_NONE};
    QString cap_normal, cap_iconic, cap_suffix;
    Group* in_group{nullptr};
    QTimer* ping_timer{nullptr};
    qint64 m_killHelperPID{0};
    xcb_timestamp_t m_pingTimestamp{XCB_TIME_CURRENT_TIME};
    xcb_timestamp_t m_userTime{XCB_TIME_CURRENT_TIME};
    NET::Actions allowed_actions;
    bool shade_geometry_change{false};
    SyncRequest m_syncRequest;
    int sm_stacking_order{-1};
    friend struct ResetupRulesProcedure;

    friend bool performTransiencyCheck();

    Xcb::StringProperty fetchActivities() const;
    void readActivities(Xcb::StringProperty &property);
    void checkActivities();

    // Whether the X property was actually set.
    bool activitiesDefined{false};

    bool sessionActivityOverride{false};
    bool needsXWindowMove{false};

    Xcb::Window m_decoInputExtent;
    QPoint input_offset;

    QTimer* m_focusOutTimer{nullptr};

    QList<QMetaObject::Connection> m_connections;

    QMetaObject::Connection m_edgeRemoveConnection;
    QMetaObject::Connection m_edgeGeometryTrackingConnection;

    friend class x11_control;
    friend class x11_transient;
};

inline xcb_window_t X11Client::wrapperId() const
{
    return m_wrapper;
}

inline bool X11Client::groupTransient() const
{
    return m_transientForId == rootWindow();
}

// Needed because verifyTransientFor() may set transient_for_id to root window,
// if the original value has a problem (window doesn't exist, etc.)
inline bool X11Client::wasOriginallyGroupTransient() const
{
    return m_originalTransientForId == rootWindow();
}

inline const Group* X11Client::group() const
{
    return in_group;
}

inline Group* X11Client::group()
{
    return in_group;
}

inline bool X11Client::isHiddenInternal() const
{
    return hidden;
}

inline win::shade X11Client::shadeMode() const
{
    return shade_mode;
}

inline QRect X11Client::geometryRestore() const
{
    return geom_restore;
}

inline void X11Client::setGeometryRestore(const QRect &geo)
{
    geom_restore = geo;
}

inline win::maximize_mode X11Client::maximizeMode() const
{
    return max_mode;
}

inline bool X11Client::hasNETSupport() const
{
    return info->hasNETSupport();
}

inline xcb_colormap_t X11Client::colormap() const
{
    return m_colormap;
}

inline int X11Client::sessionStackingOrder() const
{
    return sm_stacking_order;
}

inline bool X11Client::isManaged() const
{
    return m_managed;
}

inline QSize X11Client::clientSize() const
{
    return m_clientGeometry.size();
}

inline void X11Client::plainResize(const QSize& s, win::force_geometry force)
{
    plainResize(s.width(), s.height(), force);
}

inline void X11Client::resizeWithChecks(const QSize &size, win::force_geometry force)
{
    resizeWithChecks(size.width(), size.height(), XCB_GRAVITY_BIT_FORGET, force);
}

inline void X11Client::resizeWithChecks(const QSize& s, xcb_gravity_t gravity, win::force_geometry force)
{
    resizeWithChecks(s.width(), s.height(), gravity, force);
}

inline bool X11Client::hasUserTimeSupport() const
{
    return info->userTime() != -1U;
}

inline xcb_window_t X11Client::moveResizeGrabWindow() const
{
    return m_moveResizeGrabWindow;
}

inline bool X11Client::hiddenPreview() const
{
    return mapping_state == Kept;
}

template <typename T>
inline void X11Client::print(T &stream) const
{
    stream << "\'Client:" << window() << ";WMCLASS:" << resourceClass() << ":"
           << resourceName() << ";Caption:" << win::caption(this) << "\'";
}

}

Q_DECLARE_METATYPE(KWin::X11Client *)
Q_DECLARE_METATYPE(QList<KWin::X11Client *>)
