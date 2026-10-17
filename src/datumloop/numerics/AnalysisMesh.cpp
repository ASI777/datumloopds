#include "datumloop/numerics/AnalysisMesh.hpp"

#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/topology/DatumTopologyResolver.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Dir.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace datumloop {
namespace {

enum class BoxSide : std::size_t {
    MinimumX,
    MaximumX,
    MinimumY,
    MaximumY,
    MinimumZ,
    MaximumZ,
};

struct ResolvedBoxFace {
    std::string topologyReferenceId;
    BoxSide side = BoxSide::MinimumX;
    Vector3 outwardNormal{0.0, 0.0, 0.0};
    double areaM2 = 0.0;
    int attribute = 0;
};

std::size_t sideIndex(BoxSide side)
{
    return static_cast<std::size_t>(side);
}

std::uint32_t nodeIndex(int x, int y, int z, int nx, int ny)
{
    const std::uint64_t index = static_cast<std::uint64_t>(x) +
        (static_cast<std::uint64_t>(y) + static_cast<std::uint64_t>(z) *
         static_cast<std::uint64_t>(ny + 1)) * static_cast<std::uint64_t>(nx + 1);
    if (index > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("analysis mesh exceeds the supported 32-bit node index range");
    }
    return static_cast<std::uint32_t>(index);
}

double distance(double left, double right)
{
    return std::abs(left - right);
}

bool near(double left, double right, double tolerance)
{
    return distance(left, right) <= tolerance;
}

ResolvedBoxFace resolveBoxFace(const DatumObject& object,
                               const DatumTopologyReference& reference,
                               const ShapeMetrics& metrics,
                               double toleranceMm,
                               const std::string& featureId)
{
    if (reference.objectId != object.id || reference.subshapeType != DatumSubshapeType::Face) {
        throw std::invalid_argument("rectangular-solid meshing requires face references owned by the selected CAD object");
    }
    const DatumTopologyResolution resolution = DatumTopologyResolver::resolve(
        reference, object.id, object.shape, {}, featureId.empty() ? std::nullopt
                                                                   : std::optional<std::string>(featureId));
    if (resolution.status != DatumTopologyResolutionStatus::Resolved ||
        resolution.subshape.ShapeType() != TopAbs_FACE) {
        throw std::invalid_argument("analysis boundary face reference did not resolve uniquely on the current CAD shape");
    }
    const TopoDS_Face face = TopoDS::Face(resolution.subshape);
    const BRepAdaptor_Surface surface(face, Standard_True);
    if (surface.GetType() != GeomAbs_Plane) {
        throw std::invalid_argument("the initial analysis mesher supports planar rectangular-solid faces only");
    }
    const gp_Dir normal = surface.Plane().Axis().Direction();
    const std::array<double, 3> absoluteNormal{
        std::abs(normal.X()), std::abs(normal.Y()), std::abs(normal.Z())};
    const auto axisIterator = std::max_element(absoluteNormal.begin(), absoluteNormal.end());
    const std::size_t axis = static_cast<std::size_t>(axisIterator - absoluteNormal.begin());
    if (*axisIterator < 1.0 - 1e-8) {
        throw std::invalid_argument("the initial analysis mesher supports axis-aligned planar faces only");
    }
    for (std::size_t componentAxis = 0; componentAxis < absoluteNormal.size(); ++componentAxis) {
        if (componentAxis != axis && absoluteNormal[componentAxis] > 1e-8) {
            throw std::invalid_argument("the initial analysis mesher supports axis-aligned planar faces only");
        }
    }

    BoxSide side = BoxSide::MinimumX;
    const double center = reference.geometrySignature.centroidMm[axis];
    bool maximum = false;
    if (near(center, metrics.minimum[axis], toleranceMm)) {
        maximum = false;
    } else if (near(center, metrics.maximum[axis], toleranceMm)) {
        maximum = true;
    } else {
        throw std::invalid_argument("resolved CAD face is not on an exterior rectangular-solid boundary plane");
    }
    if (axis == 0) side = maximum ? BoxSide::MaximumX : BoxSide::MinimumX;
    else if (axis == 1) side = maximum ? BoxSide::MaximumY : BoxSide::MinimumY;
    else side = maximum ? BoxSide::MaximumZ : BoxSide::MinimumZ;

    Vector3 outward{0.0, 0.0, 0.0};
    outward[axis] = maximum ? 1.0 : -1.0;
    GProp_GProps faceProperties;
    BRepGProp::SurfaceProperties(face, faceProperties);
    const double areaM2 = faceProperties.Mass() * 1e-6;
    const double expectedAreaMm2 = axis == 0
        ? (metrics.maximum[1] - metrics.minimum[1]) * (metrics.maximum[2] - metrics.minimum[2])
        : axis == 1
            ? (metrics.maximum[0] - metrics.minimum[0]) * (metrics.maximum[2] - metrics.minimum[2])
            : (metrics.maximum[0] - metrics.minimum[0]) * (metrics.maximum[1] - metrics.minimum[1]);
    if (std::abs(faceProperties.Mass() - expectedAreaMm2) > expectedAreaMm2 * 1e-8) {
        throw std::invalid_argument("each CAD face must cover exactly one rectangular-solid side");
    }
    return {reference.id, side, outward, areaM2, 0};
}

void addBoundaryElement(AnalysisMesh& mesh,
                        int attribute,
                        std::uint32_t a,
                        std::uint32_t b,
                        std::uint32_t c,
                        std::uint32_t d)
{
    mesh.boundaryElements.push_back({{a, b, c, d}, attribute});
}

}  // namespace

void AnalysisMesh::validate() const
{
    if (!isDatumUuid(provenance.revisionId) || !isDatumUuid(cadObjectId) ||
        provenance.generator != "datumloop.occt-rectangular-solid-structured-hex" ||
        provenance.generatorVersion != "1.0.0" || provenance.elementType != "hexahedron8" ||
        provenance.nodeCount != nodes.size() || provenance.elementCount != elements.size() ||
        nodes.size() < 8 || elements.empty() || boundaryElements.empty() ||
        boundaryRegions.size() != 6 || topologyReferenceIds.size() != 6) {
        throw std::invalid_argument("analysis mesh identity, topology, or element counts are invalid");
    }
    if (!std::isfinite(provenance.requestedMaximumElementSizeM) ||
        provenance.requestedMaximumElementSizeM <= 0.0 ||
        !std::isfinite(provenance.minimumScaledJacobian) || provenance.minimumScaledJacobian <= 0.0 ||
        provenance.minimumScaledJacobian > 1.0 + 1e-12 ||
        !std::isfinite(provenance.maximumAspectRatio) || provenance.maximumAspectRatio < 1.0) {
        throw std::invalid_argument("analysis mesh quality metadata is invalid");
    }
    std::unordered_set<std::string> referenceIds;
    std::unordered_set<std::string> meshReferenceIds;
    for (const std::string& id : topologyReferenceIds) {
        if (!isDatumUuid(id) || !meshReferenceIds.insert(id).second) {
            throw std::invalid_argument("analysis mesh topology references must be unique UUIDs");
        }
    }
    std::unordered_set<int> attributes;
    for (const AnalysisBoundaryRegion& region : boundaryRegions) {
        if (!isDatumUuid(region.topologyReferenceId) || !referenceIds.insert(region.topologyReferenceId).second ||
            region.attribute <= 0 || !attributes.insert(region.attribute).second ||
            !std::isfinite(region.areaM2) || region.areaM2 <= 0.0 ||
            std::any_of(region.outwardNormal.begin(), region.outwardNormal.end(),
                        [](double component) { return !std::isfinite(component); })) {
            throw std::invalid_argument("analysis boundary region must map one unique persistent face to one valid mesh attribute");
        }
    }
    if (referenceIds != meshReferenceIds) {
        throw std::invalid_argument("analysis mesh boundary regions must exactly match its CAD face identity list");
    }
    for (const AnalysisNode& node : nodes) {
        if (std::any_of(node.positionM.begin(), node.positionM.end(),
                        [](double coordinate) { return !std::isfinite(coordinate); })) {
            throw std::invalid_argument("analysis mesh nodes must have finite SI coordinates");
        }
    }
    for (const AnalysisHexElement& element : elements) {
        std::unordered_set<std::uint32_t> unique;
        for (const std::uint32_t index : element.nodeIndices) {
            if (index >= nodes.size() || !unique.insert(index).second) {
                throw std::invalid_argument("analysis hexahedron must use eight unique valid mesh nodes");
            }
        }
    }
    for (const AnalysisBoundaryElement& element : boundaryElements) {
        std::unordered_set<std::uint32_t> unique;
        if (!attributes.contains(element.attribute)) {
            throw std::invalid_argument("analysis boundary element references an unknown CAD face attribute");
        }
        for (const std::uint32_t index : element.nodeIndices) {
            if (index >= nodes.size() || !unique.insert(index).second) {
                throw std::invalid_argument("analysis boundary quadrilateral must use four unique valid mesh nodes");
            }
        }
    }
}

AnalysisMesh OcctPrismAnalysisMesher::generate(const DatumDocument& document,
                                               const std::string& cadObjectId,
                                               const AnalysisMeshSettings& settings)
{
    if (!isDatumUuid(cadObjectId) || !std::isfinite(settings.maximumElementSizeM) ||
        settings.maximumElementSizeM <= 0.0 ||
        !std::isfinite(settings.maximumElementAspectRatio) || settings.maximumElementAspectRatio < 1.0 ||
        settings.maximumElementCount == 0) {
        throw std::invalid_argument("analysis mesh settings require a CAD object, positive size, and valid limits");
    }
    const DatumObject* object = document.object(cadObjectId);
    if (object == nullptr || object->shape.IsNull() || !OcctKernel::isValid(object->shape)) {
        throw std::invalid_argument("analysis meshing requires a valid exact OCCT CAD solid");
    }
    const ShapeMetrics metrics = OcctKernel::measure(object->shape);
    int solidCount = 0;
    for (TopExp_Explorer explorer(object->shape, TopAbs_SOLID); explorer.More(); explorer.Next()) ++solidCount;
    if (solidCount != 1 || metrics.volume <= 0.0) {
        throw std::invalid_argument("initial analysis meshing requires one positive-volume OCCT solid");
    }
    const Vector3 dimensionsMm{
        metrics.maximum[0] - metrics.minimum[0],
        metrics.maximum[1] - metrics.minimum[1],
        metrics.maximum[2] - metrics.minimum[2]};
    if (std::any_of(dimensionsMm.begin(), dimensionsMm.end(), [](double value) {
            return !std::isfinite(value) || value <= 0.0;
        })) {
        throw std::invalid_argument("OCCT solid bounds must have three positive finite dimensions");
    }
    const double boundingVolumeMm3 = dimensionsMm[0] * dimensionsMm[1] * dimensionsMm[2];
    if (std::abs(metrics.volume - boundingVolumeMm3) > boundingVolumeMm3 * 1e-8) {
        throw std::invalid_argument("initial analysis meshing accepts only a solid that fills its rectangular bounds");
    }

    int faceCount = 0;
    for (TopExp_Explorer explorer(object->shape, TopAbs_FACE); explorer.More(); explorer.Next()) ++faceCount;
    if (faceCount != 6) {
        throw std::invalid_argument("initial analysis meshing requires six unsplit rectangular-solid faces");
    }
    const double maximumDimensionMm = *std::max_element(dimensionsMm.begin(), dimensionsMm.end());
    const double faceToleranceMm = std::max(1e-7, maximumDimensionMm * 1e-9);
    const std::string featureId = object->featureGraph.outputFeatureId().value_or("");
    std::array<std::optional<ResolvedBoxFace>, 6> resolvedFaces;
    for (const DatumTopologyReference& reference : document.topologyReferences()) {
        if (reference.objectId != cadObjectId || reference.subshapeType != DatumSubshapeType::Face) continue;
        ResolvedBoxFace face = resolveBoxFace(*object, reference, metrics, faceToleranceMm, featureId);
        const std::size_t index = sideIndex(face.side);
        if (resolvedFaces[index].has_value()) {
            throw std::invalid_argument("multiple persistent CAD faces map to the same rectangular-solid side");
        }
        resolvedFaces[index] = std::move(face);
    }
    if (std::any_of(resolvedFaces.begin(), resolvedFaces.end(), [](const auto& face) { return !face.has_value(); })) {
        throw std::invalid_argument("all six rectangular-solid boundary faces need persistent topology references");
    }

    std::array<double, 3> dimensionsM{
        dimensionsMm[0] * 1e-3, dimensionsMm[1] * 1e-3, dimensionsMm[2] * 1e-3};
    std::array<int, 3> cells{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const double requestedCells = std::ceil(dimensionsM[axis] / settings.maximumElementSizeM);
        if (!std::isfinite(requestedCells) ||
            requestedCells > static_cast<double>(std::numeric_limits<int>::max())) {
            throw std::length_error("requested analysis mesh resolution is too fine");
        }
        cells[axis] = std::max(2, static_cast<int>(requestedCells));
    }
    const long double elementCountEstimate = static_cast<long double>(cells[0]) *
                                             static_cast<long double>(cells[1]) *
                                             static_cast<long double>(cells[2]);
    const long double nodeCountEstimate = (static_cast<long double>(cells[0]) + 1.0L) *
                                          (static_cast<long double>(cells[1]) + 1.0L) *
                                          (static_cast<long double>(cells[2]) + 1.0L);
    if (elementCountEstimate > static_cast<long double>(settings.maximumElementCount) ||
        nodeCountEstimate > static_cast<long double>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::length_error("requested analysis mesh exceeds its configured element or node limit");
    }
    const std::uint64_t elementCount = static_cast<std::uint64_t>(elementCountEstimate);
    const std::uint64_t nodeCount = static_cast<std::uint64_t>(nodeCountEstimate);
    const std::array<double, 3> cellSize{
        dimensionsM[0] / cells[0], dimensionsM[1] / cells[1], dimensionsM[2] / cells[2]};
    const auto [minimumCellSize, maximumCellSize] =
        std::minmax_element(cellSize.begin(), cellSize.end());
    const double aspectRatio = *maximumCellSize / *minimumCellSize;
    if (aspectRatio > settings.maximumElementAspectRatio) {
        throw std::invalid_argument("structured mesh cell aspect ratio exceeds the requested quality limit");
    }

    AnalysisMesh mesh;
    mesh.provenance.revisionId = createDatumUuidV4();
    mesh.provenance.generator = "datumloop.occt-rectangular-solid-structured-hex";
    mesh.provenance.generatorVersion = "1.0.0";
    mesh.provenance.elementType = "hexahedron8";
    mesh.provenance.requestedMaximumElementSizeM = settings.maximumElementSizeM;
    mesh.provenance.nodeCount = nodeCount;
    mesh.provenance.elementCount = elementCount;
    mesh.provenance.minimumScaledJacobian = 1.0;
    mesh.provenance.maximumAspectRatio = aspectRatio;
    mesh.cadObjectId = cadObjectId;
    mesh.sourceDocumentRevision = document.revision();

    for (std::size_t side = 0; side < resolvedFaces.size(); ++side) {
        ResolvedBoxFace& face = *resolvedFaces[side];
        face.attribute = static_cast<int>(side + 1);
        mesh.topologyReferenceIds.push_back(face.topologyReferenceId);
        mesh.boundaryRegions.push_back({face.topologyReferenceId, face.attribute, face.outwardNormal, face.areaM2});
    }

    mesh.nodes.reserve(static_cast<std::size_t>(nodeCount));
    for (int z = 0; z <= cells[2]; ++z) {
        for (int y = 0; y <= cells[1]; ++y) {
            for (int x = 0; x <= cells[0]; ++x) {
                mesh.nodes.push_back({{
                    metrics.minimum[0] * 1e-3 + static_cast<double>(x) * cellSize[0],
                    metrics.minimum[1] * 1e-3 + static_cast<double>(y) * cellSize[1],
                    metrics.minimum[2] * 1e-3 + static_cast<double>(z) * cellSize[2]}});
            }
        }
    }
    mesh.elements.reserve(static_cast<std::size_t>(elementCount));
    for (int z = 0; z < cells[2]; ++z) {
        for (int y = 0; y < cells[1]; ++y) {
            for (int x = 0; x < cells[0]; ++x) {
                mesh.elements.push_back({{
                    nodeIndex(x, y, z, cells[0], cells[1]),
                    nodeIndex(x + 1, y, z, cells[0], cells[1]),
                    nodeIndex(x + 1, y + 1, z, cells[0], cells[1]),
                    nodeIndex(x, y + 1, z, cells[0], cells[1]),
                    nodeIndex(x, y, z + 1, cells[0], cells[1]),
                    nodeIndex(x + 1, y, z + 1, cells[0], cells[1]),
                    nodeIndex(x + 1, y + 1, z + 1, cells[0], cells[1]),
                    nodeIndex(x, y + 1, z + 1, cells[0], cells[1])}});
            }
        }
    }
    mesh.boundaryElements.reserve(static_cast<std::size_t>(2ULL *
        (static_cast<std::uint64_t>(cells[0]) * cells[1] +
         static_cast<std::uint64_t>(cells[0]) * cells[2] +
         static_cast<std::uint64_t>(cells[1]) * cells[2])));
    const auto addFace = [&mesh](BoxSide side, std::uint32_t a, std::uint32_t b,
                                 std::uint32_t c, std::uint32_t d) {
        const auto region = std::find_if(mesh.boundaryRegions.begin(), mesh.boundaryRegions.end(),
                                         [side](const AnalysisBoundaryRegion& candidate) {
            const std::size_t index = static_cast<std::size_t>(candidate.attribute - 1);
            return index == sideIndex(side);
        });
        if (region == mesh.boundaryRegions.end()) {
            throw std::logic_error("structured mesh boundary side has no CAD region mapping");
        }
        addBoundaryElement(mesh, region->attribute, a, b, c, d);
    };
    for (int y = 0; y < cells[1]; ++y) {
        for (int x = 0; x < cells[0]; ++x) {
            addFace(BoxSide::MinimumZ,
                    nodeIndex(x, y, 0, cells[0], cells[1]),
                    nodeIndex(x, y + 1, 0, cells[0], cells[1]),
                    nodeIndex(x + 1, y + 1, 0, cells[0], cells[1]),
                    nodeIndex(x + 1, y, 0, cells[0], cells[1]));
            addFace(BoxSide::MaximumZ,
                    nodeIndex(x, y, cells[2], cells[0], cells[1]),
                    nodeIndex(x + 1, y, cells[2], cells[0], cells[1]),
                    nodeIndex(x + 1, y + 1, cells[2], cells[0], cells[1]),
                    nodeIndex(x, y + 1, cells[2], cells[0], cells[1]));
        }
    }
    for (int z = 0; z < cells[2]; ++z) {
        for (int y = 0; y < cells[1]; ++y) {
            addFace(BoxSide::MinimumX,
                    nodeIndex(0, y, z, cells[0], cells[1]),
                    nodeIndex(0, y, z + 1, cells[0], cells[1]),
                    nodeIndex(0, y + 1, z + 1, cells[0], cells[1]),
                    nodeIndex(0, y + 1, z, cells[0], cells[1]));
            addFace(BoxSide::MaximumX,
                    nodeIndex(cells[0], y, z, cells[0], cells[1]),
                    nodeIndex(cells[0], y + 1, z, cells[0], cells[1]),
                    nodeIndex(cells[0], y + 1, z + 1, cells[0], cells[1]),
                    nodeIndex(cells[0], y, z + 1, cells[0], cells[1]));
        }
    }
    for (int x = 0; x < cells[0]; ++x) {
        for (int z = 0; z < cells[2]; ++z) {
            addFace(BoxSide::MinimumY,
                    nodeIndex(x, 0, z, cells[0], cells[1]),
                    nodeIndex(x + 1, 0, z, cells[0], cells[1]),
                    nodeIndex(x + 1, 0, z + 1, cells[0], cells[1]),
                    nodeIndex(x, 0, z + 1, cells[0], cells[1]));
            addFace(BoxSide::MaximumY,
                    nodeIndex(x, cells[1], z, cells[0], cells[1]),
                    nodeIndex(x, cells[1], z + 1, cells[0], cells[1]),
                    nodeIndex(x + 1, cells[1], z + 1, cells[0], cells[1]),
                    nodeIndex(x + 1, cells[1], z, cells[0], cells[1]));
        }
    }
    mesh.validate();
    return mesh;
}

}  // namespace datumloop
