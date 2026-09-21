#pragma once
#include "document.h"
namespace Arc {
void crop(Document &document, QRect rectangle);
void resizeCanvas(Document &document, QSize size, bool centered);
void resizeImage(Document &document, QSize size);
void mergeDown(Document &document);
QImage blurImage(const QImage &image, int radius);
}
