// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativecandidaterows.h"
#include "../candidaterows.h"
#include <QtTest>
#include <QSignalSpy>
#include <QAbstractItemModelTester>
#include <cmath>

class NativeCandidateRowsTest final : public QObject {
 Q_OBJECT
private slots:
 void lazyUnshapedTail();
 void matchesOriginal_data();
 void matchesOriginal();
 void invalidation();
};

void NativeCandidateRowsTest::lazyUnshapedTail()
{
 NativeCandidateRows rows;
 QFont font;font.setPixelSize(18);
 rows.setFont(font);rows.setAvailableWidth(48);
 QStringList words;for(int i=0;i<1000;++i)words<<"ASCII";
 words<<QStringLiteral("漢語繁體候選詞")<<QString::fromUcs4(U"𠀀𠮷𪚥")<<QStringLiteral("العربية")<<QStringLiteral("😀👩🏽‍💻");
 rows.setCandidates(words);
 QCOMPARE(rows.rowCount(),0);QCOMPARE(rows.measuredCandidateCount(),0);QVERIFY(!rows.complete());
 QVERIFY(std::isfinite(rows.contentHeight()));QVERIFY(rows.contentHeight()>48);
 QCOMPARE(rows.measuredCandidateCount(),0); // Estimating must not shape the tail.
 QSignalSpy inserted(&rows,&QAbstractItemModel::rowsInserted);
 rows.ensureLayout(1);
 QCOMPARE(rows.rowCount(),2);QCOMPARE(rows.measuredCandidateCount(),2);QCOMPARE(inserted.size(),2);
 for(int r=0;r<rows.rowCount();++r){const auto entries=rows.data(rows.index(r),NativeCandidateRows::Entries).toList();QCOMPARE(entries.size(),1);QCOMPARE(entries[0].toMap()["index"].toInt(),r);QCOMPARE(entries[0].toMap()["text"].toString(),QString("ASCII"));}
 const auto measured=rows.measuredCandidateCount();rows.ensureLayout(1);QCOMPARE(rows.measuredCandidateCount(),measured);QCOMPARE(inserted.size(),2);
 QVERIFY(!rows.complete());
}

void NativeCandidateRowsTest::matchesOriginal_data()
{
 QTest::addColumn<QStringList>("words");QTest::addColumn<qreal>("width");QTest::addColumn<int>("pixels");
 QTest::newRow("empty")<<QStringList{}<<qreal(600)<<18;
 QTest::newRow("short")<<QStringList{"a","b","c","a","last"}<<qreal(600)<<18;
 QTest::newRow("fractional-width")<<QStringList{"one","two","three","four","five","six","seven"}<<qreal(233.125)<<18;
 QTest::newRow("minimum-width")<<QStringList{"first","second","third"}<<qreal(4)<<13;
 QTest::newRow("long-wrapped")<<QStringList{QString(350,'a'),"short",QString(500,'b'),"last"}<<qreal(175)<<18;
 QTest::newRow("mixed-script")<<QStringList{QStringLiteral("你好"),"hello world",QStringLiteral("漢語繁體候選詞"),QStringLiteral("العربية"),QStringLiteral("日本語かなカナ"),QStringLiteral("한글"),QString::fromUcs4(U"𠀀𠮷𪚥"),QStringLiteral("emoji 😀👩🏽‍💻"),QStringLiteral("e\u0301 combine"),QString(80,'z'),"","end"}<<qreal(247.75)<<18;
 QTest::newRow("spaces-newlines")<<QStringList{"a\nb\nc","many words with spaces that wrap at a boundary","\t\t","end"}<<qreal(149.5)<<13;
}

void NativeCandidateRowsTest::matchesOriginal()
{
 QFETCH(QStringList,words);QFETCH(qreal,width);QFETCH(int,pixels);
 QFont font;font.setPixelSize(pixels);
 CandidateRows original;original.setFont(font);original.setAvailableWidth(width);original.setCandidates(words);
 NativeCandidateRows lazy;lazy.setFont(font);lazy.setAvailableWidth(width);lazy.setCandidates(words);
 QAbstractItemModelTester modelTester(&lazy,QAbstractItemModelTester::FailureReportingMode::QtTest);
 QCOMPARE(lazy.measuredCandidateCount(),0);
 qreal expectedHeight=0;
 for(int r=0;r<original.rowCount();++r)expectedHeight+=original.data(original.index(r),CandidateRows::RowHeight).toReal()+8;
 expectedHeight=qMax(0.,expectedHeight-8);
 int verified=0;
 // Small scroll increments force tall wrapped rows and inter-row gaps through
 // the same path as normal viewport scrolling rather than a full eager pass.
 for(qreal bottom=0;!lazy.complete();bottom+=37){
  lazy.ensureLayout(bottom);
  QVERIFY(lazy.rowCount()<=original.rowCount());
  for(;verified<lazy.rowCount();++verified){QCOMPARE(lazy.data(lazy.index(verified),NativeCandidateRows::Entries),original.data(original.index(verified),CandidateRows::Entries));QCOMPARE(lazy.data(lazy.index(verified),NativeCandidateRows::RowHeight),original.data(original.index(verified),CandidateRows::RowHeight));}
  QVERIFY(lazy.contentHeight()>=0);QVERIFY(std::isfinite(lazy.contentHeight()));
  QVERIFY(bottom<=expectedHeight+100);
 }
 QCOMPARE(lazy.rowCount(),original.rowCount());QCOMPARE(lazy.measuredCandidateCount(),words.size());
 QCOMPARE(lazy.contentHeight(),expectedHeight);QCOMPARE(lazy.roleNames(),original.roleNames());
 QVERIFY(!lazy.data({} ,NativeCandidateRows::Entries).isValid());
 QVERIFY(!lazy.data(original.index(0),NativeCandidateRows::Entries).isValid());
 if(lazy.rowCount())QCOMPARE(lazy.rowCount(lazy.index(0)),0);
 lazy.ensureLayout(1e12);QCOMPARE(lazy.contentHeight(),expectedHeight);
}

void NativeCandidateRowsTest::invalidation()
{
 NativeCandidateRows rows;QFont font;font.setPixelSize(18);rows.setFont(font);rows.setCandidates({"a","b","c"});rows.ensureLayout(100);
 QVERIFY(rows.complete());QVERIFY(rows.rowCount()>0);
 QSignalSpy reset(&rows,&QAbstractItemModel::modelReset);
 rows.setCandidates(rows.candidates());rows.setFont(rows.font());rows.setAvailableWidth(rows.availableWidth());QCOMPARE(reset.size(),0);
 rows.setAvailableWidth(49);QCOMPARE(reset.size(),1);QCOMPARE(rows.rowCount(),0);QCOMPARE(rows.measuredCandidateCount(),0);QVERIFY(!rows.complete());
 rows.ensureLayout(100);font.setPixelSize(13);rows.setFont(font);QCOMPARE(rows.rowCount(),0);QCOMPARE(rows.measuredCandidateCount(),0);
 rows.ensureLayout(100);rows.setCandidates({"replacement"});QCOMPARE(rows.rowCount(),0);QCOMPARE(rows.measuredCandidateCount(),0);
 rows.setCandidates({});QVERIFY(rows.complete());QCOMPARE(rows.contentHeight(),0.);QCOMPARE(rows.rowCount(),0);
}
QTEST_MAIN(NativeCandidateRowsTest)
#include "nativecandidaterows_test.moc"
