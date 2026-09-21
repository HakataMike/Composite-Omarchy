#pragma once
#include "document.h"
namespace Arc {
QVector<int> layerOrder(const Document &document, bool topFirst = false);
QVector<int> descendants(const Document &document, QUuid parent);
bool effectiveVisible(const Document &document, int index);
void validateHierarchy(const Document &document);
Document flattenGroups(const Document &document);
void groupLayers(Document &document, const QVector<QUuid> &selected);
QVector<int> visibleLayerOrder(const Document &document, bool topFirst = false);
void mergeFolder(Document &document);
void duplicateLayer(Document &document);
void deleteLayer(Document &document);
void reorderLayer(Document &document, bool raise);
void transformGroup(Document &document, int index, const Layer &replacement);
}
