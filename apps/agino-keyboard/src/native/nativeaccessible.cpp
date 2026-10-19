// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativeaccessible.h"
#include <QAccessibleObject>
#include <QGuiApplication>
#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <cmath>
#include <utility>

namespace {
NativeAccessibleHost *host(QWindow *window)
{
    return window ? dynamic_cast<NativeAccessibleHost *>(window) : nullptr;
}

bool findControl(QWindow *window, const QString &id, NativeControl *result)
{
    auto *panel = host(window);
    if (!panel) return false;
    for (const auto &control : panel->accessibleControls()) {
        if (control.id == id) { *result = control; return true; }
    }
    return false;
}

QRect controlRect(QWindow *window, const NativeControl &control)
{
    if (!window) return {};
    const QRectF clipped = control.rect.intersected(QRectF(QPointF(), window->size()));
    if (clipped.isEmpty()) return {};
    return clipped.toAlignedRect().translated(window->mapToGlobal(QPoint()));
}

struct AccessibilityUpdate {
    QAccessible::Id id;
    QAccessible::Event type;
    QAccessible::State changed;
    QVariant value;
};

QAccessible::State controlState(QWindow *window, const NativeControl &control)
{
    QAccessible::State state;
    state.disabled = !control.enabled;
    state.invisible = !window || !window->isVisible() || controlRect(window, control).isEmpty();
    state.offscreen = state.invisible;
    state.checkable = control.checkable;
    state.checked = control.checked;
    state.pressed = control.pressed;
    return state;
}

QAccessible::State changedState(QAccessible::State before, QAccessible::State after)
{
    QAccessible::State changed;
    changed.disabled = before.disabled != after.disabled;
    changed.invisible = before.invisible != after.invisible;
    changed.offscreen = before.offscreen != after.offscreen;
    changed.checkable = before.checkable != after.checkable;
    changed.checked = before.checked != after.checked;
    changed.pressed = before.pressed != after.pressed;
    return changed;
}

bool anyStateChanged(QAccessible::State state)
{
    return state.disabled || state.invisible || state.offscreen || state.checkable
        || state.checked || state.pressed;
}

bool sameIdentity(const NativeControl &before, const NativeControl &after)
{
    return before.id == after.id && before.context == after.context
        && before.generation == after.generation && before.sym == after.sym
        && before.role == after.role;
}

class AccessibleControl final : public QAccessibleInterface,
                                public QAccessibleActionInterface,
                                public QAccessibleValueInterface {
public:
    AccessibleControl(QWindow *window, const NativeControl &control)
        : m_window(window), m_id(control.id), m_context(control.context),
          m_generation(control.generation), m_sym(control.sym), m_role(control.role) {}
    bool current(NativeControl *control) const {
        return findControl(m_window, m_id, control) && control->context == m_context
            && control->generation == m_generation && control->sym == m_sym && control->role == m_role;
    }
    bool isValid() const override { NativeControl c; return current(&c); }
    QObject *object() const override { return nullptr; }
    QWindow *window() const override { return m_window; }
    QAccessibleInterface *parent() const override {
        return m_window ? QAccessible::queryAccessibleInterface(m_window) : nullptr;
    }
    QAccessibleInterface *child(int) const override { return nullptr; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QString text(QAccessible::Text type) const override {
        NativeControl c;
        if (!current(&c)) return {};
        if (type == QAccessible::Name) return c.name;
        if (type == QAccessible::Value && c.role == QAccessible::Slider)
            return c.value.toMap().value(QStringLiteral("current")).toString();
        return {};
    }
    void setText(QAccessible::Text, const QString &) override {}
    QRect rect() const override {
        NativeControl c; return current(&c) ? controlRect(m_window, c) : QRect();
    }
    QAccessible::Role role() const override {
        NativeControl c; return current(&c) ? c.role : QAccessible::NoRole;
    }
    QAccessible::State state() const override {
        QAccessible::State state;
        NativeControl c;
        if (!current(&c)) { state.invalid = true; state.invisible = true; return state; }
        return controlState(m_window, c);
    }
    void *interface_cast(QAccessible::InterfaceType type) override {
        if (type == QAccessible::ActionInterface) return static_cast<QAccessibleActionInterface *>(this);
        if (type == QAccessible::ValueInterface && role() == QAccessible::Slider)
            return static_cast<QAccessibleValueInterface *>(this);
        return nullptr;
    }
    QStringList actionNames() const override {
        NativeControl c;
        if (!current(&c)) return {};
        if (c.role == QAccessible::Slider) return {increaseAction(), decreaseAction()};
        if (c.role == QAccessible::List) return {scrollLeftAction(), scrollRightAction(), scrollUpAction(), scrollDownAction()};
        QStringList result{pressAction()};
        if (c.checkable || c.role == QAccessible::RadioButton) result.append(toggleAction());
        return result;
    }
    void doAction(const QString &action) override {
        NativeControl c;
        if (!current(&c) || !c.enabled || state().invisible || !actionNames().contains(action)) return;
        if (auto *panel = host(m_window)) panel->invokeAccessible(m_id, action);
    }
    QStringList keyBindingsForAction(const QString &) const override { return {}; }
    QVariant currentValue() const override { return value(QStringLiteral("current")); }
    QVariant maximumValue() const override { return value(QStringLiteral("maximum")); }
    QVariant minimumValue() const override { return value(QStringLiteral("minimum")); }
    QVariant minimumStepSize() const override { return value(QStringLiteral("step")); }
    void setCurrentValue(const QVariant &value) override {
        NativeControl c;
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number) || !current(&c) || c.role != QAccessible::Slider
            || !c.enabled || state().invisible) return;
        const QVariantMap range = c.value.toMap();
        if (!range.contains(QStringLiteral("minimum")) || !range.contains(QStringLiteral("maximum"))) return;
        const double minimum = range.value(QStringLiteral("minimum")).toDouble();
        const double maximum = range.value(QStringLiteral("maximum")).toDouble();
        if (number < minimum || number > maximum) return;
        if (auto *panel = host(m_window))
            panel->invokeAccessible(m_id, QStringLiteral("setValue:") + QString::number(number, 'g', 17));
    }
private:
    QVariant value(const QString &key) const {
        NativeControl c;
        return current(&c) && c.role == QAccessible::Slider ? c.value.toMap().value(key) : QVariant();
    }
    QPointer<QWindow> m_window;
    QString m_id;
    quint64 m_context, m_generation;
    quint32 m_sym;
    QAccessible::Role m_role;
};

class AccessiblePanel final : public QAccessibleObject {
public:
    explicit AccessiblePanel(QWindow *window) : QAccessibleObject(window), m_window(window),
        m_previous(controls()), m_previousRect(rect()), m_notifiedRect(m_previousRect), m_previousVisible(window->isVisible()) {}
    ~AccessiblePanel() override {
        // The Qt accessibility cache owns interfaces. Never also delete their pointers.
        for (auto id : std::as_const(m_children)) QAccessible::deleteAccessibleInterface(id);
    }
    QWindow *window() const override { return m_window; }
    QAccessibleInterface *parent() const override {
        return QAccessible::queryAccessibleInterface(qGuiApp);
    }
    QAccessible::Role role() const override { return QAccessible::Pane; }
    QString text(QAccessible::Text type) const override {
        return type == QAccessible::Name ? QCoreApplication::translate("Keyboard","Screen Keyboard") : QString();
    }
    QRect rect() const override {
        return m_window ? QRect(m_window->mapToGlobal(QPoint()), m_window->size()) : QRect();
    }
    QAccessible::State state() const override {
        QAccessible::State state;
        state.invalid = !m_window;
        state.invisible = !m_window || !m_window->isVisible();
        return state;
    }
    QVector<NativeControl> controls() const {
        auto *panel = host(m_window);
        return panel ? panel->accessibleControls() : QVector<NativeControl>();
    }
    void reconcile() const {
        const auto current = controls();
        for (auto it = m_children.begin(); it != m_children.end();) {
            auto *iface = QAccessible::accessibleInterface(it.value());
            bool retained = false;
            for (const auto &control : current) if (control.id == it.key()) { retained = true; break; }
            if (!retained || !iface || !iface->isValid()) {
                const auto id = it.value();
                it = m_children.erase(it);
                if (iface) QAccessible::deleteAccessibleInterface(id);
            } else ++it;
        }
    }
    QVector<AccessibilityUpdate> takeUpdates(QAccessible::Event requested) {
        const auto current = controls();
        const QRect currentRect = rect();
        const bool visible = m_window && m_window->isVisible();
        bool reordered = current.size() != m_previous.size();
        if (!reordered) for (int index = 0; index < current.size(); ++index)
            if (!sameIdentity(current[index], m_previous[index])) { reordered = true; break; }

        QVector<AccessibilityUpdate> updates;
        // Inspect only interfaces already requested by an accessibility client.
        // Neither notifications nor ordinary paints allocate one child per key.
        for (const auto &now : current) {
            const auto found = m_children.constFind(now.id);
            if (found == m_children.cend()) continue;
            const NativeControl *before = nullptr;
            for (const auto &old : std::as_const(m_previous))
                if (sameIdentity(old, now)) { before = &old; break; }
            if (!before) continue;
            const auto id = found.value();
            if (before->name != now.name) updates.append({id, QAccessible::NameChanged, {}, {}});
            // Previous rectangles are local; account for a window move as well.
            const QRectF oldClipped = before->rect.intersected(QRectF(QPointF(), m_previousRect.size()));
            const QRect beforeRect = oldClipped.isEmpty() ? QRect()
                : oldClipped.toAlignedRect().translated(m_previousRect.topLeft());
            if (beforeRect != controlRect(m_window, now)) updates.append({id, QAccessible::LocationChanged, {}, {}});
            QAccessible::State oldState;
            oldState.disabled = !before->enabled;
            oldState.invisible = !m_previousVisible || beforeRect.isEmpty();
            oldState.offscreen = oldState.invisible;
            oldState.checkable = before->checkable;
            oldState.checked = before->checked;
            oldState.pressed = before->pressed;
            const auto changed = changedState(oldState, controlState(m_window, now));
            if (anyStateChanged(changed)) updates.append({id, QAccessible::StateChanged, changed, {}});
            if (now.role == QAccessible::Slider
                && before->value.toMap().value(QStringLiteral("current")) != now.value.toMap().value(QStringLiteral("current")))
                updates.append({id, QAccessible::ValueChanged, {}, now.value.toMap().value(QStringLiteral("current"))});
        }
        // Commit the snapshot before delivering notifications: a bridge callback
        // may query/reenter the tree or synchronously destroy the window.
        m_previous = current;
        m_previousRect = currentRect;
        m_previousVisible = visible;
        reconcile();
        const auto rootId = QAccessible::uniqueId(this);
        if (reordered) updates.append({rootId, QAccessible::ObjectReorder, {}, {}});
        if (currentRect != m_notifiedRect) {
            m_notifiedRect = currentRect;
            updates.append({rootId, QAccessible::LocationChanged, {}, {}});
        }
        // ObjectReorder is the callers' default "check for changes" request.
        // Payload-requiring event kinds are emitted above with their typed data.
        if (requested == QAccessible::ObjectShow || requested == QAccessible::ObjectHide
            || requested == QAccessible::NameChanged || requested == QAccessible::DescriptionChanged)
            updates.append({rootId, requested, {}, {}});
        return updates;
    }
    int childCount() const override { reconcile(); return controls().size(); }
    QAccessibleInterface *child(int index) const override {
        reconcile();
        const auto current = controls();
        if (index < 0 || index >= current.size()) return nullptr;
        const auto &control = current[index];
        if (control.id.isEmpty()) return nullptr;
        const auto found = m_children.constFind(control.id);
        if (found != m_children.cend()) return QAccessible::accessibleInterface(found.value());
        auto *result = new AccessibleControl(m_window, control);
        m_children.insert(control.id, QAccessible::registerAccessibleInterface(result));
        return result;
    }
    int indexOfChild(const QAccessibleInterface *child) const override {
        if (!child) return -1;
        const auto current = controls();
        for (int index = 0; index < current.size(); ++index) {
            const auto found = m_children.constFind(current[index].id);
            if (found != m_children.cend() && QAccessible::accessibleInterface(found.value()) == child)
                return child->isValid() ? index : -1;
        }
        return -1;
    }
    QAccessibleInterface *childAt(int x, int y) const override {
        const auto current = controls();
        // Lists represent the scroll fallback, including when the panel appends
        // one after its candidate buttons. Match native pointer hit priorities.
        for (bool lists : {false, true}) {
            for (int index = current.size() - 1; index >= 0; --index)
                if ((current[index].role == QAccessible::List) == lists
                    && controlRect(m_window, current[index]).contains(x, y)) return child(index);
        }
        return nullptr;
    }
private:
    QPointer<QWindow> m_window;
    mutable QHash<QString, QAccessible::Id> m_children;
    QVector<NativeControl> m_previous;
    QRect m_previousRect, m_notifiedRect;
    bool m_previousVisible;
};

QAccessibleInterface *factory(const QString &, QObject *object)
{
    auto *panel = dynamic_cast<NativeAccessibleHost *>(object);
    if (!panel || panel->accessibleWindow() != object) return nullptr;
    return new AccessiblePanel(panel->accessibleWindow());
}
}

void installNativeAccessibility()
{
    static const bool installed = [] { QAccessible::installFactory(factory); return true; }();
    Q_UNUSED(installed);
}

void notifyNativeAccessibility(QWindow *window, QAccessible::Event event)
{
    // Do not create a tree or activate the platform bridge while accessibility is unused.
    if (!window || !QAccessible::isActive()) return;
    auto *panel = dynamic_cast<AccessiblePanel *>(QAccessible::queryAccessibleInterface(window));
    if (!panel) return;
    const auto updates = panel->takeUpdates(event);
    const QPointer<QWindow> guard(window);
    for (const auto &change : updates) {
        if (!guard) break;
        auto *target = QAccessible::accessibleInterface(change.id);
        if (!target || !target->isValid()) continue;
        if (change.type == QAccessible::StateChanged) {
            QAccessibleStateChangeEvent update(target, change.changed);
            QAccessible::updateAccessibility(&update);
        } else if (change.type == QAccessible::ValueChanged) {
            QAccessibleValueChangeEvent update(target, change.value);
            QAccessible::updateAccessibility(&update);
        } else if (auto *object = target->object()) {
            // QObject-backed events use a child index internally. The interface
            // overload is for virtual children with object()==nullptr.
            QAccessibleEvent update(object, change.type);
            QAccessible::updateAccessibility(&update);
        } else {
            QAccessibleEvent update(target, change.type);
            QAccessible::updateAccessibility(&update);
        }
    }
}
