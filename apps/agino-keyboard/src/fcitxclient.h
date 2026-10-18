#pragma once
#include "keyboardstate.h"
#include <QDBusConnection>
#include <QDBusMessage>
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
 explicit FcitxClient(QObject *p=nullptr);
 bool connected()const{return connected_;} bool editable()const{return editable_&&!externalPolicyPending_&&!policyPending()&&!(suspendConfigured_&&desiredSuspend_);}bool sensitive()const{return sensitive_||(safeConfigured_&&desiredSafe_);}
 quint64 contextEpoch()const{return context_;}quint64 candidateEpoch()const{return candidate_;}
 QString preedit()const{return preedit_;}QStringList candidates()const{return candidates_;}QString inputMethod()const{return im_;}QStringList inputMethods()const{return ims_;}bool hasNext()const{return next_;}bool hasPrev()const{return prev_;}bool paging()const{return paging_;}QString status()const{return status_;}
 QVariantMap inputMethodNames()const{return imNames_;}
 QString inputMethodStatus()const{if(sensitive())return tr("安全输入");const auto text=imStatuses_.value(im_).toString();return !text.isEmpty()?text:im_.isEmpty()?status_:namesRetries_>=3&&!namesInFlight_&&!namesRetryTimer_.isActive()?tr("输入法状态暂不可用"):tr("正在读取输入法状态");}
 QString inputLanguageCode()const{return imLanguages_.value(im_).toString();}
 void refreshInputMethodNames();
 bool sendKey(KeySpec,bool,quint64);
 void refreshSnapshot();
 Q_INVOKABLE void selectCandidate(int,quint64,quint64);
 Q_INVOKABLE void requestPage(bool);
 Q_INVOKABLE void setInputMethod(const QString &);
 Q_INVOKABLE void setSafeInput(bool);
 void refresh();void suspend(bool);void setPanelVisible(bool);
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
 bool policyPending()const{return policyRevision_!=appliedPolicyRevision_||policyRefreshPending_;}
 bool externalPolicyPending_=false;
 bool panelConfigured_=false,desiredPanelVisible_=false;
 bool safeConfigured_=false,suspendConfigured_=false,desiredSafe_=false,desiredSuspend_=false,policyInFlight_=false,policyRefreshPending_=false;
 quint64 policyRevision_=0,appliedPolicyRevision_=0;
 bool connected_=false,editable_=false,sensitive_=false,next_=false,prev_=false,paging_=false,namesInFlight_=false;quint64 context_=0,candidate_=0;QString preedit_,im_,status_;QStringList candidates_,ims_;QVariantMap imNames_,imStatuses_,imLanguages_; quint64 generation_=0,pageRevision_=0;
 QTimer namesRetryTimer_,reconnectTimer_;int namesRetries_=0;quint64 namesRevision_=0;QString controllerOwner_;
};
