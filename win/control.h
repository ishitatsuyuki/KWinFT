/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"
#include "structs.h"
#include "types.h"

#include <kwin_export.h>

#include "rules/window_rules.h"

#include <QIcon>
#include <QKeySequence>
#include <QRect>

#include <memory>

namespace Wrapland
{
namespace Server
{
class PlasmaWindow;
}
}

class QTimer;

namespace KWin
{
class Toplevel;

namespace TabBox
{
class TabBoxClientImpl;
}

namespace Decoration
{
class DecoratedClientImpl;
class DecorationPalette;
}

namespace win
{

class KWIN_EXPORT control
{
    using DecoPalette = Decoration::DecorationPalette;

    bool m_skip_taskbar{false};
    bool m_original_skip_taskbar{false};
    bool m_skip_pager{false};
    bool m_skip_switcher{false};

    std::shared_ptr<TabBox::TabBoxClientImpl> m_tabbox;
    bool m_first_in_tabbox{false};

    QByteArray m_desktop_file_name;
    QIcon m_icon;

    struct {
        bool active{false};
        QString service_name;
        QString object_path;

    } m_application_menu;

    QKeySequence m_shortcut;

    bool m_active{false};
    bool m_keep_above{false};
    bool m_keep_below{false};
    bool m_demands_attention{false};
    bool m_unresponsive{false};

    QTimer* m_auto_raise_timer{nullptr};
    bool m_minimized{false};

    Wrapland::Server::PlasmaWindow* m_wayland_management{nullptr};

    bool m_have_resize_effect{false};

    quicktiles m_quicktiling{quicktiles::none};
    quicktiles m_electric{quicktiles::none};
    bool m_electric_maximizing{false};
    QTimer* m_electric_maximizing_delay{nullptr};

    bool m_fullscreen{false};

    win::move_resize_op m_move_resize;

    win::deco m_deco;
    win::palette m_palette;

    WindowRules m_rules;

    Toplevel* m_win;

    void minimize(bool avoid_animation);
    void unminimize(bool avoid_animation);

public:
    explicit control(Toplevel* win);
    virtual ~control();

    void setup_tabbox();

    bool skip_pager() const;
    virtual void set_skip_pager(bool set);

    bool skip_switcher() const;
    virtual void set_skip_switcher(bool set);

    bool skip_taskbar() const;
    virtual void set_skip_taskbar(bool set);

    bool original_skip_taskbar() const;
    void set_original_skip_taskbar(bool set);

    std::weak_ptr<TabBox::TabBoxClientImpl> tabbox() const;

    bool first_in_tabbox() const;
    void set_first_in_tabbox(bool is_first);

    QByteArray desktop_file_name() const;
    void set_desktop_file_name(QByteArray const& name);

    QIcon const& icon() const;
    void set_icon(QIcon const& icon);

    bool has_application_menu() const;

    bool application_menu_active() const;
    void set_application_menu_active(bool active);

    QString application_menu_service_name() const;
    QString application_menu_object_path() const;

    void update_application_menu_service_name(QString const& name);
    void update_application_menu_object_path(QString const& path);

    QKeySequence const& shortcut() const;
    void set_shortcut(QString const& shortcut);

    bool active() const;
    void set_active(bool active);

    bool keep_above() const;
    void set_keep_above(bool keep);

    bool keep_below() const;
    void set_keep_below(bool keep);

    void set_demands_attention(bool set);
    bool demands_attention() const;

    bool unresponsive() const;
    void set_unresponsive(bool unresponsive);

    void start_auto_raise();
    void cancel_auto_raise();

    bool minimized() const;
    void set_minimized(bool minimize);

    virtual void update_mouse_grab();

    Wrapland::Server::PlasmaWindow* wayland_management() const;
    void set_wayland_management(Wrapland::Server::PlasmaWindow* plasma_window);
    void destroy_wayland_management();

    bool have_resize_effect() const;
    void update_have_resize_effect();
    void reset_have_resize_effect();

    virtual QSize adjusted_frame_size(QSize const& frame_size, size_mode mode);

    quicktiles electric() const;
    void set_electric(quicktiles tiles);
    bool electric_maximizing() const;
    void set_electric_maximizing(bool maximizing);

    QTimer* electric_maximizing_timer() const;
    void set_electric_maximizing_timer(QTimer* timer);

    quicktiles quicktiling() const;
    void set_quicktiling(quicktiles tiles);

    virtual bool can_fullscreen() const;
    bool fullscreen() const;
    void set_fullscreen(bool fullscreen);

    win::move_resize_op& move_resize();

    win::deco& deco();
    virtual void destroy_decoration();

    win::palette& palette();
    void setup_color_scheme();

    WindowRules& rules();
    WindowRules const& rules() const;
    void set_rules(WindowRules const& rules);

    void remove_rule(Rules* r);
    void discard_temporary_rules();
};

}
}
