#include "datumloop/ui/AnalysisResultDisplay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace datumloop {
namespace {

struct BoundaryFace {
    std::vector<std::uint32_t> orderedNodes;
    Vector3 cellCenterMm{};
    std::size_t elementIndex = 0;
    std::uint8_t occurrenceCount = 0;
};

const FieldDataset& requiredField(const StructuralAnalysisRun& run,
                                  FieldType type,
                                  FieldLocation location,
                                  std::size_t componentCount,
                                  const char* units)
{
    const FieldDataset* selected = nullptr;
    for (const FieldDataset& field : run.fields) {
        if (field.fieldType != type) continue;
        if (selected != nullptr) {
            throw std::invalid_argument("structural run contains duplicate datasets for a displayed field");
        }
        selected = &field;
    }
    if (selected == nullptr || selected->location != location ||
        selected->components.size() != componentCount || selected->units != units) {
        throw std::invalid_argument("structural run does not contain the required field layout for display");
    }
    selected->validate(run.provenance.mesh.nodeCount, run.provenance.mesh.elementCount,
                       run.provenance.mesh.revisionId, run.provenance.solverRunId);
    return *selected;
}

Vector3 add(const Vector3& left, const Vector3& right)
{
    return {left[0] + right[0], left[1] + right[1], left[2] + right[2]};
}

Vector3 subtract(const Vector3& left, const Vector3& right)
{
    return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
}

Vector3 cross(const Vector3& left, const Vector3& right)
{
    return {left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0]};
}

double dot(const Vector3& left, const Vector3& right)
{
    return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}

Vector3 normalized(const Vector3& value)
{
    const double length = std::sqrt(dot(value, value));
    if (!std::isfinite(length) || length <= std::numeric_limits<double>::epsilon()) {
        throw std::invalid_argument("analysis boundary contains a degenerate display face");
    }
    return {value[0] / length, value[1] / length, value[2] / length};
}

float asFloat(double value)
{
    const float converted = static_cast<float>(value);
    if (!std::isfinite(converted)) {
        throw std::invalid_argument("analysis display geometry exceeds the supported float coordinate range");
    }
    return converted;
}

std::array<float, 3> asFloat(const Vector3& value)
{
    return {asFloat(value[0]), asFloat(value[1]), asFloat(value[2])};
}

std::array<float, 3> colorFor(double value, double minimum, double maximum)
{
    static constexpr std::array<std::array<float, 3>, 4> palette{{
        {{0.055F, 0.22F, 0.72F}},
        {{0.0F, 0.78F, 0.87F}},
        {{0.98F, 0.82F, 0.19F}},
        {{0.78F, 0.06F, 0.10F}},
    }};
    const double normalizedValue = maximum > minimum
        ? std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0)
        : 0.5;
    const double scaled = normalizedValue * static_cast<double>(palette.size() - 1);
    const std::size_t segment = std::min(static_cast<std::size_t>(scaled), palette.size() - 2);
    const float blend = static_cast<float>(scaled - static_cast<double>(segment));
    std::array<float, 3> color{};
    for (std::size_t component = 0; component < color.size(); ++component) {
        color[component] = palette[segment][component] * (1.0F - blend) +
                           palette[segment + 1][component] * blend;
    }
    return color;
}

std::vector<double> valuesFor(const FieldDataset& dataset)
{
    if (dataset.fieldType == FieldType::Displacement) {
        std::vector<double> values(dataset.values.size() / 3);
        for (std::size_t node = 0; node < values.size(); ++node) {
            const double x = dataset.values[node * 3];
            const double y = dataset.values[node * 3 + 1];
            const double z = dataset.values[node * 3 + 2];
            values[node] = std::sqrt(x * x + y * y + z * z) * 1e3;  // mm
        }
        return values;
    }
    if (dataset.fieldType == FieldType::VonMisesStress) {
        std::vector<double> values = dataset.values;
        for (double& value : values) value *= 1e-6;  // MPa
        return values;
    }
    if (dataset.fieldType == FieldType::StrainTensor) {
        std::vector<double> values(dataset.values.size() / 6);
        for (std::size_t element = 0; element < values.size(); ++element) {
            const std::size_t offset = element * 6;
            const double mean = (dataset.values[offset] + dataset.values[offset + 1] +
                                 dataset.values[offset + 2]) / 3.0;
            const double xx = dataset.values[offset] - mean;
            const double yy = dataset.values[offset + 1] - mean;
            const double zz = dataset.values[offset + 2] - mean;
            const double xy = dataset.values[offset + 3];
            const double yz = dataset.values[offset + 4];
            const double zx = dataset.values[offset + 5];
            const double deviatoricNormSquared = xx * xx + yy * yy + zz * zz +
                                                  2.0 * (xy * xy + yz * yz + zx * zx);
            values[element] = std::sqrt((2.0 / 3.0) * deviatoricNormSquared) * 1e6;  // microstrain
        }
        return values;
    }
    throw std::invalid_argument("unsupported structural result field for viewport display");
}

AnalysisResultDisplay metadataFor(AnalysisResultField field)
{
    switch (field) {
    case AnalysisResultField::DisplacementMagnitude:
        return {{}, "Displacement magnitude", "mm", 0.0, 0.0};
    case AnalysisResultField::VonMisesStress:
        return {{}, "von Mises stress", "MPa", 0.0, 0.0};
    case AnalysisResultField::EquivalentStrain:
        return {{}, "Equivalent strain", "µε", 0.0, 0.0};
    }
    throw std::invalid_argument("unknown structural result display field");
}

}  // namespace

AnalysisResultDisplay buildAnalysisResultDisplay(const StructuralAnalysisRun& run,
                                                 AnalysisResultField field,
                                                 double deformationScale)
{
    if (!std::isfinite(deformationScale) || deformationScale < 0.0 || deformationScale > 1e6) {
        throw std::invalid_argument("deformation display scale must be finite and between zero and one million");
    }
    if (!run.converged || !run.meshSnapshot.has_value()) {
        throw std::invalid_argument("field visualization requires a converged run with its persisted analysis mesh");
    }
    const AnalysisMeshSnapshot& mesh = *run.meshSnapshot;
    mesh.validate(run.provenance.mesh);

    const FieldDataset& displacement = requiredField(run, FieldType::Displacement,
        FieldLocation::Nodes, 3, "m");
    const FieldDataset* displayedField = nullptr;
    switch (field) {
    case AnalysisResultField::DisplacementMagnitude:
        displayedField = &displacement;
        break;
    case AnalysisResultField::VonMisesStress:
        displayedField = &requiredField(run, FieldType::VonMisesStress,
            FieldLocation::Elements, 1, "Pa");
        break;
    case AnalysisResultField::EquivalentStrain:
        displayedField = &requiredField(run, FieldType::StrainTensor,
            FieldLocation::Elements, 6, "1");
        break;
    }
    std::vector<double> fieldValues = valuesFor(*displayedField);
    if (fieldValues.empty() || std::any_of(fieldValues.begin(), fieldValues.end(), [](double value) {
            return !std::isfinite(value);
        })) {
        throw std::invalid_argument("structural result display contains no finite field samples");
    }
    const auto [rangeMinimum, rangeMaximum] = std::minmax_element(fieldValues.begin(), fieldValues.end());
    AnalysisResultDisplay result = metadataFor(field);
    result.minimum = *rangeMinimum;
    result.maximum = *rangeMaximum;
    result.mesh.hasVertexColors = true;
    result.mesh.minimum = {std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::infinity()};
    result.mesh.maximum = {-std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()};

    std::vector<Vector3> displayPositions(mesh.nodePositionsM.size());
    for (std::size_t node = 0; node < mesh.nodePositionsM.size(); ++node) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            displayPositions[node][axis] = (mesh.nodePositionsM[node][axis] +
                deformationScale * displacement.values[node * 3 + axis]) * 1e3;  // CAD viewport mm
        }
    }

    static constexpr std::array<std::array<std::uint8_t, 4>, 6> hexFaces{{
        {{0, 1, 2, 3}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}},
        {{1, 2, 6, 5}}, {{2, 3, 7, 6}}, {{3, 0, 4, 7}},
    }};
    static constexpr std::array<std::array<std::uint8_t, 3>, 4> tetFaces{{
        {{0, 1, 2}}, {{0, 3, 1}}, {{0, 2, 3}}, {{1, 3, 2}},
    }};
    std::map<std::vector<std::uint32_t>, BoundaryFace> faces;
    const auto collectFaces = [&](const auto& elements, const auto& localFaces) {
        for (std::size_t elementIndex=0; elementIndex<elements.size(); ++elementIndex) {
            const auto& element=elements[elementIndex];
            Vector3 center{};
            for (const auto node : element) center=add(center, displayPositions[node]);
            for (double& component : center) component/=static_cast<double>(element.size());
            for (const auto& localFace : localFaces) {
                BoundaryFace face;
                face.elementIndex=elementIndex;
                face.cellCenterMm=center;
                face.occurrenceCount=1;
                for (const auto index : localFace) face.orderedNodes.push_back(element[index]);
                auto key=face.orderedNodes;
                std::sort(key.begin(), key.end());
                const auto [found,inserted]=faces.emplace(key,face);
                if (!inserted && ++found->second.occurrenceCount>2)
                    throw std::invalid_argument("analysis mesh contains a non-manifold internal face");
            }
        }
    };
    collectFaces(mesh.hexahedra,hexFaces);
    collectFaces(mesh.tetrahedra,tetFaces);

    std::size_t boundaryFaceCount = 0;
    for (const auto& [key, face] : faces) {
        (void)key;
        if (face.occurrenceCount == 1) ++boundaryFaceCount;
    }
    if (boundaryFaceCount == 0) {
        throw std::invalid_argument("analysis mesh has no exterior faces to display");
    }
    result.mesh.vertices.reserve(boundaryFaceCount * 6);

    const auto appendVertex = [&](std::uint32_t nodeIndex, std::size_t elementIndex,
                                  const Vector3& normal) {
        const std::size_t sampleIndex = displayedField->location == FieldLocation::Nodes
            ? static_cast<std::size_t>(nodeIndex) : elementIndex;
        const auto color = colorFor(fieldValues.at(sampleIndex), result.minimum, result.maximum);
        const Vector3& position = displayPositions.at(nodeIndex);
        result.mesh.vertices.push_back({asFloat(position), asFloat(normal),
                                        {color[0], color[1], color[2], 1.0F}});
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.mesh.minimum[axis] = std::min(result.mesh.minimum[axis], position[axis]);
            result.mesh.maximum[axis] = std::max(result.mesh.maximum[axis], position[axis]);
        }
    };
    for (const auto& [key, face] : faces) {
        (void)key;
        if (face.occurrenceCount != 1) continue;
        auto nodes = face.orderedNodes;
        const Vector3 edge1 = subtract(displayPositions[nodes[1]], displayPositions[nodes[0]]);
        const Vector3 edge2 = subtract(displayPositions[nodes[2]], displayPositions[nodes[0]]);
        Vector3 normal = normalized(cross(edge1, edge2));
        Vector3 faceCenter{0.0, 0.0, 0.0};
        for (const std::uint32_t node : nodes) faceCenter = add(faceCenter, displayPositions[node]);
        for (double& component : faceCenter) component /= static_cast<double>(nodes.size());
        if (dot(normal, subtract(faceCenter, face.cellCenterMm)) < 0.0) {
            std::reverse(nodes.begin()+1, nodes.end());
            normal = {-normal[0], -normal[1], -normal[2]};
        }
        appendVertex(nodes[0], face.elementIndex, normal);
        appendVertex(nodes[1], face.elementIndex, normal);
        appendVertex(nodes[2], face.elementIndex, normal);
        if (nodes.size() == 4) {
            appendVertex(nodes[0], face.elementIndex, normal);
            appendVertex(nodes[2], face.elementIndex, normal);
            appendVertex(nodes[3], face.elementIndex, normal);
        }
    }
    return result;
}

}  // namespace datumloop
