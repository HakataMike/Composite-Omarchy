#pragma once
#include "filters.h"
namespace Arc {
QImage motionBlur(const QImage &source,double distance,double angle);
Layer expandedBlur(const Layer &layer,const FilterSettings &settings);
}
