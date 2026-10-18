// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativesettings.h"
#include "../preferences.h"
#include "../fcitxclient.h"
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#define CHECK(value) do { if(!(value)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#value);std::abort();} } while(false)
int main(int argc,char **argv)
{
 QGuiApplication app(argc,argv);QTemporaryDir temporary;
 QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,temporary.path());
 app.setOrganizationName("native-settings-test");app.setApplicationName("snapshot");
 Preferences prefs;FcitxClient input;NativeSettings settings(prefs,input);
 NativeStyle style;style.font=app.font();style.palette=app.palette();
 QImage image(934,344,QImage::Format_ARGB32_Premultiplied);
 QVector<NativeControl> controls;
 auto paint=[&]{QPainter painter(&image);controls=settings.paint(painter,image.size(),style,{"Screen A","Screen B"});};
 auto get=[&](const QString &id){for(const auto &c:controls)if(c.id=="settings/"+id)return c;std::abort();};
 paint();
 // Snapshot the painted controls, invoke a real action, then read state BEFORE
 // another paint or event-loop turn. Repeated actions must read current prefs.
 for(const auto &id:QStringList{"safeSetting","soundSetting","functionSetting"}){
  const auto original=get(id);auto live=original;
  CHECK(settings.activate(original));CHECK(settings.currentControl(live));
  CHECK(live.checked!=original.checked);CHECK(live.value.toBool()==live.checked);
  CHECK(settings.activate(original));CHECK(settings.currentControl(live));CHECK(live.checked==original.checked);
 }
 for(const auto &prefix:QStringList{"modeSetting/","themeSetting/","layoutSetting/"}){
  const auto first=get(prefix+"0"),second=get(prefix+"1");
  CHECK(settings.activate(second));auto a=first,b=second;
  CHECK(settings.currentControl(a));CHECK(settings.currentControl(b));CHECK(!a.checked&&b.checked);
  CHECK(settings.activate(first));CHECK(settings.currentControl(a));CHECK(settings.currentControl(b));CHECK(a.checked&&!b.checked);
 }
 auto slider=get("opacitySetting");CHECK(settings.drag(slider,{slider.rect.left()+12,slider.rect.center().y()}));
 CHECK(settings.currentControl(slider));CHECK(slider.value.toMap()["current"].toInt()==50);
 const auto stale=get("safeSetting");CHECK(settings.activate(get("screenSetting")));auto expired=stale;CHECK(!settings.currentControl(expired));
 paint();auto selected=get("choice/screen:0:Screen A"),other=get("choice/screen:1:Screen B");
 prefs.setScreenIndex(1);CHECK(settings.currentControl(selected));CHECK(settings.currentControl(other));CHECK(!selected.checked&&other.checked);
 settings.updateScreens({"Renamed","Screen B"});CHECK(!settings.currentControl(other));
 std::puts("Native settings immediate checkbox/radio/slider readback and stale leases passed");
}
