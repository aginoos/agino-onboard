// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

#ifndef AGINO_TRANSLATIONS_DIR
#define AGINO_TRANSLATIONS_DIR "/usr/share/agino-keyboard/translations"
#endif

namespace AginoI18n {
// Installs the catalogue of the user's UI language (agino-keyboard_<lang>.qm). The source strings are English,
// so a language without a catalogue shows the source text.
inline void install(QCoreApplication &app){
 auto *translator=new QTranslator(&app);
 if(translator->load(QLocale(),QStringLiteral("agino-keyboard"),QStringLiteral("_"),QStringLiteral(AGINO_TRANSLATIONS_DIR)))app.installTranslator(translator);
 else delete translator;
}
}
