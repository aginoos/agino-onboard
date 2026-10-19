#pragma once
#include "keyboardstate.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QSet>
#include <QTimer>
class FcitxClient:public QObject {
 Q_OBJECT
 Q_PROPERTY(bool connected READ connected NOTIFY changed)
 Q_PROPERTY(bool editable READ editable NOTIFY changed)
 Q_PROPERTY(bool sensitive READ sensitive NOTIFY changed)
 Q_PROPERTY(quint64 contextEpoch READ contextEpoch NOTIFY changed)
 Q_PROPERTY(quint64 candidateEpoch READ candidateEpoch NOTIFY changed)
 Q_PROPERTY(QString preedit READ preedit NOTIFY changed)
 Q_PROPERTY(QStringList candidates READ candidates NOTIFY changed)
 Q_PROPERTY(QString inputMethod READ inputMethod NOTIFY changed)
 Q_PROPERTY(QStringList inputMethods READ inputMethods NOTIFY changed)
 Q_PROPERTY(QVariantMap inputMethodNames READ inputMethodNames NOTIFY changed)
 Q_PROPERTY(QString inputMethodStatus READ inputMethodStatus NOTIFY changed)
 Q_PROPERTY(QString inputLanguageCode READ inputLanguageCode NOTIFY changed)
 Q_PROPERTY(bool paging READ paging NOTIFY changed)
 Q_PROPERTY(bool hasPrev READ hasPrev NOTIFY changed)
 Q_PROPERTY(bool hasNext READ hasNext NOTIFY changed)
 Q_PROPERTY(QString status READ status NOTIFY changed)
public:
 // Standalone: no input method (the keyboard of the login and lock screens, agino-keyboard-embed). The client is
 // always connected and editable, never touches the session bus and has no input method name to show.
 explicit FcitxClient(QObject *p=nullptr,bool standalone=false);
 bool connected()const{return standalone_||connected_;} bool editable()const{return standalone_||(editable_&&!externalPolicyPending_&&!policyPending()&&!(suspendConfigured_&&desiredSuspend_));}bool sensitive()const{return sensitive_||(safeConfigured_&&desiredSafe_);}
 quint64 contextEpoch()const{return context_;}quint64 candidateEpoch()const{return candidate_;}
 QString preedit()const{return preedit_;}QStringList candidates()const{return candidates_;}QString inputMethod()const{return im_;}QStringList inputMethods()const{return ims_;}bool hasNext()const{return next_;}bool hasPrev()const{return prev_;}bool paging()const{return paging_;}QString status()const{return status_;}
 QVariantMap inputMethodNames()const{return imNames_;}
 QString inputMethodStatus()const{if(standalone_)return QString();if(sensitive())return tr("Secure input");const auto text=imStatuses_.value(im_).toString();return !text.isEmpty()?text:im_.isEmpty()?status_:namesRetries_>=3&&!namesInFlight_&&!namesRetryTimer_.isActive()?tr("Input method status unavailable"):tr("Reading the input method status");}
 QString inputLanguageCode()const{return imLanguages_.value(im_).toString();}
 void refreshInputMethodNames();
 bool sendKey(KeySpec,bool,quint64);
 void refreshSnapshot();
 Q_INVOKABLE void selectCandidate(int,quint64,quint64);
 Q_INVOKABLE void requestPage(bool);
 Q_INVOKABLE void setInputMethod(const QString &);
 Q_INVOKABLE void setSafeInput(bool);
 void refresh();void suspend(bool);void setPanelVisible(bool);
 // The physical key signal has no consumer in the programs; only the tests listen to it.
 void subscribePhysicalKey();
 bool policyReady()const{return !policyPending();}
 void setPolicyBlocked(bool value){if(externalPolicyPending_==value)return;externalPolicyPending_=value;if(value){preedit_.clear();candidates_.clear();next_=prev_=false;}emit changed();}
signals:
 void pageFinished(bool accepted,quint64 context,quint64 candidate);
 void changed(); void physicalKey(); void candidateSelected(); void targetRect(int,int);
private slots:
 void snapshot(const QDBusMessage &); void ownerChanged(const QString &,const QString &,const QString &);
private:
 void call(const QString &,const QVariantList &args={});void clear();
 void synchronizePolicy();
 void retryInputMethodNames();
 void requestInputMethodNames();
 void controllerOwnerChanged(const QString &,const QString &,const QString &);
 // SEC-10: whether the unique bus name runs Fcitx5; one verdict per name (one round trip to the bus daemon), dropped when an owner changes.
 bool trustedPeer(const QString &);
 // Takes a snapshot message that is known to come from the verified connection `from`.
 void applySnapshot(const QDBusMessage &,const QString &from);
 // Where the calls to the bridge go: the verified connection once known, so a program that claims the name later gets nothing.
 QString bridge()const{return bridge_.isEmpty()?QString::fromLatin1("org.agino.Keyboard1"):bridge_;}
 QHash<QString,bool> verdicts_;QSet<QString> warned_;QString bridge_;
 bool policyPending()const{return policyRevision_!=appliedPolicyRevision_||policyRefreshPending_;}
 bool externalPolicyPending_=false;
 bool panelConfigured_=false,desiredPanelVisible_=false;
 bool safeConfigured_=false,suspendConfigured_=false,desiredSafe_=false,desiredSuspend_=false,policyInFlight_=false,policyRefreshPending_=false;
 quint64 policyRevision_=0,appliedPolicyRevision_=0;
 bool standalone_=false,cleared_=false,connected_=false,editable_=false,sensitive_=false,next_=false,prev_=false,paging_=false,namesInFlight_=false;quint64 context_=0,candidate_=0;QString preedit_,im_,status_;QStringList candidates_,ims_;QVariantMap imNames_,imStatuses_,imLanguages_; quint64 generation_=0,pageRevision_=0;
 QTimer namesRetryTimer_,reconnectTimer_;int namesRetries_=0;quint64 namesRevision_=0;QString controllerOwner_;
};
