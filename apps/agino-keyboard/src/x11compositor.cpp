// SPDX-License-Identifier: GPL-3.0-or-later
#include "x11compositor.h"
#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xfixes.h>
#include <cstdlib>
#include <X11/Xlib.h>
bool x11CompositeAvailable(){
 auto *native=qGuiApp->nativeInterface<QNativeInterface::QX11Application>();if(!native)return false;
 auto *display=native->display();const QByteArray selection="_NET_WM_CM_S"+QByteArray::number(DefaultScreen(display));
 return XGetSelectionOwner(display,XInternAtom(display,selection.constData(),False))!=0;
}
X11CompositorWatcher::X11CompositorWatcher(QObject *parent):QObject(parent){
 auto *native=qGuiApp->nativeInterface<QNativeInterface::QX11Application>();if(!native)return;
 auto *connection=native->connection();const auto *extension=xcb_get_extension_data(connection,&xcb_xfixes_id);if(!extension||!extension->present)return;
 auto *version=xcb_xfixes_query_version_reply(connection,xcb_xfixes_query_version(connection,5,0),nullptr);if(!version)return;const bool supported=version->major_version>=2;std::free(version);if(!supported)return;
 auto *display=native->display();const QByteArray name="_NET_WM_CM_S"+QByteArray::number(DefaultScreen(display));selection_=XInternAtom(display,name.constData(),False);eventType_=extension->first_event+XCB_XFIXES_SELECTION_NOTIFY;
 xcb_xfixes_select_selection_input(connection,DefaultRootWindow(display),selection_,XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER|XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY|XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE);xcb_flush(connection);
 qGuiApp->installNativeEventFilter(this);
}
X11CompositorWatcher::~X11CompositorWatcher(){if(eventType_>=0)qGuiApp->removeNativeEventFilter(this);}
bool X11CompositorWatcher::nativeEventFilter(const QByteArray &type,void *message,qintptr *){
 if(type!="xcb_generic_event_t"||eventType_<0)return false;
 auto *event=static_cast<xcb_generic_event_t *>(message);
 if((event->response_type&0x7f)==eventType_&&reinterpret_cast<xcb_xfixes_selection_notify_event_t *>(event)->selection==selection_)emit changed();
 return false;
}
