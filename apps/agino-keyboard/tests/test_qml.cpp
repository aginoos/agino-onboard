#include <QtQuickTest/quicktest.h>
#include <QQmlEngine>
#include <DGuiApplicationHelper>
#include "candidaterows.h"
#include "keyboardiconprovider.h"
#include "keyboardstate.h"
#include "preferences.h"
#include <QTemporaryDir>
#include <QSettings>
class Setup:public QObject{Q_OBJECT
private:QTemporaryDir preferencesDirectory;
public slots:void qmlEngineAvailable(QQmlEngine *engine){engine->addImageProvider("keyboard-icon",new KeyboardIconProvider);}
void applicationAvailable(){QCoreApplication::setOrganizationName("agino-test");QCoreApplication::setApplicationName("keyboard-qml");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,preferencesDirectory.path());qmlRegisterType<Preferences>("org.agino.keyboard",1,0,"Preferences");if(qEnvironmentVariableIsSet("AGINO_TEST_DARK"))Dtk::Gui::DGuiApplicationHelper::instance()->setPaletteType(Dtk::Gui::DGuiApplicationHelper::DarkType);qRegisterMetaType<KeySpec>();qmlRegisterType<KeyboardState>("org.agino.keyboard",1,0,"KeyboardState");qmlRegisterType<CandidateRows>("org.agino.keyboard",1,0,"CandidateRows");}};
QUICK_TEST_MAIN_WITH_SETUP(keyboard,Setup)
#include "test_qml.moc"
