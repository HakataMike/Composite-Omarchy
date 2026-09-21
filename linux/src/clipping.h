#pragma once
#include "document.h"
#include <QSet>
namespace Arc {
bool hasClipping(const Document &document);
void validateClipping(const Document &document);
QImage renderClipping(const Document &document);
void createClippingMask(Document &document);
void releaseClippingMask(Document &document);
void bakeClippingDependents(Document &document, const QSet<QUuid> &removed);
void bakeClippingMask(Document &document, int index);
}
