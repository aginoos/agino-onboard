#pragma once
#include <QObject>
#include <QHash>
struct KeySpec { quint32 sym=0, code=0, modifiers=0; bool operator==(const KeySpec &k)const{return sym==k.sym&&code==k.code&&modifiers==k.modifiers;} };
Q_DECLARE_METATYPE(KeySpec)
class KeyboardState:public QObject {
 Q_OBJECT
 Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
 Q_PROPERTY(bool inputAllowed READ inputAllowed NOTIFY inputAllowedChanged)
 Q_PROPERTY(quint32 modifiers READ modifiers NOTIFY modifiersChanged)
 Q_PROPERTY(bool settingsOpen READ settingsOpen WRITE setSettingsOpen NOTIFY settingsOpenChanged)
public:
 enum Mode { Automatic,Manual,Off }; Q_ENUM(Mode)
 enum InputSource { Unknown,Mouse,Touch,Pen,PhysicalKeyboard,VirtualKeyboard }; Q_ENUM(InputSource)
 explicit KeyboardState(QObject *p=nullptr):QObject(p){}
 void setCapsLock(bool v){caps_=v;updateModifiers();}
 bool visible()const{return visible_;} quint32 modifiers()const{return modifiers_;}
 bool inputAllowed()const{return layoutFits_&&editable_&&mode_!=Off&&!settingsOpen_;}
 bool settingsOpen()const{return settingsOpen_;}
 Q_INVOKABLE void setSettingsOpen(bool);
 void beginTouch(int,KeySpec); void endTouch(int,bool); void cancelAll();
 Q_INVOKABLE void setContext(quint64,bool,bool); void setMode(Mode); void setSource(InputSource);
 void advanceClock(qint64); void manualShow(); void dismiss();
 void setLayoutFits(bool fits){if(layoutFits_==fits)return;layoutFits_=fits;emit inputAllowedChanged();if(!fits)dismiss();}
 bool layoutFits()const{return layoutFits_;}
 Q_INVOKABLE void press(int id,quint32 sym){beginTouch(id,KeySpec{sym});}
 Q_INVOKABLE void release(int id,bool inside){endTouch(id,inside);}
 Q_INVOKABLE void cancel(){cancelAll();}
 Q_INVOKABLE void closePanel(){dismiss();}
signals:
 void keyRequested(KeySpec,bool); void visibleChanged(bool); void inputAllowedChanged(); void modifiersChanged(); void settingsOpenChanged();
private:
 struct TouchKey {KeySpec key; bool sent=false; qint64 nextRepeat=0; qint64 started=0; bool shiftTap=false; bool repeated=false;};
 void show(bool); void updateModifiers(); KeySpec resolved(KeySpec)const;
 QHash<int,TouchKey> touches_;
 Mode mode_=Automatic; InputSource source_=Unknown; quint64 epoch_=0;
 bool editable_=false,sensitive_=false,suppressed_=false,manualVisible_=false,caps_=false,shiftLatched_=false; qint64 clock_=0,lastShiftTap_=-1;
 bool visible_=false,layoutFits_=true,settingsOpen_=false; quint32 modifiers_=0;
};
