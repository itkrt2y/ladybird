/*
 * Copyright (c) 2026, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <AK/kmalloc.h>
#include <LibWebView/Forward.h>

#include <QPointer>
#include <QRect>
#include <QToolBar>

class QAction;
class QMenu;
class QTimer;
class QToolButton;

namespace Ladybird {

class BookmarkDragImage;
class BookmarkDropIndicator;
class Tab;

class BookmarksBar final : public QToolBar {
    Q_OBJECT

public:
    AK_ALLOC_WITH_KMALLOC;

    explicit BookmarksBar(Tab* parent);
    virtual ~BookmarksBar() override;

    void rebuild();

    String const& selected_bookmark_menu_item_id() const { return m_selected_bookmark_menu_item_id; }
    Optional<String> const& selected_bookmark_menu_target_folder_id() const { return m_selected_bookmark_menu_target_folder_id; }
    Optional<String> const& selected_bookmark_menu_parent_folder_id() const { return m_selected_bookmark_menu_parent_folder_id; }

    void show_context_menu(QPoint, Optional<WebView::BookmarkItem const&>, Optional<String const&> target_folder_id, Optional<String const&> parent_folder_id);

private:
    virtual bool event(QEvent*) override;
    virtual bool eventFilter(QObject* object, QEvent* event) override;

    struct DropLocation {
        Optional<String> target_folder_id;
        size_t index { 0 };

        // The bar button or menu action of the folder being dropped onto, which is opened if the drag lingers over it.
        QPointer<QObject> folder_target;

        QPointer<QWidget> indicator_parent;
        QRect indicator_rect;
    };
    struct DropCandidate {
        String id;
        QRect rect;
        QObject* folder_target { nullptr };
    };
    Optional<DropLocation> drop_location_in(QWidget& container, Qt::Orientation, Optional<String> const& folder_id, ReadonlySpan<DropCandidate>, QPoint position) const;
    Optional<DropLocation> bar_drop_location_at(QPoint position);
    Optional<DropLocation> menu_drop_location_at(QMenu&, QPoint position);
    QMenu* menu_at(QPoint global_position) const;

    bool handle_drag_event(QEvent*);
    void begin_bookmark_drag();
    void end_bookmark_drag();
    void close_drag_menus();
    void cancel_bookmark_drag();
    void update_drop_location(QPoint global_position);
    void set_drop_location(Optional<DropLocation>);
    void open_spring_loaded_folder();

    bool handle_left_mouse_click(QMouseEvent*, QObject*);
    bool handle_middle_mouse_click(QMouseEvent*, QObject*);
    bool handle_right_mouse_click(QMouseEvent*, QObject*);
    void extract_item_properties(QObject*);
    void update_chrome_style();

    QMenu& bookmarks_bar_context_menu();
    QMenu& bookmark_context_menu();
    QMenu& bookmark_folder_context_menu();

    Tab* m_tab { nullptr };

    QMenu* m_bookmarks_bar_context_menu { nullptr };
    QMenu* m_bookmark_context_menu { nullptr };
    QMenu* m_bookmark_folder_context_menu { nullptr };

    String m_selected_bookmark_menu_item_id;
    QString m_selected_bookmark_menu_item_type;
    Optional<String> m_selected_bookmark_menu_target_folder_id;
    Optional<String> m_selected_bookmark_menu_parent_folder_id;
    bool m_is_updating_chrome_style { false };

    QPointer<QObject> m_drag_candidate;
    String m_dragged_item_id;
    QPointer<BookmarkDragImage> m_drag_image;
    QPoint m_drag_start_global_position;
    bool m_is_dragging { false };
    bool m_is_ignoring_mouse_until_release { false };

    Optional<DropLocation> m_drop_location;
    QPointer<BookmarkDropIndicator> m_drop_indicator;
    QTimer* m_spring_load_timer { nullptr };
    Vector<QPointer<QWidget>> m_spring_load_bridges;
};

}
