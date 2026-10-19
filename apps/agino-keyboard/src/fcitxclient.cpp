#include "fcitxclient.h"
#include <QDBusServiceWatcher>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QTimer>
#include <QLocale>
#include <memory>
#include "peercheck.h"
#include "log.h"
static constexpr auto service="org.agino.Keyboard1";
static constexpr auto path="/org/agino/Keyboard1";
// Logging: lossReason says why clear() is called (the callers set it, clear() logs it when the connection was up);
// seenFailures keeps a bridge that stays unreachable (retried with a back-off) from logging the same error again and again (emptied when it connects);
// namesFailure says why the input method names could not be read.
static QString lossReason,namesFailure;static QSet<QString> seenFailures;
static void callFailed(const QString &method,const QString &error){lossReason=QStringLiteral("call %1 failed: %2").arg(method,error);if(seenFailures.contains(lossReason))return;seenFailures.insert(lossReason);qCWarning(lcFcitx()).noquote()<<"bridge call"<<method<<"failed:"<<error;}
// Debian's Fcitx5 is in /usr/bin whatever the install prefix of this package is.
static const QStringList fcitxPrograms{QStringLiteral("/usr/bin/fcitx5")};
bool FcitxClient::trustedPeer(const QString &sender){
 const auto known=verdicts_.constFind(sender);if(known!=verdicts_.cend())return known.value();
 const int verdict=PeerCheck::check(QDBusConnection::sessionBus(),sender,fcitxPrograms);
 if(verdict<0)return false;   // the program could not be found: not remembered, asked again next time
 const bool trusted=verdict>0;verdicts_.insert(sender,trusted);
 if(!trusted&&!warned_.contains(sender)){warned_.insert(sender);qCWarning(lcFcitx(),"Ignoring the bus connection %s: it is not Fcitx5 (/usr/bin/fcitx5)",qPrintable(sender));}
 return trusted;
}
FcitxClient::FcitxClient(QObject *p,bool standalone):QObject(p),standalone_(standalone){
 if(standalone_)return;
 namesRetryTimer_.setSingleShot(true);
 connect(&namesRetryTimer_,&QTimer::timeout,this,&FcitxClient::requestInputMethodNames);
 reconnectTimer_.setInterval(2000);connect(&reconnectTimer_,&QTimer::timeout,this,[this]{if(!connected_){refresh();reconnectTimer_.setInterval(qMin(reconnectTimer_.interval()*2,60000));}});reconnectTimer_.start();
 auto bus=QDBusConnection::sessionBus();
 if(bus.interface())controllerOwner_=bus.interface()->serviceOwner("org.fcitx.Fcitx5").value();
 auto controllerWatcher=new QDBusServiceWatcher("org.fcitx.Fcitx5",bus,QDBusServiceWatcher::WatchForOwnerChange,this);
 connect(controllerWatcher,&QDBusServiceWatcher::serviceOwnerChanged,this,&FcitxClient::controllerOwnerChanged);
 auto watcher=new QDBusServiceWatcher(service,bus,QDBusServiceWatcher::WatchForOwnerChange,this);
 connect(watcher,&QDBusServiceWatcher::serviceOwnerChanged,this,&FcitxClient::ownerChanged);
 bus.connect(service,path,service,"SnapshotChanged",this,SLOT(snapshot(QDBusMessage)));
 QTimer::singleShot(0,this,&FcitxClient::refresh);
}
void FcitxClient::subscribePhysicalKey(){if(!standalone_)QDBusConnection::sessionBus().connect(service,path,service,"PhysicalKey",this,SIGNAL(physicalKey()));}
void FcitxClient::clear(){
 if(connected_)qCInfo(lcFcitx()).noquote()<<"bridge lost:"<<(lossReason.isEmpty()?QStringLiteral("unknown reason"):lossReason)<<"(peer"<<bridge_<<")";
 lossReason.clear();
 const bool wasCleared=cleared_;cleared_=true;reconnectTimer_.start();++pageRevision_;++generation_;++namesRevision_;appliedPolicyRevision_=0;policyInFlight_=false;policyRefreshPending_=false;namesInFlight_=false;namesRetryTimer_.stop();namesRetries_=0;connected_=editable_=sensitive_=next_=prev_=paging_=false;bridge_.clear();context_=candidate_=0;preedit_.clear();candidates_.clear();im_.clear();ims_.clear();imNames_.clear();imStatuses_.clear();imLanguages_.clear();status_=tr("Waiting for the Fcitx5 screen keyboard plugin");if(!wasCleared)emit changed();}
void FcitxClient::ownerChanged(const QString &,const QString &,const QString &owner){lossReason=owner.isEmpty()?QStringLiteral("owner gone"):QStringLiteral("owner changed to %1").arg(owner);verdicts_.clear();++generation_;reconnectTimer_.setInterval(2000);clear();if(!owner.isEmpty())refresh();}
void FcitxClient::refresh(){
 if(standalone_)return;
 const auto gen=generation_,policy=appliedPolicyRevision_;auto bus=QDBusConnection::sessionBus();
 // The owner of the bridge name is asked who it is before it is called; the calls go to its unique name, so the replies are its own
 // (the sender of a reply is not set in the message QtDBus hands over).
 const QString owner=bus.interface()?bus.interface()->serviceOwner(service).value():QString();
 if(!owner.isEmpty()&&!trustedPeer(owner)){lossReason=QStringLiteral("untrusted peer %1").arg(owner);clear();return;}
 const QString target=owner.isEmpty()?QString::fromLatin1(service):owner;
 auto m=QDBusMessage::createMethodCall(target,path,service,"GetSnapshot");m.setAutoStartService(false);
 auto w=new QDBusPendingCallWatcher(bus.asyncCall(m,1500),this);
 connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,gen,policy,owner]{auto reply=w->reply();w->deleteLater();if(gen!=generation_)return;if(reply.type()==QDBusMessage::ErrorMessage){callFailed(QStringLiteral("GetSnapshot"),reply.errorName());clear();return;}if(policy==policyRevision_&&policy==appliedPolicyRevision_)policyRefreshPending_=false;applySnapshot(reply,owner);});
 auto ims=QDBusMessage::createMethodCall(target,path,service,"InputMethods");ims.setAutoStartService(false);
 auto iw=new QDBusPendingCallWatcher(bus.asyncCall(ims,1500),this);
 connect(iw,&QDBusPendingCallWatcher::finished,this,[this,iw,gen]{QDBusPendingReply<QStringList> r=*iw;iw->deleteLater();if(gen==generation_&&r.isError())callFailed(QStringLiteral("InputMethods"),r.error().name());if(gen==generation_&&!r.isError()){bool needsNames=ims_!=r.value()||imNames_.isEmpty();if(ims_!=r.value()){++namesRevision_;namesInFlight_=false;namesRetries_=0;namesRetryTimer_.stop();imNames_.clear();imStatuses_.clear();imLanguages_.clear();}ims_=r.value();emit changed();if(needsNames)requestInputMethodNames();}});
}
void FcitxClient::refreshSnapshot(){if(!connected_){refresh();return;}policyRefreshPending_=true;emit changed();refresh();}
void FcitxClient::controllerOwnerChanged(const QString &,const QString &,const QString &owner){verdicts_.clear();controllerOwner_=owner;++namesRevision_;namesRetryTimer_.stop();namesInFlight_=false;namesRetries_=0;imNames_.clear();imStatuses_.clear();imLanguages_.clear();emit changed();if(!owner.isEmpty())refreshInputMethodNames();}
void FcitxClient::refreshInputMethodNames(){++namesRevision_;namesRetries_=0;namesRetryTimer_.stop();namesInFlight_=false;imNames_.clear();imStatuses_.clear();imLanguages_.clear();emit changed();refresh();}
void FcitxClient::requestInputMethodNames(){
 if(namesInFlight_||ims_.isEmpty())return;
 namesRetryTimer_.stop();
 // Only the verified owner of the controller name gets the call, by its unique name.
 if(!trustedPeer(controllerOwner_)){namesFailure=controllerOwner_.isEmpty()?QStringLiteral("org.fcitx.Fcitx5 has no owner"):QStringLiteral("the owner of org.fcitx.Fcitx5 is not verified as Fcitx5");retryInputMethodNames();return;}
 namesInFlight_=true;
 const auto gen=generation_,revision=namesRevision_;const auto owner=controllerOwner_;auto m=QDBusMessage::createMethodCall(owner.isEmpty()?QStringLiteral("org.fcitx.Fcitx5"):owner,"/controller","org.fcitx.Fcitx.Controller1","AvailableInputMethods");m.setAutoStartService(false);
 auto w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,1500),this);
 connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,gen,revision,owner]{
  auto reply=w->reply();w->deleteLater();if(gen!=generation_||revision!=namesRevision_||owner!=controllerOwner_)return;namesInFlight_=false;
  if(reply.type()==QDBusMessage::ErrorMessage||reply.arguments().size()!=1||reply.arguments().first().metaType()!=QMetaType::fromType<QDBusArgument>()){namesFailure=reply.type()==QDBusMessage::ErrorMessage?reply.errorName():QStringLiteral("unexpected reply");retryInputMethodNames();return;}
  const auto entries=qvariant_cast<QDBusArgument>(reply.arguments().first());QVariantMap names,statuses,languages;
  if(entries.currentSignature()!=QStringLiteral("a(ssssssb)")){namesFailure=QStringLiteral("unexpected signature %1").arg(entries.currentSignature());retryInputMethodNames();return;}
  entries.beginArray();while(!entries.atEnd()){
   QString id,name,nativeName,icon,label,language;bool configurable=false;
   entries.beginStructure();entries>>id>>name>>nativeName>>icon>>label>>language>>configurable;entries.endStructure();
   if(ims_.contains(id)){
    const QString displayName=nativeName.isEmpty()?name:nativeName;
    names.insert(id,id=="keyboard-us"?tr("English (US)"):displayName);
    const QLocale locale(language);QString languageName;
    if(locale.language()==QLocale::Chinese)languageName=tr("Chinese");else if(locale.language()==QLocale::English)languageName=tr("English");else if(locale.language()!=QLocale::C)languageName=locale.nativeLanguageName();
    languages.insert(id,locale.language()==QLocale::Chinese?QStringLiteral("CN"):locale.language()==QLocale::C?QString():QLocale::languageToCode(locale.language()).toUpper());
    statuses.insert(id,languageName.isEmpty()?displayName:tr("%1: %2").arg(languageName,displayName));
   }
  }entries.endArray();imNames_=names;imStatuses_=statuses;imLanguages_=languages;namesRetries_=0;emit changed();
 });
}
void FcitxClient::retryInputMethodNames(){
 if(connected_&&!ims_.isEmpty()&&namesRetries_<3)namesRetryTimer_.start(500<<namesRetries_++);
 else if(connected_&&!ims_.isEmpty())qCWarning(lcFcitx()).noquote()<<"input method names unavailable after 3 retries:"<<namesFailure;
 emit changed();
}
// The signal SnapshotChanged: only from the verified bridge connection.
void FcitxClient::snapshot(const QDBusMessage &m){if(trustedPeer(m.service()))applySnapshot(m,m.service());}
void FcitxClient::applySnapshot(const QDBusMessage &m,const QString &from){
 auto a=m.arguments();if(a.size()!=12)return;
 auto context=a[0].toULongLong(),candidate=a[1].toULongLong();
 if(context<context_||(context==context_&&candidate<candidate_))return;
 if(context!=context_){++pageRevision_;paging_=false;}
 const bool wasConnected=connected_;
 bridge_=from;context_=context;candidate_=candidate;editable_=a[2].toBool();sensitive_=a[3].toBool();preedit_=a[4].toString();im_=a[5].toString();candidates_=qdbus_cast<QStringList>(a[6]);prev_=a[7].toBool();next_=a[8].toBool();connected_=true;cleared_=false;reconnectTimer_.stop();reconnectTimer_.setInterval(2000);
 if(!wasConnected){seenFailures.clear();qCInfo(lcFcitx())<<"bridge connected, peer"<<from;}
 synchronizePolicy();
 if(!editable()||sensitive()){preedit_.clear();candidates_.clear();next_=prev_=false;}
 status_=editable()?(sensitive()?tr("Secure input"):QString()):tr("Select a text field first");emit changed();emit targetRect(a[10].toInt(),a[11].toInt());
}
void FcitxClient::call(const QString &method,const QVariantList &args){if(!connected_)return;auto m=QDBusMessage::createMethodCall(bridge(),path,service,method);m.setArguments(args);m.setAutoStartService(false);auto w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,1500),this);auto gen=generation_;connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,gen,method]{auto reply=w->reply();w->deleteLater();if(gen!=generation_)return;if(reply.type()==QDBusMessage::ErrorMessage){callFailed(method,reply.errorName());clear();} });}
bool FcitxClient::sendKey(KeySpec k,bool release,quint64 epoch){if(!editable()||epoch!=context_)return false;call("SendKey",{k.sym,k.code,k.modifiers,release,QVariant::fromValue(epoch)});return true;}
void FcitxClient::selectCandidate(int index,quint64 epoch,quint64 candidate){
 if(!editable()||sensitive()||epoch!=context_||candidate!=candidate_||index<0||index>=candidates_.size())return;
 auto m=QDBusMessage::createMethodCall(bridge(),path,service,"SelectCandidate");m.setArguments({index,QVariant::fromValue(epoch),QVariant::fromValue(candidate)});m.setAutoStartService(false);
 auto w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,1500),this);auto gen=generation_;connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,gen]{QDBusPendingReply<bool> r=*w;w->deleteLater();if(gen!=generation_)return;if(r.isError()){callFailed(QStringLiteral("SelectCandidate"),r.error().name());clear();return;}if(r.value())emit candidateSelected();});
}
void FcitxClient::requestPage(bool next){
 if(!editable()||sensitive()||paging_||(next?!next_:!prev_))return;
 paging_=true;const auto revision=++pageRevision_;emit changed();
 auto m=QDBusMessage::createMethodCall(bridge(),path,service,"RequestPage");const auto context=context_,candidate=candidate_;
 m.setArguments({next,QVariant::fromValue(context),QVariant::fromValue(candidate)});m.setAutoStartService(false);
 auto w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,1500),this);auto gen=generation_;
 connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,gen,revision,context,candidate]{
  QDBusPendingReply<bool> reply=*w;w->deleteLater();if(gen!=generation_||revision!=pageRevision_)return;
  paging_=false;if(reply.isError()){callFailed(QStringLiteral("RequestPage"),reply.error().name());clear();return;}emit changed();
  if(context==context_)emit pageFinished(reply.value(),context,candidate);
 });
}
void FcitxClient::setInputMethod(const QString &name){if(!ims_.contains(name))return;call("SetInputMethod",{name,QVariant::fromValue(context_)});}
void FcitxClient::setSafeInput(bool v){
 if(safeConfigured_&&desiredSafe_==v)return;
 safeConfigured_=true;desiredSafe_=v;++policyRevision_;synchronizePolicy();emit changed();
}

void FcitxClient::suspend(bool v){
 if(suspendConfigured_&&desiredSuspend_==v)return;
 suspendConfigured_=true;desiredSuspend_=v;++policyRevision_;synchronizePolicy();
 if(v){preedit_.clear();candidates_.clear();}emit changed();
}
void FcitxClient::setPanelVisible(bool value){
 if(panelConfigured_&&desiredPanelVisible_==value)return;
 panelConfigured_=true;desiredPanelVisible_=value;++policyRevision_;synchronizePolicy();emit changed();
}
void FcitxClient::synchronizePolicy(){
 if(!connected_||policyRevision_==appliedPolicyRevision_||policyInFlight_)return;
 policyInFlight_=true;
 const auto gen=generation_,revision=policyRevision_;
 struct Batch {int remaining=0;bool failed=false;QString method,error;};
 auto batch=std::make_shared<Batch>();
 batch->remaining=int(safeConfigured_)+int(suspendConfigured_)+int(panelConfigured_);
 auto send=[this,gen,revision,batch](const QString &method,bool value){
  auto m=QDBusMessage::createMethodCall(bridge(),path,service,method);m.setArguments({value});m.setAutoStartService(false);
  auto w=new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(m,1500),this);
  connect(w,&QDBusPendingCallWatcher::finished,this,[this,w,gen,revision,batch,method]{
   const auto reply=w->reply();w->deleteLater();if(gen!=generation_)return;
   batch->failed|=reply.type()==QDBusMessage::ErrorMessage;
   if(reply.type()==QDBusMessage::ErrorMessage&&batch->method.isEmpty()){batch->method=method;batch->error=reply.errorName();}   // the first failure of the batch
   if(--batch->remaining)return;
   policyInFlight_=false;
   if(batch->failed){callFailed(batch->method,batch->error);clear();return;}
   appliedPolicyRevision_=revision;policyRefreshPending_=true;
   if(policyRevision_!=appliedPolicyRevision_)synchronizePolicy();
   // Read the authoritative post-policy epoch before accepting input.
   refresh();
  });
 };
 if(panelConfigured_)send("SetPanelVisible",desiredPanelVisible_);
 if(safeConfigured_)send("SetSafeInput",desiredSafe_);
 if(suspendConfigured_)send("Suspend",desiredSuspend_);
}
