// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QGuiApplication>
#include <utility>

class StartupDpiPolicy {
public:
 StartupDpiPolicy():original_(qgetenv("QT_SCALE_FACTOR_ROUNDING_POLICY")){
  using Policy=Qt::HighDpiScaleFactorRoundingPolicy;
  const std::pair<const char *,Policy> policies[]={
   {"Round",Policy::Round},{"Ceil",Policy::Ceil},{"Floor",Policy::Floor},
   {"RoundPreferFloor",Policy::RoundPreferFloor},{"PassThrough",Policy::PassThrough}
  };
  for(const auto &[name,policy]:policies){
   if(original_.compare(name,Qt::CaseInsensitive)!=0)continue;
   // Qt 6.8 reads this environment variable during application construction
   // and calls its public setter too late. Apply it before that constructor;
   // restore the environment afterwards for settings/other child processes.
   QGuiApplication::setHighDpiScaleFactorRoundingPolicy(policy);
   qunsetenv("QT_SCALE_FACTOR_ROUNDING_POLICY");restore_=true;break;
  }
 }
 void restore(){if(restore_){qputenv("QT_SCALE_FACTOR_ROUNDING_POLICY",original_);restore_=false;}}
 ~StartupDpiPolicy(){restore();}
private:
 QByteArray original_;bool restore_=false;
};
