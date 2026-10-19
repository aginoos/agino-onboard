// SPDX-License-Identifier: GPL-3.0-or-later
#include "keysound.h"
#include <canberra.h>
#include <atomic>
#include <QLibrary>
class SoundWorker:public QObject {
public:
 ~SoundWorker()override{if(context_&&destroy_)destroy_(context_);}
 void cancel(){canceled_=true;}
 bool play(){
  if(canceled_)return false;
  if(!attempted_){
   attempted_=true;
   if(!library_.load())return false;
   auto create=reinterpret_cast<Create>(library_.resolve("ca_context_create"));destroy_=reinterpret_cast<Destroy>(library_.resolve("ca_context_destroy"));auto props=reinterpret_cast<Props>(library_.resolve("ca_context_change_props"));play_=reinterpret_cast<Play>(library_.resolve("ca_context_play"));
   if(!create||!destroy_||!props||!play_||create(&context_)!=CA_SUCCESS)return false;
   props(context_,CA_PROP_APPLICATION_NAME,"Agino Keyboard",nullptr);
  }
  // Canberra opens/plays asynchronously on this worker. Neither connection
  // establishment nor sound decoding blocks key delivery on the GUI thread.
  return !canceled_&&context_&&play_&&play_(context_,1,CA_PROP_MEDIA_FILENAME,AGINO_KEY_SOUND_PATH,
   CA_PROP_MEDIA_NAME,"Keyboard click",CA_PROP_CANBERRA_VOLUME,"-12",nullptr)==CA_SUCCESS;
 }
private:
 using Create=int(*)(ca_context **);using Destroy=int(*)(ca_context *);using Props=int(*)(ca_context *,...);using Play=int(*)(ca_context *,uint32_t,...);
 QLibrary library_{QStringLiteral("canberra"),0};Destroy destroy_=nullptr;Play play_=nullptr;
 ca_context *context_=nullptr;
 bool attempted_=false;
 std::atomic_bool canceled_{false};
};
KeySound::~KeySound(){
 // Joining is reserved for process shutdown. Setting changes never join.
 for(auto *thread:threads_){disconnect(thread,nullptr,this,nullptr);thread->quit();if(!thread->wait(1000)){thread->terminate();thread->wait(500);}delete thread;}
}
void KeySound::setEnabled(bool enabled){
 if(enabled==active())return;
 ++generation_;
 if(!enabled){
  auto *worker=static_cast<SoundWorker *>(worker_);disconnect(this,&KeySound::requested,worker,nullptr);worker->cancel();worker->thread()->quit();worker_=nullptr;return;
 }
 auto *thread=new QThread;threads_.append(thread);
 auto *worker=new SoundWorker;worker_=worker;worker->moveToThread(thread);
 connect(thread,&QThread::finished,worker,&QObject::deleteLater);
 connect(thread,&QThread::finished,this,[this,thread]{threads_.removeAll(thread);thread->deleteLater();});
 const auto serial=generation_;
 connect(this,&KeySound::requested,worker,[this,worker,serial]{const bool success=worker->play();QMetaObject::invokeMethod(this,[this,success,serial]{if(serial==generation_)emit delivered(success);},Qt::QueuedConnection);},Qt::QueuedConnection);
 thread->start();
}
void KeySound::play(){if(active())emit requested();}
