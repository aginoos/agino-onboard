// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone QtGui-only regression test; run with QT_QPA_PLATFORM=offscreen.
#include "nativeaccessible.h"
#include <QGuiApplication>
#include <QPointer>
#include <QVariantMap>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); std::abort(); } } while (false)
class TestPanel final : public QWindow, public NativeAccessibleHost {
public:
    QVector<NativeControl> controls;
    QString lastId, lastAction;
    int calls = 0;
    mutable int queries = 0;
    bool live = false;
    QWindow *accessibleWindow() const override { return const_cast<TestPanel *>(this); }
    QAccessibleInterface *accessibleRoot() const override { return QAccessible::queryAccessibleInterface(const_cast<TestPanel *>(this)); }
    QVector<NativeControl> accessibleControls() const override { ++queries; return controls; }
    bool invokeAccessible(const QString &id, const QString &action) override {
        lastId = id; lastAction = action; ++calls;
        if (live) for (auto &control : controls) if (control.id == id) {
            if (control.checkable) control.checked = !control.checked;
            if (control.role == QAccessible::Slider) {
                auto value = control.value.toMap();
                value["current"] = action.startsWith("setValue:") ? action.mid(9).toDouble()
                    : value["current"].toDouble() + (action == QAccessibleActionInterface::decreaseAction() ? -1 : 1);
                control.value = value;
            }
            notifyNativeAccessibility(this);
            break;
        }
        return true;
    }
};

struct CapturedEvent { QAccessible::Id id; QAccessible::Event type; QAccessible::State changed; QVariant value; };
static QVector<CapturedEvent> captured;
static QPointer<QWindow> destroyOnEvent;
static void captureEvent(QAccessibleEvent *event)
{
    CapturedEvent result{event->uniqueId(), event->type(), {}, {}};
    if (event->type() == QAccessible::StateChanged) result.changed = static_cast<QAccessibleStateChangeEvent *>(event)->changedStates();
    if (event->type() == QAccessible::ValueChanged) result.value = static_cast<QAccessibleValueChangeEvent *>(event)->value();
    captured.append(result);
    if (destroyOnEvent) {
        auto *window=destroyOnEvent.data();destroyOnEvent.clear();delete window;
    }
}

static void notificationTests()
{
    if(qEnvironmentVariableIntValue("QT_LINUX_ACCESSIBILITY_ALWAYS_ON")) {
        QElapsedTimer wait;wait.start();
        while(!QAccessible::isActive()&&wait.elapsed()<2000){QCoreApplication::processEvents();QThread::msleep(10);}
        CHECK(QAccessible::isActive());
    }
    TestPanel panel;
    panel.resize(300, 200);panel.show();
    NativeControl button;button.id="button";button.name="Button";button.checkable=true;button.rect={10,10,60,30};
    NativeControl slider;slider.id="slider";slider.name="Opacity";slider.role=QAccessible::Slider;slider.rect={10,60,200,30};
    slider.value=QVariantMap{{"current",80},{"minimum",50},{"maximum",100},{"step",1}};
    panel.controls={button,slider};
    auto *root=QAccessible::queryAccessibleInterface(&panel);
    const auto buttonId=QAccessible::uniqueId(root->child(0)),sliderId=QAccessible::uniqueId(root->child(1));
    if (!QAccessible::isActive()) {
        const int queries=panel.queries;notifyNativeAccessibility(&panel);CHECK(panel.queries==queries);
        std::puts("Inactive notifier is inert; typed notification tests require an active platform accessibility bridge");
        return;
    }
    const auto previousHandler=QAccessible::installUpdateHandler(captureEvent);
    captured.clear();notifyNativeAccessibility(&panel);CHECK(captured.isEmpty());
    panel.controls[0].name="Renamed";panel.controls[0].rect.translate(5,0);panel.controls[0].checked=true;panel.controls[0].pressed=true;
    auto value=panel.controls[1].value.toMap();value["current"]=85;panel.controls[1].value=value;
    notifyNativeAccessibility(&panel);
    bool name=false,location=false,state=false,number=false;
    for(const auto &event:std::as_const(captured)){
        CHECK(event.type!=QAccessible::ObjectReorder);
        if(event.id==buttonId&&event.type==QAccessible::NameChanged)name=true;
        if(event.id==buttonId&&event.type==QAccessible::LocationChanged)location=true;
        if(event.id==buttonId&&event.type==QAccessible::StateChanged){CHECK(event.changed.checked&&event.changed.pressed);state=true;}
        if(event.id==sliderId&&event.type==QAccessible::ValueChanged){CHECK(event.value.toInt()==85);number=true;}
    }
    CHECK(name&&location&&state&&number);
    captured.clear();notifyNativeAccessibility(&panel);CHECK(captured.isEmpty());
    std::swap(panel.controls[0],panel.controls[1]);notifyNativeAccessibility(&panel);
    CHECK(captured.size()==1&&captured[0].type==QAccessible::ObjectReorder);
    captured.clear();panel.controls.removeLast();notifyNativeAccessibility(&panel);
    CHECK(captured.size()==1&&captured[0].type==QAccessible::ObjectReorder);
    CHECK(QAccessible::accessibleInterface(buttonId)==nullptr);
    auto *reentrant=new TestPanel;reentrant->resize(300,200);reentrant->show();reentrant->controls={button,slider};
    auto *reentrantRoot=QAccessible::queryAccessibleInterface(reentrant);const auto reentrantId=QAccessible::uniqueId(reentrantRoot->child(0));
    reentrantRoot->child(1);reentrant->controls[0].name="Destroy during notification";reentrant->controls[0].checked=true;
    QPointer<TestPanel> guard(reentrant);destroyOnEvent=reentrant;notifyNativeAccessibility(reentrant);
    CHECK(!guard&&QAccessible::accessibleInterface(reentrantId)==nullptr);
    TestPanel clipped;clipped.setGeometry(100,200,300,200);clipped.show();
    button.rect={500,500,30,30};clipped.controls={button};
    QAccessible::queryAccessibleInterface(&clipped)->child(0);captured.clear();
    notifyNativeAccessibility(&clipped);CHECK(captured.isEmpty());
    QAccessible::installUpdateHandler(previousHandler);
    std::puts("Typed child state/value/name/location notifications and identity-only reorder passed");
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    installNativeAccessibility();
    installNativeAccessibility();
    if(app.arguments().contains("--atspi-fixture")) {
        std::printf("Qt %s native accessibility fixture\n",qVersion());std::fflush(stdout);
        auto *fixture=new TestPanel;fixture->live=true;fixture->setTitle("Native accessibility fixture");fixture->resize(400,200);
        NativeControl toggle;toggle.id="toggle";toggle.name="Toggle test";toggle.checkable=true;toggle.rect={10,10,100,30};
        NativeControl opacity;opacity.id="opacity";opacity.name="Opacity test";opacity.role=QAccessible::Slider;opacity.rect={10,60,200,30};opacity.value=QVariantMap{{"current",80},{"minimum",50},{"maximum",100},{"step",1}};
        NativeControl candidate;candidate.id="candidate:1";candidate.name="Candidate test";candidate.rect={10,110,100,30};
        fixture->controls={toggle,opacity,candidate};fixture->show();
        QTimer::singleShot(500,fixture,[fixture]{notifyNativeAccessibility(fixture,QAccessible::ObjectShow);});
        QTimer::singleShot(2000,fixture,[fixture]{fixture->controls[0].rect.translate(5,0);fixture->controls[1].name="Renamed opacity test";notifyNativeAccessibility(fixture);});
        QTimer::singleShot(5000,fixture,[fixture]{fixture->controls.removeLast();notifyNativeAccessibility(fixture);});
        QTimer::singleShot(8000,fixture,[fixture]{fixture->hide();notifyNativeAccessibility(fixture,QAccessible::ObjectHide);delete fixture;});
        QTimer::singleShot(10000,&app,&QCoreApplication::quit);
        return app.exec();
    }
    auto *panel = new TestPanel;
    panel->setGeometry(100, 200, 300, 200);
    panel->show();
    app.processEvents();
    NativeControl key;
    key.id = QStringLiteral("key-shift"); key.name = QStringLiteral("Shift");
    key.rect = QRectF(-10, 10, 40, 30); key.checkable = true; key.checked = true; key.pressed = true;
    NativeControl candidate;
    candidate.id = QStringLiteral("candidate:42:7:0"); candidate.name = QStringLiteral("候选");
    candidate.context = 42; candidate.generation = 7; candidate.rect = QRectF(50, 10, 100, 30);
    NativeControl slider;
    slider.id = QStringLiteral("opacity"); slider.role = QAccessible::Slider;
    slider.rect = QRectF(20, 80, 200, 30);
    slider.value = QVariantMap{{"current", 80}, {"minimum", 50}, {"maximum", 100}, {"step", 1}};
    NativeControl list;
    list.id = QStringLiteral("candidate-list"); list.role = QAccessible::List;
    list.rect = QRectF(40, 0, 150, 60);
    panel->controls = {key, list, candidate, slider};
    const auto objectCount = panel->children().size();
    auto *root = QAccessible::queryAccessibleInterface(panel);
    CHECK(root && root->role() == QAccessible::Pane);
    CHECK(root->parent() && root->parent()->role() == QAccessible::Application);
    CHECK(root->childCount() == 4 && root->child(-1) == nullptr && root->child(4) == nullptr);
    CHECK(root->childCount() == 4 && panel->children().size() == objectCount);
    auto *shift = root->child(0);
    const auto shiftId = QAccessible::uniqueId(shift);
    CHECK(shift->rect() == QRect(panel->mapToGlobal(QPoint(0, 10)), QSize(30, 30)));
    CHECK(shift->role() == QAccessible::Button && shift->text(QAccessible::Name) == QStringLiteral("Shift"));
    CHECK(shift->state().checked && shift->state().pressed && shift->state().checkable);
    shift->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    CHECK(panel->calls == 1 && panel->lastId == key.id && panel->lastAction == QAccessibleActionInterface::toggleAction());
    auto *word = root->child(2);
    const auto wordId = QAccessible::uniqueId(word);
    const QPoint wordPoint = panel->mapToGlobal(QPoint(60, 20));
    CHECK(root->childAt(wordPoint.x(), wordPoint.y()) == word);
    // Actual panel rendering appends the candidate list after its buttons.
    std::swap(panel->controls[1], panel->controls[2]);
    CHECK(root->childAt(wordPoint.x(), wordPoint.y()) == word);
    const QPoint listOnlyPoint = panel->mapToGlobal(QPoint(180, 50));
    CHECK(root->childAt(listOnlyPoint.x(), listOnlyPoint.y()) == root->child(2));
    std::swap(panel->controls[1], panel->controls[2]);
    CHECK(word->parent() == root);
    auto *range = root->child(3)->valueInterface();
    CHECK(range && range->currentValue() == 80 && range->minimumValue() == 50 && range->maximumValue() == 100 && range->minimumStepSize() == 1);
    range->setCurrentValue(85);
    CHECK(panel->calls == 2 && panel->lastAction == QStringLiteral("setValue:85"));
    range->setCurrentValue(101);
    CHECK(panel->calls == 2);
    root->child(1)->actionInterface()->doAction(QAccessibleActionInterface::scrollRightAction());
    CHECK(panel->calls == 3);
    panel->controls[0].enabled = false;
    shift->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(panel->calls == 3 && shift->state().disabled);
    panel->controls[0].enabled = true;
    panel->hide();
    shift->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(panel->calls == 3 && shift->state().invisible);
    panel->show();
    std::swap(panel->controls[0], panel->controls[2]);
    CHECK(root->child(2) == shift && root->indexOfChild(shift) == 2);
    CHECK(root->child(0) == word && root->indexOfChild(word) == 0);
    // A candidate from the preceding input revision must not call into the host.
    panel->controls[0].generation++;
    CHECK(!word->isValid());
    word->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(panel->calls == 3);
    auto *newWord = root->child(0);
    CHECK(newWord && QAccessible::accessibleInterface(wordId) == nullptr);
    const auto newWordId = QAccessible::uniqueId(newWord);
    CHECK(newWordId != wordId);
    panel->controls.removeAt(0);
    CHECK(!newWord->isValid());
    newWord->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(panel->calls == 3);
    CHECK(root->childCount() == 3 && QAccessible::accessibleInterface(newWordId) == nullptr);
    // Repeated input revisions retire their cache entries instead of accumulating
    // one QObject/interface for every candidate ever shown.
    for (int revision = 0; revision < 1000; ++revision) {
        candidate.id = QStringLiteral("candidate-revision:%1").arg(revision);
        candidate.generation = revision;
        panel->controls.append(candidate);
        auto *transient = root->child(panel->controls.size() - 1);
        CHECK(transient && transient->isValid());
        const auto transientId = QAccessible::uniqueId(transient);
        panel->controls.removeLast();
        CHECK(root->childCount() == 3);
        CHECK(QAccessible::accessibleInterface(transientId) == nullptr);
    }
    CHECK(panel->children().size() == objectCount);
    QPointer<TestPanel> guard(panel);
    const auto rootId = QAccessible::uniqueId(root);
    delete panel;
    CHECK(!guard);
    CHECK(QAccessible::accessibleInterface(rootId) == nullptr);
    CHECK(QAccessible::accessibleInterface(shiftId) == nullptr);
    // Fresh windows (e.g. a worker reload) receive a fresh tree.
    TestPanel replacement;
    auto *fresh = QAccessible::queryAccessibleInterface(&replacement);
    CHECK(fresh && fresh->childCount() == 0);
    notificationTests();
    std::puts("Native accessibility: roles, rects, lazy children, actions, freshness, reorder, teardown passed");
}
