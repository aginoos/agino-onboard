#include <QtTest>
#include "keysound.h"
class KeySoundTest:public QObject {
 Q_OBJECT
private slots:
 void disabledDoesNotInitializeAudio(){KeySound sound;QSignalSpy delivered(&sound,&KeySound::delivered);sound.play();QTest::qWait(40);QCOMPARE(delivered.count(),0);QVERIFY(!sound.active());QFile maps("/proc/self/maps");QVERIFY(maps.open(QIODevice::ReadOnly));QVERIFY(!maps.readAll().contains("libcanberra.so"));}
 void disablingDuringUnresponsiveAudioDoesNotBlock(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_AUDIO_STALL"))QSKIP("Requires disposable stalled audio socket");
  KeySound sound;sound.setEnabled(true);sound.play();QTest::qWait(150);QElapsedTimer elapsed;elapsed.start();sound.setEnabled(false);QVERIFY2(elapsed.elapsed()<100,"Disabling sound blocked GUI waiting for audio server");QVERIFY(!sound.active());
 }
 void enabledPlaysBundledClick(){
  if(!qEnvironmentVariableIsSet("AGINO_TEST_AUDIO"))QSKIP("Run against a disposable audio server");
  KeySound sound;QSignalSpy delivered(&sound,&KeySound::delivered);sound.setEnabled(true);sound.play();QTRY_COMPARE(delivered.count(),1);QVERIFY(delivered.first().first().toBool());QVERIFY(sound.active());QTest::qWait(2300);
  sound.setEnabled(false);QVERIFY(!sound.active());delivered.clear();sound.play();QTest::qWait(40);QCOMPARE(delivered.count(),0);
  sound.setEnabled(true);sound.play();QTRY_COMPARE(delivered.count(),1);QVERIFY(delivered.first().first().toBool());QTest::qWait(2300);
 }
};
QTEST_GUILESS_MAIN(KeySoundTest)
#include "test_keysound.moc"
