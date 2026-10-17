#include "datumloop/core/DatumTopologyReference.hpp"

#include <stdexcept>

namespace datumloop {

std::string datumSubshapeTypeName(DatumSubshapeType type)
{
    switch (type) {
    case DatumSubshapeType::Face:
        return "face";
    case DatumSubshapeType::Edge:
        return "edge";
    case DatumSubshapeType::Vertex:
        return "vertex";
    }
    throw std::invalid_argument("unknown DatumLoop subshape type");
}

std::optional<DatumSubshapeType> datumSubshapeTypeFromName(const std::string& name)
{
    if (name == "face") {
        return DatumSubshapeType::Face;
    }
    if (name == "edge") {
        return DatumSubshapeType::Edge;
    }
    if (name == "vertex") {
        return DatumSubshapeType::Vertex;
    }
    return std::nullopt;
}

}  // namespace datumloop
