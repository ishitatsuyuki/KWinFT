/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2010 Rohan Prabhu <rohan@rohanprabhu.com>
Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

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

#ifndef KWIN_SCRIPTING_WORKSPACE_WRAPPER_H
#define KWIN_SCRIPTING_WORKSPACE_WRAPPER_H

#include <QObject>
#include <QSize>
#include <QStringList>
#include <QRect>
#include <QQmlListProperty>
#include <kwinglobals.h>

#include <memory>
#include <vector>

namespace KWin
{
class Toplevel;
class WindowWrapper;

class WorkspaceWrapper : public QObject
{
    Q_OBJECT
    Q_ENUMS(ClientAreaOption)
    Q_ENUMS(ElectricBorder)
    Q_PROPERTY(int currentDesktop READ currentDesktop WRITE setCurrentDesktop NOTIFY currentDesktopChanged)
    Q_PROPERTY(KWin::WindowWrapper* activeClient READ activeClient WRITE setActiveClient NOTIFY clientActivated)
    // TODO: write and notify?
    Q_PROPERTY(QSize desktopGridSize READ desktopGridSize NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int desktopGridWidth READ desktopGridWidth NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int desktopGridHeight READ desktopGridHeight NOTIFY desktopLayoutChanged)
    Q_PROPERTY(int workspaceWidth READ workspaceWidth)
    Q_PROPERTY(int workspaceHeight READ workspaceHeight)
    Q_PROPERTY(QSize workspaceSize READ workspaceSize)
    /**
     * The number of desktops currently used. Minimum number of desktops is 1, maximum 20.
     */
    Q_PROPERTY(int desktops READ numberOfDesktops WRITE setNumberOfDesktops NOTIFY numberDesktopsChanged)
    /**
     * The same of the display, that is all screens.
     * @deprecated since 5.0 use virtualScreenSize
     */
    Q_PROPERTY(QSize displaySize READ displaySize)
    /**
     * The width of the display, that is width of all combined screens.
     * @deprecated since 5.0 use virtualScreenSize
     */
    Q_PROPERTY(int displayWidth READ displayWidth)
    /**
     * The height of the display, that is height of all combined screens.
     * @deprecated since 5.0 use virtualScreenSize
     */
    Q_PROPERTY(int displayHeight READ displayHeight)
    Q_PROPERTY(int activeScreen READ activeScreen)
    Q_PROPERTY(int numScreens READ numScreens NOTIFY numberScreensChanged)
    Q_PROPERTY(QString currentActivity READ currentActivity WRITE setCurrentActivity NOTIFY currentActivityChanged)
    Q_PROPERTY(QStringList activities READ activityList NOTIFY activitiesChanged)
    /**
     * The bounding size of all screens combined. Overlapping areas
     * are not counted multiple times.
     * @see virtualScreenGeometry
     */
    Q_PROPERTY(QSize virtualScreenSize READ virtualScreenSize NOTIFY virtualScreenSizeChanged)
    /**
     * The bounding geometry of all outputs combined. Always starts at (0,0) and has
     * virtualScreenSize as it's size.
     * @see virtualScreenSize
     */
    Q_PROPERTY(QRect virtualScreenGeometry READ virtualScreenGeometry NOTIFY virtualScreenGeometryChanged)

private:
    Q_DISABLE_COPY(WorkspaceWrapper)

Q_SIGNALS:
    void desktopPresenceChanged(KWin::WindowWrapper *client, int desktop);
    void currentDesktopChanged(int desktop, KWin::WindowWrapper *client);
    void clientAdded(KWin::WindowWrapper *client);
    void clientRemoved(KWin::WindowWrapper *client);
    void clientManaging(KWin::WindowWrapper *client);
    void clientMinimized(KWin::WindowWrapper *client);
    void clientUnminimized(KWin::WindowWrapper *client);
    void clientRestored(KWin::WindowWrapper *client);
    void clientMaximizeSet(KWin::WindowWrapper *client, bool h, bool v);
    void killWindowCalled(KWin::WindowWrapper *client);
    void clientActivated(KWin::WindowWrapper *client);
    void clientFullScreenSet(KWin::WindowWrapper *client, bool fullScreen, bool user);
    void clientSetKeepAbove(KWin::WindowWrapper *client, bool keepAbove);
    /**
     * Signal emitted whenever the number of desktops changed.
     * To get the current number of desktops use the property desktops.
     * @param oldNumberOfDesktops The previous number of desktops.
     */
    void numberDesktopsChanged(uint oldNumberOfDesktops);
    /**
     * Signal emitted whenever the layout of virtual desktops changed.
     * That is desktopGrid(Size/Width/Height) will have new values.
     * @since 4.11
     */
    void desktopLayoutChanged();
    /**
     * The demands attention state for Client @p c changed to @p set.
     * @param c The Client for which demands attention changed
     * @param set New value of demands attention
     */
    void clientDemandsAttentionChanged(KWin::WindowWrapper* window, bool set);
    /**
     * Signal emitted when the number of screens changes.
     * @param count The new number of screens
     */
    void numberScreensChanged(int count);
    /**
     * This signal is emitted when the size of @p screen changes.
     * Don't forget to fetch an updated client area.
     *
     * @deprecated Use QScreen::geometryChanged signal instead.
     */
    void screenResized(int screen);
    /**
     * Signal emitted whenever the current activity changed.
     * @param id id of the new activity
     */
    void currentActivityChanged(const QString &id);
    /**
     * Signal emitted whenever the list of activities changed.
     * @param id id of the new activity
     */
    void activitiesChanged(const QString &id);
    /**
     * This signal is emitted when a new activity is added
     * @param id id of the new activity
     */
    void activityAdded(const QString &id);
    /**
     * This signal is emitted when the activity
     * is removed
     * @param id id of the removed activity
     */
    void activityRemoved(const QString &id);
    /**
     * Emitted whenever the virtualScreenSize changes.
     * @see virtualScreenSize()
     * @since 5.0
     */
    void virtualScreenSizeChanged();
    /**
     * Emitted whenever the virtualScreenGeometry changes.
     * @see virtualScreenGeometry()
     * @since 5.0
     */
    void virtualScreenGeometryChanged();

public:
//------------------------------------------------------------------
//enums copy&pasted from kwinglobals.h because qtscript is evil

    enum ClientAreaOption {
        ///< geometry where a window will be initially placed after being mapped
        PlacementArea,
        ///< window movement snapping area?  ignore struts
        MovementArea,
        ///< geometry to which a window will be maximized
        MaximizeArea,
        ///< like MaximizeArea, but ignore struts - used e.g. for topmenu
        MaximizeFullArea,
        ///< area for fullscreen windows
        FullScreenArea,
        ///< whole workarea (all screens together)
        WorkArea,
        ///< whole area (all screens together), ignore struts
        FullArea,
        ///< one whole screen, ignore struts
        ScreenArea
    };
    enum ElectricBorder {
        ElectricTop,
        ElectricTopRight,
        ElectricRight,
        ElectricBottomRight,
        ElectricBottom,
        ElectricBottomLeft,
        ElectricLeft,
        ElectricTopLeft,
        ELECTRIC_COUNT,
        ElectricNone
    };

protected:
    explicit WorkspaceWrapper(QObject* parent = nullptr);

public:
#define GETTERSETTERDEF( rettype, getter, setter ) \
rettype getter() const; \
void setter( rettype val );
    GETTERSETTERDEF(int, numberOfDesktops, setNumberOfDesktops)
    GETTERSETTERDEF(int, currentDesktop, setCurrentDesktop)
    GETTERSETTERDEF(QString, currentActivity, setCurrentActivity)
    GETTERSETTERDEF(KWin::WindowWrapper*, activeClient, setActiveClient)
#undef GETTERSETTERDEF
    QSize desktopGridSize() const;
    int desktopGridWidth() const;
    int desktopGridHeight() const;
    int workspaceWidth() const;
    int workspaceHeight() const;
    QSize workspaceSize() const;
    int displayWidth() const;
    int displayHeight() const;
    QSize displaySize() const;
    int activeScreen() const;
    int numScreens() const;
    QStringList activityList() const;
    QSize virtualScreenSize() const;
    QRect virtualScreenGeometry() const;

    /**
     * Returns the geometry a Client can use with the specified option.
     * This method should be preferred over other methods providing screen sizes as the
     * various options take constraints such as struts set on panels into account.
     * This method is also multi screen aware, but there are also options to get full areas.
     * @param option The type of area which should be considered
     * @param screen The screen for which the area should be considered
     * @param desktop The desktop for which the area should be considered, in general there should not be a difference
     * @returns The specified screen geometry
     */
    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option, int screen, int desktop) const;
    /**
     * Overloaded method for convenience.
     * @param option The type of area which should be considered
     * @param point The coordinates which have to be included in the area
     * @param desktop The desktop for which the area should be considered, in general there should not be a difference
     * @returns The specified screen geometry
     */
    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option, const QPoint& point, int desktop) const;
    /**
     * Overloaded method for convenience.
     * @param client The Client for which the area should be retrieved
     * @returns The specified screen geometry
     */
    Q_SCRIPTABLE QRect clientArea(ClientAreaOption option, KWin::WindowWrapper const* client) const;
    /**
     * Returns the name for the given @p desktop.
     */
    Q_SCRIPTABLE QString desktopName(int desktop) const;
    /**
     * Create a new virtual desktop at the requested position.
     * @param position The position of the desktop. It should be in range [0, count].
     * @param name The name for the new desktop, if empty the default name will be used.
     */
    Q_SCRIPTABLE void createDesktop(int position, const QString &name) const;
    /**
     * Remove the virtual desktop at the requested position
     * @param position The position of the desktop to be removed. It should be in range [0, count - 1].
     */
    Q_SCRIPTABLE void removeDesktop(int position) const;
    /**
     * Provides support information about the currently running KWin instance.
     */
    Q_SCRIPTABLE QString supportInformation() const;
    /**
     * Finds the Client with the given @p windowId.
     * @param windowId The window Id of the Client
     * @return The found Client or @c null
     */
    Q_SCRIPTABLE KWin::WindowWrapper* getClient(qulonglong windowId);

public Q_SLOTS:
    // all the available key bindings
    void slotSwitchDesktopNext();
    void slotSwitchDesktopPrevious();
    void slotSwitchDesktopRight();
    void slotSwitchDesktopLeft();
    void slotSwitchDesktopUp();
    void slotSwitchDesktopDown();

    void slotSwitchToNextScreen();
    void slotWindowToNextScreen();
    void slotToggleShowDesktop();

    void slotWindowMaximize();
    void slotWindowMaximizeVertical();
    void slotWindowMaximizeHorizontal();
    void slotWindowMinimize();
    void slotWindowShade() {} // Deprecated
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
    void slotWindowQuickTileLeft();
    void slotWindowQuickTileRight();
    void slotWindowQuickTileTop();
    void slotWindowQuickTileBottom();
    void slotWindowQuickTileTopLeft();
    void slotWindowQuickTileTopRight();
    void slotWindowQuickTileBottomLeft();
    void slotWindowQuickTileBottomRight();

    void slotSwitchWindowUp();
    void slotSwitchWindowDown();
    void slotSwitchWindowRight();
    void slotSwitchWindowLeft();

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

    /**
     * Sends the WindowWrapper to the given @p screen.
     */
    void sendClientToScreen(KWin::WindowWrapper *client, int screen);

    /**
     * Shows an outline at the specified @p geometry.
     * If an outline is already shown the outline is moved to the new position.
     * Use hideOutline to remove the outline again.
     */
    void showOutline(const QRect &geometry);
    /**
     * Overloaded method for convenience.
     */
    void showOutline(int x, int y, int width, int height);
    /**
     * Hides the outline previously shown by showOutline.
     */
    void hideOutline();

    WindowWrapper* get_window(Toplevel* client) const;

protected:
    // TODO: make this private. Remove dynamic inheritance?
    std::vector<std::unique_ptr<WindowWrapper>> m_windows;

private Q_SLOTS:
    void setupAbstractClientConnections(WindowWrapper* window);
    void setupClientConnections(WindowWrapper* window);

private:
    void handle_client_added(Toplevel* client);
    void handle_client_removed(Toplevel* client);
};

class QtScriptWorkspaceWrapper : public WorkspaceWrapper
{
    Q_OBJECT
public:
    /**
     * List of Clients currently managed by KWin.
     */
    Q_INVOKABLE QList<KWin::WindowWrapper*> clientList() const;

    explicit QtScriptWorkspaceWrapper(QObject* parent = nullptr);
};

class DeclarativeScriptWorkspaceWrapper : public WorkspaceWrapper
{
    Q_OBJECT

    Q_PROPERTY(QQmlListProperty<KWin::WindowWrapper> clients READ clients)
public:
    QQmlListProperty<KWin::WindowWrapper> clients();
    static int countClientList(QQmlListProperty<KWin::WindowWrapper> *clients);
    static WindowWrapper *atClientList(QQmlListProperty<KWin::WindowWrapper> *clients, int index);

    explicit DeclarativeScriptWorkspaceWrapper(QObject* parent = nullptr);
};

}

#endif
