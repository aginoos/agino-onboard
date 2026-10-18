// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QQuickImageProvider>
#include <QImageReader>

// A known embedded PNG must not enumerate every system image plugin. Some
// optional codecs pull in video/raw-image runtimes merely to probe the file.
// Raster resources preserve the original DTK SVG rendering at each pixel size.
class KeyboardIconProvider final : public QQuickImageProvider {
public:
 KeyboardIconProvider():QQuickImageProvider(Image){}
 QImage requestImage(const QString &id,QSize *size,const QSize &)override{
  bool ok=false;const int pixels=id.toInt(&ok);
  if(!ok||pixels<1||pixels>192)return {};
  QImageReader reader(QString(":/data/icons/keyboard-%1.png").arg(pixels),"PNG");
  reader.setAutoDetectImageFormat(false);
  auto image=reader.read();if(size)*size=image.size();return image;
 }
};

