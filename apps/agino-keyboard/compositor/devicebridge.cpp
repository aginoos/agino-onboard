// SPDX-License-Identifier: GPL-3.0-or-later
// Built against the exact Deepin KWin source; exports device kinds, never key contents.
#include <plugin.h>
#include <input.h>
#include <input_event.h>
#include <input_event_spy.h>
#include <QDBusConnection>
namespace {
class DeviceBridge final : public KWin::Plugin, public KWin::InputEventSpy {
 Q_OBJECT
 Q_CLASSINFO("D-Bus Interface", "org.agino.KeyboardDevice1")
public:
 DeviceBridge() { auto bus=QDBusConnection::sessionBus();owned_=bus.registerService("org.agino.KeyboardDevice1");if(owned_){bus.registerObject("/org/agino/KeyboardDevice1",this,QDBusConnection::ExportAllSignals);KWin::input()->installInputEventSpy(this);} }
 ~DeviceBridge() override {if(owned_){auto bus=QDBusConnection::sessionBus();bus.unregisterObject("/org/agino/KeyboardDevice1");bus.unregisterService("org.agino.KeyboardDevice1");}}
 void pointerEvent(KWin::MouseEvent *e) override {if(e->type()==QEvent::MouseButtonPress&&e->source()==Qt::MouseEventNotSynthesized)emit SourceChanged(1,e->globalPosition().x(),e->globalPosition().y());}
 void keyEvent(KWin::KeyEvent *e) override {if(e->type()==QEvent::KeyPress&&!e->isAutoRepeat())emit SourceChanged(4,0,0);}
 void touchDown(qint32,const QPointF &p,std::chrono::microseconds) override {emit SourceChanged(2,p.x(),p.y());}
 void tabletToolEvent(KWin::TabletEvent *e) override {if(e->type()==QEvent::TabletPress)emit SourceChanged(3,e->globalPosition().x(),e->globalPosition().y());}
signals: void SourceChanged(uint kind,double x,double y);
private:bool owned_=false;
};
class DeviceBridgeFactory final : public KWin::PluginFactory {
 Q_OBJECT
 Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "metadata.json")
 Q_INTERFACES(KWin::PluginFactory)
public:std::unique_ptr<KWin::Plugin> create()const override{return std::make_unique<DeviceBridge>();}
};
}
#include "devicebridge.moc"
