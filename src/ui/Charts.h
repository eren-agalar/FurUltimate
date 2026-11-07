#pragma once
#include "UI.h"
#include <string>
#include <vector>

namespace aether {

struct ChartSeries {
    std::vector<float> values;     // evenly spaced samples, NaN = gap
    Color color;
    std::string name;
    std::string unit;
    bool fill = true;
};

struct ChartOptions {
    float minY = 0, maxY = -1;     // maxY < minY => automatic
    bool grid = true;
    bool axisLabels = true;
    float spanSeconds = 0;         // for the time axis label (0 = hide)
    int decimals = 0;
    bool hover = true;
};

void lineChart(UI& ui, const std::string& key, const Rect& r, const std::vector<ChartSeries>& series, const ChartOptions& opt);

} // namespace aether
