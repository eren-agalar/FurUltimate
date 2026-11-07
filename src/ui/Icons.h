#pragma once
#include "Canvas.h"

namespace aether {

enum class Icon {
    Overview, Stress, Benchmark, Monitoring, Results, Hardware, Settings,
    Play, Pause, Stop, Export, Compare, Trash, Folder, Camera, Check, Warning, Info, ChevronRight, ChevronDown,
    Close, Refresh, Copy, Thermometer, Fan, Bolt, Chip, Clock, Memory, Gauge, Shield, Trophy, AI, Globe, User
};

// Original line-icon set drawn as vector geometry on a 20 × 20 grid.
void drawIcon(Canvas& c, Icon icon, float x, float y, float size, const Color& color, float stroke = 1.5f);
// FurUltimate brand mark (gradient hexagon badge + flame core + orbiting arcs). `phase` animates it.
void drawLogoMark(Canvas& c, float cx, float cy, float radius, float phase = 0.0f, bool glow = true);

} // namespace aether
