/*
 * Copyright (c) 2026, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Enumerate.h>
#include <AK/ScopeGuard.h>
#include <LibWebView/Application.h>
#include <LibWebView/BookmarkStore.h>
#include <UI/Qt/BookmarksBar.h>
#include <UI/Qt/ChromeStyle.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/Menu.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Tab.h>

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QTimer>
#include <QToolButton>

namespace Ladybird {

static constexpr int BOOKMARK_BUTTON_MAX_WIDTH = 150;
static constexpr int BOOKMARK_BUTTON_ICON_SIZE = 16;
static constexpr int BOOKMARK_BUTTON_MIN_HEIGHT = 24;
static constexpr int BOOKMARK_BUTTON_VERTICAL_PADDING = 8;
static constexpr int BOOKMARK_BUTTON_HORIZONTAL_PADDING = 7;
static constexpr int BOOKMARK_BUTTON_ICON_TEXT_SPACING = 6;
static constexpr int BOOKMARK_BUTTON_TEXT_ELISION_PADDING = 2;

static constexpr char const* BOOKMARK_ITEM_PROPERTY = "bookmark_item";
static constexpr char const* BOOKMARK_CONTEXT_MENU_OPEN_PROPERTY = "bookmark_context_menu_open";
static constexpr char const* BOOKMARK_MENU_SHOW_ORDER_PROPERTY = "bookmark_menu_show_order";
static constexpr char const* BOOKMARK_MENU_COLLAPSED_PROPERTY = "bookmark_menu_collapsed";

static constexpr int SPRING_LOADED_FOLDER_DELAY_MS = 400;

static QStyleOptionToolButton bookmark_button_style_option(QToolButton const& button)
{
    QStyleOptionToolButton option;
    option.initFrom(&button);

    option.rect = button.rect();
    option.icon = button.icon();
    option.iconSize = button.iconSize();
    option.text = button.text();
    option.toolButtonStyle = button.toolButtonStyle();
    option.arrowType = button.arrowType();
    option.subControls = QStyle::SC_ToolButton;

    if (button.arrowType() != Qt::NoArrow)
        option.features |= QStyleOptionToolButton::Arrow;

    switch (button.popupMode()) {
    case QToolButton::DelayedPopup:
        option.features |= QStyleOptionToolButton::PopupDelay;
        break;
    case QToolButton::MenuButtonPopup:
        option.features |= QStyleOptionToolButton::MenuButtonPopup;
        option.subControls |= QStyle::SC_ToolButtonMenu;
        break;
    case QToolButton::InstantPopup:
        option.features |= QStyleOptionToolButton::Menu;
        break;
    }

    if (button.autoRaise())
        option.state |= QStyle::State_AutoRaise;
    if (button.menu())
        option.features |= QStyleOptionToolButton::HasMenu;
    if (button.isDown())
        option.state |= QStyle::State_Sunken;
    if (button.property(BOOKMARK_CONTEXT_MENU_OPEN_PROPERTY).toBool())
        option.state |= QStyle::State_MouseOver;

    return option;
}

struct BookmarkButtonLayout {
    QRect content_rect;
    QRect icon_rect;
    QRect text_rect;
    int menu_indicator_width { 0 };
    int icon_width { 0 };
    int icon_text_spacing { 0 };
    int available_text_width { 0 };
    int preferred_width { 0 };
};
static BookmarkButtonLayout bookmark_button_layout(QToolButton const& button, QStyleOptionToolButton const& option, QString const& text, QRect const& button_rect)
{
    BookmarkButtonLayout layout;

    if (button.menu())
        layout.menu_indicator_width = button.style()->pixelMetric(QStyle::PM_MenuButtonIndicator, &option, &button);

    layout.icon_width = button.icon().isNull() ? 0 : button.iconSize().width();
    layout.icon_text_spacing = layout.icon_width > 0 && !text.isEmpty() ? BOOKMARK_BUTTON_ICON_TEXT_SPACING : 0;

    layout.content_rect = button_rect;
    layout.content_rect.adjust(BOOKMARK_BUTTON_HORIZONTAL_PADDING, 0, -BOOKMARK_BUTTON_HORIZONTAL_PADDING, 0);
    layout.content_rect.adjust(0, 0, -layout.menu_indicator_width, 0);

    auto text_left = layout.content_rect.left() + layout.icon_width + layout.icon_text_spacing;
    layout.available_text_width = max(layout.content_rect.right() - text_left + 1, 0);
    layout.text_rect = QRect { text_left, layout.content_rect.top(), layout.available_text_width, layout.content_rect.height() };

    if (layout.icon_width > 0) {
        auto icon_size = option.iconSize;
        layout.icon_rect = QRect {
            layout.content_rect.left(),
            layout.content_rect.top() + ((layout.content_rect.height() - icon_size.height()) / 2),
            icon_size.width(),
            icon_size.height(),
        };
    }

    auto preferred_width = (BOOKMARK_BUTTON_HORIZONTAL_PADDING * 2)
        + layout.icon_width
        + layout.icon_text_spacing
        + button.fontMetrics().horizontalAdvance(text)
        + BOOKMARK_BUTTON_TEXT_ELISION_PADDING
        + layout.menu_indicator_width;
    layout.preferred_width = min(preferred_width, button_rect.width());

    return layout;
}

static void paint_bookmark_button(QToolButton& button)
{
    QStylePainter painter(&button);

    auto option = bookmark_button_style_option(button);
    auto layout = bookmark_button_layout(button, option, option.text, button.rect());

    auto frame_option = option;
    frame_option.icon = {};
    frame_option.text.clear();
    painter.drawComplexControl(QStyle::CC_ToolButton, frame_option);

    if (layout.icon_width > 0) {
        auto mode = button.isEnabled() ? QIcon::Normal : QIcon::Disabled;
        if (button.isEnabled() && (option.state & QStyle::State_MouseOver))
            mode = QIcon::Active;

        option.icon.paint(&painter, layout.icon_rect, Qt::AlignCenter, mode, button.isChecked() ? QIcon::On : QIcon::Off);
    }

    auto elided_text = button.fontMetrics().elidedText(option.text, Qt::ElideRight, layout.available_text_width);

    button.style()->drawItemText(
        &painter,
        layout.text_rect,
        Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
        option.palette,
        button.isEnabled(),
        elided_text,
        QPalette::ButtonText);
}

static void install_menu_event_filter(QObject* filter, QMenu* menu)
{
    // Allows collapsed menus to be painted fully transparent while dragging.
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->installEventFilter(filter);

    for (auto* action : menu->actions()) {
        if (auto* submenu = action->menu())
            install_menu_event_filter(filter, submenu);
    }
}

class BookmarkDropIndicator final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    explicit BookmarkDropIndicator(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void set_outline(bool outline)
    {
        if (m_outline == outline)
            return;

        m_outline = outline;
        update();
    }

private:
    virtual void paintEvent(QPaintEvent*) override
    {
        auto color = ChromeStyle::chrome_accent(palette());
        color.setAlpha(220);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        if (m_outline) {
            painter.setPen(QPen(color, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 5, 5);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRoundedRect(QRectF(rect()), 1.5, 1.5);
        }
    }

    bool m_outline { false };
};

class BookmarkDragImage final : public QWidget {
public:
    AK_ALLOC_WITH_KMALLOC;

    BookmarkDragImage(QWidget& source, QRect source_rect, QPoint hot_spot)
        : m_pixmap(source_rect.size() * source.devicePixelRatioF())
        , m_hot_spot(hot_spot)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        resize(source_rect.size());

        m_pixmap.setDevicePixelRatio(source.devicePixelRatioF());
        m_pixmap.fill(Qt::transparent);

        QPainter painter(&m_pixmap);
        painter.setOpacity(0.75);
        source.render(&painter, QPoint(), QRegion(source_rect), QWidget::DrawChildren);
    }

    void move_to(QWidget& parent, QPoint global_position)
    {
        move(parent.mapFromGlobal(global_position) - m_hot_spot);

        if (parentWidget() != &parent) {
            setParent(&parent);
            show();
            raise();
        }
    }

private:
    virtual void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.drawPixmap(0, 0, m_pixmap);
    }

    QPixmap m_pixmap;
    QPoint m_hot_spot;
};

// Whether the event shows that the left mouse button is no longer held. A new press means that its release happened
// without being delivered to us. Mouse moves cannot tell us this, as moves that report no buttons held may arrive while
// the button is still held.
static bool ends_mouse_hold(QEvent const& event)
{
    switch (event.type()) {
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        return as<QMouseEvent>(event).button() == Qt::LeftButton;
    default:
        return false;
    }
}

static QToolButton* as_bookmark_button(QObject* object)
{
    if (auto* button = as_if<QToolButton>(object); button && button->property(BOOKMARK_ITEM_PROPERTY).toBool())
        return button;
    return nullptr;
}

// On Wayland, a new menu becomes a child of the topmost open menu, and the compositor dismisses it if it does not touch
// that menu. A transparent popup over the whole window gives the next menu something to attach to.
static QWidget* open_popup_bridge(QWidget const& window)
{
    auto* bridge = new QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint);
    bridge->setAttribute(Qt::WA_TranslucentBackground);
    bridge->setAttribute(Qt::WA_TransparentForMouseEvents);
    bridge->setGeometry(window.geometry());
    bridge->show();
    return bridge;
}

static QMenu* folder_menu_for_button(QToolButton const& button)
{
    if (auto* action = button.defaultAction())
        return action->menu();
    return nullptr;
}

static QString bookmark_item_id_for_menu_action(QAction const& action)
{
    if (auto* submenu = action.menu())
        return submenu->property("id").toString();
    return action.property("id").toString();
}

static Optional<size_t> index_of_bookmark_item(ReadonlySpan<WebView::BookmarkItem> items, StringView id)
{
    for (auto [i, item] : enumerate(items)) {
        if (item.id == id)
            return i;
    }
    return {};
}

static void find_topmost_visible_menu_at(QMenu& menu, QPoint global_position, QMenu*& topmost_menu)
{
    if (!menu.isVisible())
        return;

    if (menu.geometry().contains(global_position) && !menu.property(BOOKMARK_MENU_COLLAPSED_PROPERTY).toBool()) {
        auto show_order = menu.property(BOOKMARK_MENU_SHOW_ORDER_PROPERTY).toULongLong();
        if (!topmost_menu || show_order > topmost_menu->property(BOOKMARK_MENU_SHOW_ORDER_PROPERTY).toULongLong())
            topmost_menu = &menu;
    }

    for (auto* action : menu.actions()) {
        if (auto* submenu = action->menu())
            find_topmost_visible_menu_at(*submenu, global_position, topmost_menu);
    }
}

// Closing or resizing menus mid-drag may cause some Wayland compositors to dismiss the other open menus, or to refuse
// any further menus until the mouse button is released. So rather than closing menus during a drag, we stop painting
// them until the drag returns to them.
static void set_menu_collapsed(QMenu& menu, bool collapsed)
{
    if (menu.property(BOOKMARK_MENU_COLLAPSED_PROPERTY).toBool() == collapsed)
        return;

    menu.setProperty(BOOKMARK_MENU_COLLAPSED_PROPERTY, collapsed);
    menu.update();
}

static void collapse_menu_tree(QMenu& menu)
{
    if (!menu.isVisible())
        return;

    for (auto* action : menu.actions()) {
        if (auto* submenu = action->menu())
            collapse_menu_tree(*submenu);
    }

    set_menu_collapsed(menu, true);
}

// Folder menus opened mid-drag are popped up directly rather than through their parent menu, so hiding the parent
// does not hide them.
static void hide_menu_tree(QMenu& menu)
{
    for (auto* action : menu.actions()) {
        if (auto* submenu = action->menu())
            hide_menu_tree(*submenu);
    }

    if (menu.isVisible())
        menu.hide();
}

BookmarksBar::BookmarksBar(Tab* parent)
    : QToolBar(parent)
    , m_tab(parent)
{
    setObjectName("LadybirdBookmarksBar");
    setIconSize({ BOOKMARK_BUTTON_ICON_SIZE, BOOKMARK_BUTTON_ICON_SIZE });
    setVisible(WebView::Application::settings().appearance().show_bookmarks_bar);
    setMovable(false);
    setFloatable(false);
    update_chrome_style();

    m_spring_load_timer = new QTimer(this);
    m_spring_load_timer->setSingleShot(true);
    m_spring_load_timer->setInterval(SPRING_LOADED_FOLDER_DELAY_MS);
    connect(m_spring_load_timer, &QTimer::timeout, this, &BookmarksBar::open_spring_loaded_folder);

    installEventFilter(this);

    rebuild();
}

BookmarksBar::~BookmarksBar()
{
    cancel_bookmark_drag();
}

bool BookmarksBar::event(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange)
        update_chrome_style();

    return QToolBar::event(event);
}

void BookmarksBar::update_chrome_style()
{
    if (m_is_updating_chrome_style)
        return;

    m_is_updating_chrome_style = true;
    setStyleSheet(ChromeStyle::bookmarks_bar_style_sheet(palette()));
    m_is_updating_chrome_style = false;
}

void BookmarksBar::rebuild()
{
    cancel_bookmark_drag();
    m_drag_candidate = nullptr;

    for (auto* action : actions()) {
        if (auto* menu = action->menu())
            menu->close();
    }

    clear();

    auto set_button_properties = [&](QToolButton* button, QString const& title) {
        button->setProperty(BOOKMARK_ITEM_PROPERTY, true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->ensurePolished();

        auto option = bookmark_button_style_option(*button);

        auto bookmark_button_height = max(BOOKMARK_BUTTON_MIN_HEIGHT, max(button->fontMetrics().height(), button->iconSize().height()) + BOOKMARK_BUTTON_VERTICAL_PADDING);
        auto max_size_rect = QRect { 0, 0, BOOKMARK_BUTTON_MAX_WIDTH, bookmark_button_height };

        auto layout = bookmark_button_layout(*button, option, title, max_size_rect);

        auto available_title_width = max(layout.available_text_width - BOOKMARK_BUTTON_TEXT_ELISION_PADDING, 0);
        auto text = button->fontMetrics().elidedText(title, Qt::ElideRight, available_title_width);
        button->setText(text);

        layout = bookmark_button_layout(*button, option, text, max_size_rect);

        button->setFixedWidth(layout.preferred_width);
        button->setMaximumWidth(BOOKMARK_BUTTON_MAX_WIDTH);
        button->setFixedHeight(bookmark_button_height);

        button->installEventFilter(this);
    };

    for (auto const& item : WebView::Application::the().bookmarks_menu().items()) {
        item.visit(
            [&](NonnullRefPtr<WebView::Action> const& bookmark) {
                if (bookmark->id() != WebView::ActionID::BookmarkItem)
                    return;

                auto* action = create_application_action(*this, *bookmark);
                addAction(action);

                if (auto* button = as_if<QToolButton>(widgetForAction(action)))
                    set_button_properties(button, qstring_from_ak_string(bookmark->text()));
            },
            [&](NonnullRefPtr<WebView::Menu> const& folder) {
                auto title = qstring_from_ak_string(folder->title());

                auto* submenu = create_application_menu(*this, *folder);
                install_menu_event_filter(this, submenu);

                auto* action = new QAction(title, this);
                action->setIcon(create_chrome_icon(ChromeIcon::Folder, palette()));
                action->setProperty("id", submenu->property("id"));
                action->setProperty("type", submenu->property("type"));
                action->setProperty("target_folder_id", submenu->property("target_folder_id"));
                action->setMenu(submenu);
                addAction(action);

                if (auto* button = as_if<QToolButton>(widgetForAction(action))) {
                    button->setPopupMode(QToolButton::InstantPopup);
                    set_button_properties(button, title);
                }
            },
            [](WebView::Separator) {
            });
    }
}

void BookmarksBar::show_context_menu(QPoint position, Optional<WebView::BookmarkItem const&> item, Optional<String const&> target_folder_id)
{
    if (item.has_value()) {
        m_selected_bookmark_menu_item_id = item->id;
        m_selected_bookmark_menu_target_folder_id = target_folder_id.copy();

        if (item->is_bookmark())
            bookmark_context_menu().exec(position);
        else if (item->is_folder())
            bookmark_folder_context_menu().exec(position);
    } else {
        m_selected_bookmark_menu_item_id = {};
        m_selected_bookmark_menu_target_folder_id = {};

        bookmarks_bar_context_menu().exec(position);
    }
}

bool BookmarksBar::eventFilter(QObject* object, QEvent* event)
{
    if (m_is_dragging && object->isWidgetType() && handle_drag_event(event))
        return true;

    if (m_is_ignoring_mouse_until_release && object->isWidgetType()) {
        if (ends_mouse_hold(*event)) {
            close_drag_menus();
            return event->type() == QEvent::MouseButtonRelease;
        }

        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonRelease:
            return true;
        default:
            break;
        }
    }

    // Menus opened while dragging may overlap one another, so we track the order in which they were shown to hit test
    // against the topmost one.
    if (event->type() == QEvent::Show) {
        if (auto* menu = as_if<QMenu>(object)) {
            static u64 s_menu_show_order = 0;
            menu->setProperty(BOOKMARK_MENU_SHOW_ORDER_PROPERTY, QVariant::fromValue(++s_menu_show_order));
            menu->setProperty(BOOKMARK_MENU_COLLAPSED_PROPERTY, false);
        }
    }

    if (event->type() == QEvent::Paint) {
        if (auto* menu = as_if<QMenu>(object); menu && menu->property(BOOKMARK_MENU_COLLAPSED_PROPERTY).toBool())
            return true;

        if (auto* button = as_bookmark_button(object)) {
            paint_bookmark_button(*button);
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto& mouse_event = as<QMouseEvent>(*event);

        if (mouse_event.button() == Qt::LeftButton)
            return handle_left_mouse_click(&mouse_event, object);
        if (mouse_event.button() == Qt::MiddleButton)
            return handle_middle_mouse_click(&mouse_event, object);
        if (mouse_event.button() == Qt::RightButton)
            return handle_right_mouse_click(&mouse_event, object);
    }

    if (event->type() == QEvent::MouseMove && !m_is_dragging && as_if<QMenu>(object))
        open_hovered_folder_menu(as<QMouseEvent>(*event).globalPosition().toPoint());

    if (event->type() == QEvent::MouseMove && m_drag_candidate == object) {
        auto& mouse_event = as<QMouseEvent>(*event);
        auto global_position = mouse_event.globalPosition().toPoint();

        if (mouse_event.buttons().testFlag(Qt::LeftButton) && (global_position - m_drag_start_global_position).manhattanLength() >= QApplication::startDragDistance()) {
            begin_bookmark_drag();
            update_drop_location(global_position);
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease && m_drag_candidate == object) {
        auto& mouse_event = as<QMouseEvent>(*event);

        if (mouse_event.button() == Qt::LeftButton) {
            auto candidate = m_drag_candidate;
            m_drag_candidate = nullptr;

            // Folder buttons open their menu on release rather than on press, so that pressing on them may begin a drag.
            if (auto* button = as_if<QToolButton>(candidate.data()); button && folder_menu_for_button(*button)) {
                button->setDown(false);
                if (button->rect().contains(mouse_event.position().toPoint()))
                    button->showMenu();
                return true;
            }
        }
    }

    return QToolBar::eventFilter(object, event);
}

// While a folder menu is open, hovering another folder button on the bar switches to that folder's menu, as in a menu bar.
void BookmarksBar::open_hovered_folder_menu(QPoint global_position)
{
    auto position = mapFromGlobal(global_position);
    if (!rect().contains(position) || menu_at(global_position))
        return;

    auto* button = as_bookmark_button(childAt(position));
    if (!button)
        return;

    auto* hovered_menu = folder_menu_for_button(*button);
    if (!hovered_menu || hovered_menu->isVisible())
        return;

    QMenu* open_menu = nullptr;
    for (auto* action : actions()) {
        if (auto* menu = action->menu(); menu && menu->isVisible()) {
            open_menu = menu;
            break;
        }
    }
    if (!open_menu)
        return;

    hide_menu_tree(*open_menu);

    // The open menu runs a nested event loop, which must return before the next menu is shown.
    QTimer::singleShot(0, button, [button]() {
        button->showMenu();
    });
}

bool BookmarksBar::handle_left_mouse_click(QMouseEvent* event, QObject* item)
{
    if (event->modifiers().testFlag(Qt::ControlModifier))
        return handle_middle_mouse_click(event, item);

    m_drag_candidate = nullptr;
    m_drag_start_global_position = event->globalPosition().toPoint();

    if (auto* button = as_bookmark_button(item)) {
        if (auto* action = button->defaultAction()) {
            m_drag_candidate = button;
            m_dragged_item_id = ak_string_from_qstring(action->property("id").toString());
        }

        if (folder_menu_for_button(*button)) {
            button->setDown(true);
            return true;
        }
    } else if (auto* menu = as_if<QMenu>(item)) {
        if (auto* action = menu->actionAt(event->position().toPoint()); action && !action->isSeparator()) {
            if (auto id = bookmark_item_id_for_menu_action(*action); !id.isEmpty()) {
                m_drag_candidate = menu;
                m_dragged_item_id = ak_string_from_qstring(id);
            }
        }
    }

    return false;
}

void BookmarksBar::begin_bookmark_drag()
{
    m_is_dragging = true;

    // While the mouse button is held, the source widget (or the open bookmark menu) keeps the pointer grab. This lets
    // us open folder menus mid-drag, which platform drag-and-drop does not allow on every windowing system.
    qApp->installEventFilter(this);
    QGuiApplication::setOverrideCursor(Qt::DragMoveCursor);

    auto* source = as_if<QWidget>(m_drag_candidate.data());
    if (!source)
        return;

    auto start_position = source->mapFromGlobal(m_drag_start_global_position);
    auto source_rect = source->rect();

    if (auto* button = as_if<QToolButton>(source)) {
        button->setDown(false);
    } else if (auto* menu = as_if<QMenu>(source)) {
        source_rect = menu->actionGeometry(menu->actionAt(start_position));

        // The menu may later close a submenu it opened (such as that of the dragged folder), which would also close
        // any menu opened on top of it mid-drag. So we close them now, while nothing is opened on top of them.
        for (auto* action : menu->actions()) {
            if (auto* submenu = action->menu())
                hide_menu_tree(*submenu);
        }
    }

    m_drag_image = new BookmarkDragImage(*source, source_rect, start_position - source_rect.topLeft());
}

void BookmarksBar::end_bookmark_drag()
{
    m_is_dragging = false;
    m_drag_candidate = nullptr;
    delete m_drag_image.data();

    QGuiApplication::restoreOverrideCursor();

    set_drop_location({});
}

void BookmarksBar::cancel_bookmark_drag()
{
    if (!m_is_dragging && !m_is_ignoring_mouse_until_release)
        return;

    if (m_is_dragging)
        end_bookmark_drag();
    close_drag_menus();
}

void BookmarksBar::close_drag_menus()
{
    for (auto* action : actions()) {
        if (auto* menu = action->menu())
            hide_menu_tree(*menu);
    }

    for (auto& bridge : m_spring_load_bridges)
        delete bridge.data();
    m_spring_load_bridges.clear();

    m_is_ignoring_mouse_until_release = false;
    qApp->removeEventFilter(this);
}

bool BookmarksBar::handle_drag_event(QEvent* event)
{
    if (ends_mouse_hold(*event)) {
        // Only a release drops the item. Otherwise, the release was not delivered to us, and the drag is cancelled.
        auto is_drop = event->type() == QEvent::MouseButtonRelease;
        auto location = is_drop ? m_drop_location : Optional<DropLocation> {};
        auto id = m_dragged_item_id;

        end_bookmark_drag();
        close_drag_menus();

        if (!location.has_value())
            return is_drop;

        // Mutating the bookmark store synchronously rebuilds every bookmarks bar, which would destroy the widget whose
        // mouse event we are currently handling.
        QMetaObject::invokeMethod(
            this, [id = AK::move(id), target_folder_id = AK::move(location->target_folder_id), index = location->index]() {
                WebView::Application::bookmark_store().move_item(id, target_folder_id, index);
            },
            Qt::QueuedConnection);
        return true;
    }

    switch (event->type()) {
    case QEvent::MouseMove:
        update_drop_location(as<QMouseEvent>(*event).globalPosition().toPoint());
        return true;

    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonRelease:
    case QEvent::Wheel:
        return true;

    case QEvent::KeyPress:
        if (as<QKeyEvent>(*event).key() == Qt::Key_Escape) {
            end_bookmark_drag();

            // The mouse button is still held, so keep swallowing mouse events until it is released. Otherwise, the
            // source button would treat the release as a click. The menus stay open (though unpainted) until then, as
            // closing the menu that holds the pointer grab would stop the release from being delivered.
            for (auto* action : actions()) {
                if (auto* menu = action->menu())
                    collapse_menu_tree(*menu);
            }
            m_is_ignoring_mouse_until_release = true;
        }
        return true;

    default:
        return false;
    }
}

QMenu* BookmarksBar::menu_at(QPoint global_position) const
{
    QMenu* topmost_menu = nullptr;

    for (auto* action : actions()) {
        if (auto* menu = action->menu())
            find_topmost_visible_menu_at(*menu, global_position, topmost_menu);
    }

    return topmost_menu;
}

Optional<BookmarksBar::DropLocation> BookmarksBar::drop_location_in(QWidget& container, Qt::Orientation orientation, Optional<String> const& folder_id, ReadonlySpan<DropCandidate> candidates, QPoint position) const
{
    auto& bookmark_store = WebView::Application::bookmark_store();

    if (!bookmark_store.can_move_item(m_dragged_item_id, folder_id))
        return {};

    ReadonlySpan<WebView::BookmarkItem> items = bookmark_store.root_items();
    if (folder_id.has_value()) {
        auto folder = bookmark_store.find_item_by_id(*folder_id);
        if (!folder.has_value() || !folder->is_folder())
            return {};
        items = folder->folder().children;
    }

    auto is_horizontal = orientation == Qt::Horizontal;
    auto offset = [&](QPoint point) { return is_horizontal ? point.x() : point.y(); };
    auto start = [&](QRect const& rect) { return is_horizontal ? rect.left() : rect.top(); };
    auto end = [&](QRect const& rect) { return is_horizontal ? rect.right() : rect.bottom(); };
    auto extent = [&](QRect const& rect) { return is_horizontal ? rect.width() : rect.height(); };

    auto dragged_item_index = index_of_bookmark_item(items, m_dragged_item_id);

    auto insertion_location = [&](size_t index, int edge) -> Optional<DropLocation> {
        // Dropping an item immediately before or after itself would not move it.
        if (dragged_item_index.has_value() && (index == *dragged_item_index || index == *dragged_item_index + 1))
            return {};

        edge = clamp(edge, 2, max(extent(container.rect()) - 3, 2));

        DropLocation location;
        location.target_folder_id = folder_id;
        location.index = index;
        location.indicator_parent = &container;
        location.indicator_rect = is_horizontal
            ? QRect { edge - 1, 5, 3, max(container.height() - 10, 0) }
            : QRect { 6, edge - 1, max(container.width() - 12, 0), 3 };
        return location;
    };

    DropCandidate const* previous_candidate = nullptr;
    size_t previous_index = 0;

    for (auto const& candidate : candidates) {
        auto index = index_of_bookmark_item(items, candidate.id);
        if (!index.has_value())
            continue;

        auto const& rect = candidate.rect;
        auto const& item = items[*index];

        // Dropping onto the middle of a folder moves the item to the end of that folder.
        if (item.is_folder() && bookmark_store.can_move_item(m_dragged_item_id, item.id)) {
            auto quarter = extent(rect) / 4;

            if (offset(position) >= start(rect) + quarter && offset(position) <= end(rect) - quarter) {
                DropLocation location;
                location.target_folder_id = item.id;
                location.index = item.folder().children.size();
                location.folder_target = candidate.folder_target;
                location.indicator_parent = &container;
                location.indicator_rect = rect;
                return location;
            }
        }

        if (offset(position) < offset(rect.center())) {
            auto edge = previous_candidate ? (end(previous_candidate->rect) + start(rect) + 1) / 2 : start(rect) - 1;
            return insertion_location(*index, edge);
        }

        previous_candidate = &candidate;
        previous_index = *index;
    }

    if (!previous_candidate)
        return insertion_location(0, 0);

    return insertion_location(previous_index + 1, end(previous_candidate->rect) + 2);
}

Optional<BookmarksBar::DropLocation> BookmarksBar::bar_drop_location_at(QPoint position)
{
    Vector<DropCandidate> candidates;

    for (auto* action : actions()) {
        if (auto* button = as_if<QToolButton>(widgetForAction(action)); button && button->isVisible())
            candidates.append({ ak_string_from_qstring(action->property("id").toString()), button->geometry(), button });
    }

    return drop_location_in(*this, Qt::Horizontal, {}, candidates, position);
}

Optional<BookmarksBar::DropLocation> BookmarksBar::menu_drop_location_at(QMenu& menu, QPoint position)
{
    auto folder_id = ak_string_from_qstring(menu.property("target_folder_id").toString());
    if (folder_id.is_empty())
        return {};

    Vector<DropCandidate> candidates;

    for (auto* action : menu.actions()) {
        if (!action->isSeparator() && action->isVisible())
            candidates.append({ ak_string_from_qstring(bookmark_item_id_for_menu_action(*action)), menu.actionGeometry(action), action });
    }

    return drop_location_in(menu, Qt::Vertical, folder_id, candidates, position);
}

void BookmarksBar::update_drop_location(QPoint global_position)
{
    auto* menu = menu_at(global_position);

    if (menu)
        set_drop_location(menu_drop_location_at(*menu, menu->mapFromGlobal(global_position)));
    else if (auto position = mapFromGlobal(global_position); rect().contains(position))
        set_drop_location(bar_drop_location_at(position));
    else
        set_drop_location({});

    // Close any submenu of the hovered menu other than the one belonging to the hovered item.
    if (menu) {
        auto* hovered_action = menu->actionAt(menu->mapFromGlobal(global_position));

        for (auto* action : menu->actions()) {
            if (auto* submenu = action->menu(); submenu && action != hovered_action)
                collapse_menu_tree(*submenu);
        }
    }

    if (m_drag_image)
        m_drag_image->move_to(menu ? *menu : *window(), global_position);
}

void BookmarksBar::set_drop_location(Optional<DropLocation> location)
{
    auto* previous_folder_target = m_drop_location.has_value() ? m_drop_location->folder_target.data() : nullptr;
    auto* folder_target = location.has_value() ? location->folder_target.data() : nullptr;

    if (auto* indicator_parent = location.has_value() ? location->indicator_parent.data() : nullptr) {
        if (!m_drop_indicator)
            m_drop_indicator = new BookmarkDropIndicator(indicator_parent);
        else if (m_drop_indicator->parentWidget() != indicator_parent)
            m_drop_indicator->setParent(indicator_parent);

        m_drop_indicator->set_outline(folder_target != nullptr);
        m_drop_indicator->setGeometry(location->indicator_rect);

        if (m_drop_indicator->isHidden()) {
            m_drop_indicator->show();
            m_drop_indicator->raise();

            if (m_drag_image)
                m_drag_image->raise();
        }
    } else if (m_drop_indicator) {
        m_drop_indicator->hide();
    }

    m_drop_location = AK::move(location);

    if (previous_folder_target != folder_target) {
        m_spring_load_timer->stop();
        if (folder_target)
            m_spring_load_timer->start();
    }
}

void BookmarksBar::open_spring_loaded_folder()
{
    if (!m_is_dragging || !m_drop_location.has_value())
        return;

    auto open_folder_menu = [this](QMenu& folder_menu, QPoint global_position) {
        if (folder_menu.isVisible()) {
            set_menu_collapsed(folder_menu, false);
            return;
        }

        if (QGuiApplication::platformName() == "wayland")
            m_spring_load_bridges.append(open_popup_bridge(*window()));

        folder_menu.popup(global_position);
    };

    auto* folder_target = m_drop_location->folder_target.data();

    if (auto* action = as_if<QAction>(folder_target)) {
        auto* menu = as_if<QMenu>(m_drop_location->indicator_parent.data());
        if (menu && action->menu())
            open_folder_menu(*action->menu(), menu->mapToGlobal(menu->actionGeometry(action).topRight()));
        return;
    }

    auto* button = as_if<QToolButton>(folder_target);
    if (!button)
        return;

    auto* folder_menu = folder_menu_for_button(*button);
    if (!folder_menu)
        return;

    open_folder_menu(*folder_menu, button->mapToGlobal(QPoint { 0, button->height() }));

    for (auto* action : actions()) {
        if (auto* menu = action->menu(); menu && menu != folder_menu)
            collapse_menu_tree(*menu);
    }
}

bool BookmarksBar::handle_middle_mouse_click(QMouseEvent* event, QObject* item)
{
    auto activate_tab = event->modifiers().testFlag(Qt::ShiftModifier) ? Web::HTML::ActivateTab::No : Web::HTML::ActivateTab::Yes;

    if (auto* button = as_if<QToolButton>(item)) {
        auto* action = button->defaultAction();
        extract_item_properties(action);

        if (m_selected_bookmark_menu_item_type == "bookmark")
            WebView::Application::the().open_bookmark_in_new_tab(m_selected_bookmark_menu_item_id, activate_tab);
    } else if (auto* menu = as_if<QMenu>(item)) {
        if (auto* action = menu->actionAt(event->pos())) {
            extract_item_properties(action);

            if (m_selected_bookmark_menu_item_type == "bookmark")
                WebView::Application::the().open_bookmark_in_new_tab(m_selected_bookmark_menu_item_id, activate_tab);
        }
    }

    return true;
}

bool BookmarksBar::handle_right_mouse_click(QMouseEvent* event, QObject* item)
{
    if (is<BookmarksBar>(item)) {
        m_selected_bookmark_menu_item_id = {};
        m_selected_bookmark_menu_target_folder_id = {};

        bookmarks_bar_context_menu().exec(event->globalPosition().toPoint());
    } else if (auto* button = as_if<QToolButton>(item)) {
        auto* action = button->defaultAction();
        extract_item_properties(action);

        auto set_button_context_menu_property = [button = QPointer { button }](bool open) {
            if (button) {
                button->setProperty(BOOKMARK_CONTEXT_MENU_OPEN_PROPERTY, open);
                button->update();
            }
        };

        ScopeGuard guard { [&]() { set_button_context_menu_property(false); } };
        set_button_context_menu_property(true);

        if (m_selected_bookmark_menu_item_type == "bookmark")
            bookmark_context_menu().exec(event->globalPosition().toPoint());
        else if (m_selected_bookmark_menu_item_type == "folder")
            bookmark_folder_context_menu().exec(event->globalPosition().toPoint());
    } else if (auto* menu = as_if<QMenu>(item)) {
        if (auto* action = menu->actionAt(event->pos())) {
            QObject* submenu = action->menu();
            extract_item_properties(submenu ?: action);
        }

        if (m_selected_bookmark_menu_item_type.isEmpty())
            extract_item_properties(menu);

        // FIXME: We create a temporary context menu parented to the dropdown. Otherwise, Qt complains that the context
        //        menu's parent does not match the current topmost popup. It would be nice if we could figure out a way
        //        to avoid this duplicated menu.
        QMenu context_menu(menu);

        if (m_selected_bookmark_menu_item_type == "bookmark")
            repopulate_application_menu(context_menu, context_menu, m_tab->view().bookmark_context_menu());
        else if (m_selected_bookmark_menu_item_type == "folder")
            repopulate_application_menu(context_menu, context_menu, m_tab->view().bookmark_folder_context_menu());

        if (!context_menu.isEmpty() && context_menu.exec(event->globalPosition().toPoint()))
            menu->close();
    }

    return true;
}

void BookmarksBar::extract_item_properties(QObject* item)
{
    m_selected_bookmark_menu_item_id = ak_string_from_qstring(item->property("id").toString());
    m_selected_bookmark_menu_item_type = item->property("type").toString();

    if (auto value = ak_string_from_qstring(item->property("target_folder_id").toString()); !value.is_empty())
        m_selected_bookmark_menu_target_folder_id = AK::move(value);
}

QMenu& BookmarksBar::bookmarks_bar_context_menu()
{
    if (!m_bookmarks_bar_context_menu)
        m_bookmarks_bar_context_menu = create_application_menu(*this, m_tab->view().bookmarks_bar_context_menu());
    return *m_bookmarks_bar_context_menu;
}

QMenu& BookmarksBar::bookmark_context_menu()
{
    if (!m_bookmark_context_menu)
        m_bookmark_context_menu = create_application_menu(*this, m_tab->view().bookmark_context_menu());
    return *m_bookmark_context_menu;
}

QMenu& BookmarksBar::bookmark_folder_context_menu()
{
    if (!m_bookmark_folder_context_menu)
        m_bookmark_folder_context_menu = create_application_menu(*this, m_tab->view().bookmark_folder_context_menu());
    return *m_bookmark_folder_context_menu;
}

}
