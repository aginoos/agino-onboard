// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QLoggingCategory>
// Logging categories of agino-keyboard. Use them as qCInfo(lcFcitx()) << ...; qCWarning / qCCritical likewise.
// Info and above print by default (journal / stderr); qCDebug only when switched on, for example
//   QT_LOGGING_RULES="agino.keyboard.*.debug=true"   (all categories)   or   "agino.keyboard.fcitx.debug=true"   (one)
// Log events and errors, once per change: never per key / frame / snapshot, and never what the user types
// (text, key symbols or codes, preedit, candidates, passwords), window titles or file contents.
// Bus names, pids, program paths, error names and state changes are fine.
// Header only: each function holds one function-local static category, shared by all translation units (inline).
#define AGINO_LOG_CATEGORY(function,name) inline const QLoggingCategory &function(){static const QLoggingCategory category(name,QtInfoMsg);return category;}
AGINO_LOG_CATEGORY(lcController,"agino.keyboard.controller")   // the controlling process (broker, native-main)
AGINO_LOG_CATEGORY(lcWindow,"agino.keyboard.window")           // the keyboard window, panel and its placement
AGINO_LOG_CATEGORY(lcFcitx,"agino.keyboard.fcitx")             // the client side of the Fcitx5 bridge
AGINO_LOG_CATEGORY(lcSession,"agino.keyboard.session")         // lock state and the platform (idleplatform, desktopadapter)
AGINO_LOG_CATEGORY(lcEmbed,"agino.keyboard.embed")             // agino-keyboard-embed (login and lock screens)
