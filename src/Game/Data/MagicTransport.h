#pragma once

#include "../GameTypes.h"

#include <functional>
#include <optional>

Point getMagicTransportNeighbor(Point from, int direction);
std::optional<Point> resolveMagicTransportDestination(Point preferred, const std::function<bool(Point)>& canStand);
