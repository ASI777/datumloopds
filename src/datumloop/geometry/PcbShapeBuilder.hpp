#pragma once

#include "datumloop/electronics/ElectronicsModel.hpp"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace datumloop {

struct PcbViewportGeometry {
    TopoDS_Shape shape;
    std::vector<std::string> notices;
};

// Builds display-only solids from the SI board model. These shapes do not
// replace or write back to the PCB's material and manufacturing definitions.
class PcbShapeBuilder final {
public:
    [[nodiscard]] static PcbViewportGeometry build(const PcbDocument& pcb);
};

}  // namespace datumloop
