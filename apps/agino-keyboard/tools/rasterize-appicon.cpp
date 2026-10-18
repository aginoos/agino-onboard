// SPDX-License-Identifier: GPL-3.0-or-later
// Regenerate checked-in rasters with the pinned DTK SVG renderer. The running
// keyboard can decode PNG directly instead of loading the optional SVG renderer.
#include <QGuiApplication>
#include <QImageReader>
#include <QImage>
#include <QDir>
int main(int argc,char **argv){
 QGuiApplication app(argc,argv);
 if(argc!=3||!QDir().mkpath(QString::fromLocal8Bit(argv[2])))return 2;
 for(int size=1;size<=192;++size){
  QImageReader reader(QString::fromLocal8Bit(argv[1]));reader.setScaledSize({size,size});
  const auto image=reader.read();
  if(image.isNull()||!image.save(QDir(QString::fromLocal8Bit(argv[2])).filePath(QStringLiteral("keyboard-%1.png").arg(size))))return 1;
 }
 return 0;
}
