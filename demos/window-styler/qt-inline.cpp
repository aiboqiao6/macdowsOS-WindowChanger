#include "adapter-api.h"
#include <commctrl.h>
#include <windowsx.h>
#include <QApplication>
#include <QWidget>
#include <QBoxLayout>
#include <QTabBar>
#include <QAbstractButton>
#include <QPainter>
#include <QMouseEvent>
#include <QPointer>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QThread>
#include <QQmlProperty>
#include <private/qqmlanybinding_p.h>
#include <array>
#include <vector>
#include <functional>
#include <QEvent>

namespace {
constexpr UINT_PTR SubclassId = 0x4D414351;
struct State;
LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
bool Enabled(HWND target, int button) {
    if (!IsWindowEnabled(target))
        return false;
    auto style = GetWindowLongPtrW(target, GWL_STYLE);
    if (button == 1)
        return (style & WS_MINIMIZEBOX) != 0;
    if (button == 2)
        return (style & WS_MAXIMIZEBOX) != 0;
    auto menu = GetSystemMenu(target, FALSE);
    UINT flags = menu ? GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) : UINT(-1);
    return flags == UINT(-1) || !(flags & (MF_GRAYED | MF_DISABLED));
}
int Hit(QPointF point, qreal height) {
    for (int i = 0; i < 3; ++i)
        if (QRectF(8 + i * 20, (height - 24) / 2, 24, 24).contains(point))
            return i;
    return -1;
}
void Paint(QPainter *painter, HWND target, qreal height) {
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor colors[]{QColor("#ff5f57"), QColor("#ffbd2e"), QColor("#28c840")};
    for (int i = 0; i < 3; ++i) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(Enabled(target, i) ? colors[i] : QColor("#808080"));
        painter->drawEllipse(QRectF(12 + i * 20, (height - 12) / 2, 12, 12));
    }
}
void Action(HWND target, int index) {
    if (index >= 0 && Enabled(target, index))
        PostMessageW(target, WM_SYSCOMMAND,
                     index == 0         ? SC_CLOSE
                     : index == 1       ? SC_MINIMIZE
                     : IsZoomed(target) ? SC_RESTORE
                                        : SC_MAXIMIZE,
                     0);
}
class WidgetLights final : public QWidget {
  public:
    WidgetLights(QWidget *parent, HWND target) : QWidget(parent), target_(target) {
        setObjectName("macdowsInjectedLights");
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setFixedWidth(84);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setAccessibleName("Window controls");
    }
    QSize sizeHint() const override {
        return {84, 24};
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        Paint(&painter, target_, height());
    }
    void mousePressEvent(QMouseEvent *e) override {
        pressed_ = Hit(e->position(), height());
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        int pressed = pressed_;
        pressed_ = -1;
        if (pressed == Hit(e->position(), height()))
            Action(target_, pressed);
        e->accept();
    }

  private:
    HWND target_{};
    int pressed_ = -1;
};
class QuickLights final : public QQuickPaintedItem {
  public:
    QuickLights(QQuickItem *parent, HWND target) : QQuickPaintedItem(parent), target_(target) {
        setObjectName("macdowsInjectedLights");
        setWidth(84);
        setHeight(parent->height());
        setZ(1000);
        setAcceptedMouseButtons(Qt::LeftButton);
        connect(parent, &QQuickItem::heightChanged, this, [this, parent] { setHeight(parent->height()); });
    }
    void paint(QPainter *painter) override {
        Paint(painter, target_, height());
    }

  protected:
    void mousePressEvent(QMouseEvent *e) override {
        pressed_ = Hit(e->position(), height());
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        int pressed = pressed_;
        pressed_ = -1;
        if (pressed == Hit(e->position(), height()))
            Action(target_, pressed);
        e->accept();
    }

  private:
    HWND target_{};
    int pressed_ = -1;
};
struct HiddenWidget {
    QPointer<QWidget> widget;
    bool hidden{};
};
struct HiddenItem {
    QPointer<QQuickItem> item;
    bool visible{};
    QQmlAnyBinding binding;
};
class PropertyGuard final : public QObject {
    Q_OBJECT
  public:
    std::function<void()> changed;
    bool enforcing{};
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Show)
            Changed();
        return false;
    }
  public slots:
    void Changed() {
        if (enforcing)
            return;
        enforcing = true;
        changed();
        enforcing = false;
    }
};
struct State {
    HWND target{}, owner{};
    HANDLE ownerProcess{};
    UINT_PTR timer{};
    unsigned systemCommands{};
    QPointer<QWidget> root, widgetLights;
    QPointer<QBoxLayout> row;
    QPointer<QTabBar> widgetTabs;
    QPointer<QQuickItem> quickLights, quickTabs;
    QVariant originalPadding;
    QQmlAnyBinding paddingBinding;
    std::vector<HiddenWidget> widgets;
    std::vector<HiddenItem> items;
    std::vector<PropertyGuard *> guards;
};
State *Live(HWND window) {
    DWORD_PTR value{};
    return GetWindowSubclass(window, Subclass, SubclassId, &value) ? reinterpret_cast<State *>(value) : nullptr;
}
bool WindowControl(const QString &name) {
    return name == "minimizeButton" || name == "maximizeButton" || name == "closeButton" ||
           name == "windowCloseButton" || name == "windowMinimizeButton" || name == "windowMaximizeButton";
}
QRectF LightRect(State *s) {
    if (s->widgetLights && s->root)
        return {QPointF(s->widgetLights->mapTo(s->root, QPoint(0, 0))), QSizeF(s->widgetLights->size())};
    if (s->quickLights)
        return {s->quickLights->mapToScene(QPointF(0, 0)), QSizeF(s->quickLights->width(), s->quickLights->height())};
    return {};
}
void Release(State *s) {
    HWND window = s->target;
    KillTimer(window, s->timer);
    RemoveWindowSubclass(window, Subclass, SubclassId);
    for (auto guard : s->guards)
        delete guard;
    s->guards.clear();
    if (s->row && s->widgetLights)
        s->row->removeWidget(s->widgetLights);
    delete s->widgetLights.data();
    for (auto &saved : s->widgets)
        if (saved.widget)
            saved.widget->setHidden(saved.hidden);
    if (s->row)
        s->row->activate();
    delete s->quickLights.data();
    if (s->quickTabs) {
        s->quickTabs->setProperty("leftPadding", s->originalPadding);
        if (s->paddingBinding)
            s->paddingBinding.installOn(QQmlProperty(s->quickTabs, "leftPadding"));
    }
    for (auto &saved : s->items)
        if (saved.item) {
            saved.item->setVisible(saved.visible);
            if (saved.binding)
                saved.binding.installOn(QQmlProperty(saved.item, "visible"));
        }
    RemovePropW(window, styler::InlineProperty);
    RemovePropW(window, styler::InlineControlProperty);
    RemovePropW(window, styler::OwnerProperty);
    RemovePropW(window, styler::AdapterProperty);
    if (s->ownerProcess)
        CloseHandle(s->ownerProcess);
    delete s;
}
LRESULT CALLBACK Subclass(HWND window, UINT message, WPARAM param, LPARAM data, UINT_PTR, DWORD_PTR reference) {
    auto *s = reinterpret_cast<State *>(reference);
    if (message == styler::QueryMessage())
        return styler::Acknowledged;
    if (message == styler::DetachMessage()) {
        if (reinterpret_cast<HWND>(param) != s->owner)
            return 0;
        if (s->systemCommands)
            return styler::TransitionBusy;
        Release(s);
        return styler::Acknowledged;
    }
    if (message == styler::DiagnosticMessage()) {
        auto r = LightRect(s);
        auto scale = [window](qreal n) { return qRound(n * GetDpiForWindow(window) / 96.0); };
        switch (static_cast<styler::Metric>(param)) {
        case styler::Metric::InlineLeft:
            return scale(r.x());
        case styler::Metric::InlineTop:
            return scale(r.y());
        case styler::Metric::InlineWidth:
            return scale(r.width());
        case styler::Metric::InlineHeight:
            return scale(r.height());
        case styler::Metric::InlineKind:
            return s->widgetLights ? 1 : 2;
        case styler::Metric::InlineTabsLeft:
            if (s->widgetTabs && s->root)
                return scale(s->widgetTabs->mapTo(s->root, QPoint(0, 0)).x());
            else if (s->quickTabs)
                return scale(s->quickTabs->mapToScene(QPointF(s->quickTabs->property("leftPadding").toReal(), 0)).x());
            return 0;
        case styler::Metric::InlineTabIndex:
            return s->widgetTabs  ? s->widgetTabs->currentIndex()
                   : s->quickTabs ? s->quickTabs->property("currentIndex").toInt()
                                  : -1;
        case styler::Metric::InlineOriginalButtonsVisible: {
            int n{};
            for (auto &v : s->widgets)
                if (v.widget && !v.widget->isHidden())
                    ++n;
            for (auto &v : s->items)
                if (v.item && v.item->isVisible())
                    ++n;
            return n;
        }
        default:
            return 0;
        }
    }
    if (message == WM_TIMER && param == s->timer) {
        if (!IsWindow(s->owner) || WaitForSingleObject(s->ownerProcess, 0) != WAIT_TIMEOUT)
            Release(s);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        Release(s);
        return DefSubclassProc(window, message, param, data);
    }
    if (message == WM_SYSCOMMAND) {
        ++s->systemCommands;
        auto result = DefSubclassProc(window, message, param, data);
        if (auto *live = Live(window))
            --live->systemCommands;
        return result;
    }
    return DefSubclassProc(window, message, param, data);
}
bool AttachWidgets(State *s) {
    auto *root = QWidget::find(WId(s->target));
    if (!root || root->window() != root || !(root->windowFlags() & Qt::FramelessWindowHint))
        return false;
    for (auto *tabs : root->findChildren<QTabBar *>()) {
        if (tabs->mapTo(root, QPoint(0, 0)).y() > 64 || !tabs->isVisibleTo(root))
            continue;
        auto *parent = tabs->parentWidget();
        auto *row = qobject_cast<QBoxLayout *>(parent->layout());
        if (!row || row->direction() != QBoxLayout::LeftToRight || row->indexOf(tabs) < 0)
            continue;
        s->root = root;
        s->row = row;
        s->widgetTabs = tabs;
        for (auto *button : parent->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly))
            if (WindowControl(button->objectName())) {
                s->widgets.push_back({button, button->isHidden()});
                button->hide();
            }
        if (s->widgets.empty())
            return false;
        s->widgetLights = new WidgetLights(parent, s->target);
        row->insertWidget(0, s->widgetLights);
        s->widgetLights->show();
        row->activate();
        for (size_t i = 0; i < s->widgets.size(); ++i) {
            auto guard = new PropertyGuard;
            guard->changed = [s, i] {
                auto &saved = s->widgets[i];
                if (saved.widget) {
                    saved.hidden = false;
                    saved.widget->hide();
                }
            };
            s->widgets[i].widget->installEventFilter(guard);
            s->guards.push_back(guard);
        }
        return true;
    }
    return false;
}
bool AttachQuick(State *s) {
    for (auto *window : QGuiApplication::allWindows()) {
        if (window->winId() != WId(s->target))
            continue;
        auto *quick = qobject_cast<QQuickWindow *>(window);
        if (!quick || !(window->flags() & Qt::FramelessWindowHint))
            return false;
        for (auto *tabs : quick->contentItem()->findChildren<QQuickItem *>()) {
            if (!QByteArray(tabs->metaObject()->className()).contains("TabBar") ||
                tabs->mapToScene(QPointF(0, 0)).y() > 64 || !tabs->isVisible() ||
                !tabs->property("leftPadding").isValid())
                continue;
            s->quickTabs = tabs;
            s->originalPadding = tabs->property("leftPadding");
            for (auto *item : quick->contentItem()->findChildren<QQuickItem *>())
                if (WindowControl(item->objectName()) && item->mapToScene(QPointF(0, 0)).y() < 64) {
                    s->items.push_back(
                        {item, item->isVisible(), QQmlAnyBinding::takeFrom(QQmlProperty(item, "visible"))});
                    item->setVisible(false);
                }
            if (s->items.empty())
                return false;
            s->paddingBinding = QQmlAnyBinding::takeFrom(QQmlProperty(tabs, "leftPadding"));
            tabs->setProperty("leftPadding", s->originalPadding.toReal() + 84);
            s->quickLights = new QuickLights(tabs, s->target);
            for (size_t i = 0; i < s->items.size(); ++i) {
                auto guard = new PropertyGuard;
                guard->changed = [s, i] {
                    auto &saved = s->items[i];
                    if (saved.item && saved.item->isVisible()) {
                        saved.visible = true;
                        saved.binding = QQmlAnyBinding::takeFrom(QQmlProperty(saved.item, "visible"));
                        saved.item->setVisible(false);
                    }
                };
                QQmlProperty(s->items[i].item, "visible").connectNotifySignal(guard, SLOT(Changed()));
                s->guards.push_back(guard);
            }
            auto paddingGuard = new PropertyGuard;
            paddingGuard->changed = [s] {
                if (!s->quickTabs)
                    return;
                s->originalPadding = s->quickTabs->property("leftPadding");
                s->paddingBinding = QQmlAnyBinding::takeFrom(QQmlProperty(s->quickTabs, "leftPadding"));
                s->quickTabs->setProperty("leftPadding", s->originalPadding.toReal() + 84);
            };
            QQmlProperty(tabs, "leftPadding").connectNotifySignal(paddingGuard, SLOT(Changed()));
            s->guards.push_back(paddingGuard);
            return true;
        }
    }
    return false;
}
} // namespace
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context))
        return ERROR_INVALID_PARAMETER;
    HWND window = context->window, owner = context->owner;
    if (Live(window))
        return Live(window)->owner == owner ? ERROR_SUCCESS : ERROR_ALREADY_EXISTS;
    auto *app = QCoreApplication::instance();
    if (!app || app->thread() != QThread::currentThread())
        return ERROR_NOT_SUPPORTED;
    auto *s = new State;
    s->target = window;
    s->owner = owner;
    DWORD pid{};
    GetWindowThreadProcessId(owner, &pid);
    s->ownerProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!s->ownerProcess) {
        delete s;
        return ERROR_ACCESS_DENIED;
    }
    if (!AttachWidgets(s) && !AttachQuick(s)) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    if (!SetWindowSubclass(window, Subclass, SubclassId, reinterpret_cast<DWORD_PTR>(s))) {
        Release(s);
        return ERROR_NOT_SUPPORTED;
    }
    s->timer = SetTimer(window, reinterpret_cast<UINT_PTR>(s), 750, nullptr);
    if (!s->timer || !SetPropW(window, styler::OwnerProperty, owner)) {
        Release(s);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    SetPropW(window, styler::AdapterProperty, reinterpret_cast<HANDLE>(context->adapterId));
    SetPropW(window, styler::InlineProperty, reinterpret_cast<HANDLE>(UINT_PTR(s->widgetLights ? 1 : 2)));
    return ERROR_SUCCESS;
}

extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *context) {
    if (!styler::ValidContext(context))
        return ERROR_INVALID_PARAMETER;
    auto *app = QCoreApplication::instance();
    if (!app || app->thread() != QThread::currentThread())
        return ERROR_NOT_SUPPORTED;
    if (auto *root = QWidget::find(WId(context->window));
        root && root->window() == root && (root->windowFlags() & Qt::FramelessWindowHint)) {
        for (auto *tabs : root->findChildren<QTabBar *>()) {
            auto *parent = tabs->parentWidget();
            auto *row = qobject_cast<QBoxLayout *>(parent->layout());
            if (!row || row->direction() != QBoxLayout::LeftToRight || row->indexOf(tabs) < 0 ||
                tabs->mapTo(root, QPoint(0, 0)).y() > 64 || !tabs->isVisibleTo(root))
                continue;
            for (auto *b : parent->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly))
                if (WindowControl(b->objectName()))
                    return ERROR_SUCCESS;
        }
    }
    for (auto *w : QGuiApplication::allWindows())
        if (w->winId() == WId(context->window)) {
            auto *quick = qobject_cast<QQuickWindow *>(w);
            if (!quick || !(w->flags() & Qt::FramelessWindowHint))
                continue;
            bool tabs = false, buttons = false;
            for (auto *item : quick->contentItem()->findChildren<QQuickItem *>()) {
                if (item->mapToScene(QPointF(0, 0)).y() > 64)
                    continue;
                tabs |= QByteArray(item->metaObject()->className()).contains("TabBar") && item->isVisible() &&
                        item->property("leftPadding").isValid();
                buttons |= WindowControl(item->objectName());
            }
            if (tabs && buttons)
                return ERROR_SUCCESS;
        }
    return ERROR_NOT_SUPPORTED;
}
#include "qt-inline.moc"
