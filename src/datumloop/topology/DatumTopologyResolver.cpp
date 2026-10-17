#include "datumloop/topology/DatumTopologyResolver.hpp"

#include "datumloop/core/DatumUuid.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace datumloop {
namespace {

std::string surfaceName(GeomAbs_SurfaceType type)
{
    switch (type) {
    case GeomAbs_Plane: return "plane";
    case GeomAbs_Cylinder: return "cylinder";
    case GeomAbs_Cone: return "cone";
    case GeomAbs_Sphere: return "sphere";
    case GeomAbs_Torus: return "torus";
    case GeomAbs_BezierSurface: return "bezier-surface";
    case GeomAbs_BSplineSurface: return "bspline-surface";
    case GeomAbs_SurfaceOfRevolution: return "surface-of-revolution";
    case GeomAbs_SurfaceOfExtrusion: return "surface-of-extrusion";
    case GeomAbs_OffsetSurface: return "offset-surface";
    case GeomAbs_OtherSurface: return "other-surface";
    }
    return "unknown-surface";
}

std::string curveName(GeomAbs_CurveType type)
{
    switch (type) {
    case GeomAbs_Line: return "line";
    case GeomAbs_Circle: return "circle";
    case GeomAbs_Ellipse: return "ellipse";
    case GeomAbs_Hyperbola: return "hyperbola";
    case GeomAbs_Parabola: return "parabola";
    case GeomAbs_BezierCurve: return "bezier-curve";
    case GeomAbs_BSplineCurve: return "bspline-curve";
    case GeomAbs_OffsetCurve: return "offset-curve";
    case GeomAbs_OtherCurve: return "other-curve";
    }
    return "unknown-curve";
}

Vector3 pointVector(const gp_Pnt& point)
{
    return {point.X(), point.Y(), point.Z()};
}

Vector3 directionVector(const gp_Dir& direction)
{
    return {direction.X(), direction.Y(), direction.Z()};
}

void addSurfaceDirection(DatumTopologyGeometrySignature& signature, const BRepAdaptor_Surface& surface)
{
    switch (surface.GetType()) {
    case GeomAbs_Plane:
        signature.direction = directionVector(surface.Plane().Axis().Direction());
        break;
    case GeomAbs_Cylinder:
        signature.direction = directionVector(surface.Cylinder().Axis().Direction());
        break;
    case GeomAbs_Cone:
        signature.direction = directionVector(surface.Cone().Axis().Direction());
        break;
    case GeomAbs_Sphere:
        signature.direction = directionVector(surface.Sphere().Position().Axis().Direction());
        break;
    case GeomAbs_Torus:
        signature.direction = directionVector(surface.Torus().Axis().Direction());
        break;
    default:
        break;
    }
}

DatumTopologyGeometrySignature describeFace(const TopoDS_Face& face)
{
    DatumTopologyGeometrySignature signature;
    const BRepAdaptor_Surface surface(face, Standard_True);
    signature.geometryType = surfaceName(surface.GetType());
    addSurfaceDirection(signature, surface);
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties);
    signature.measure = properties.Mass();
    signature.centroidMm = pointVector(properties.CentreOfMass());
    return signature;
}

DatumTopologyGeometrySignature describeEdge(const TopoDS_Edge& edge)
{
    DatumTopologyGeometrySignature signature;
    const BRepAdaptor_Curve curve(edge);
    signature.geometryType = curveName(curve.GetType());
    GProp_GProps properties;
    BRepGProp::LinearProperties(edge, properties);
    signature.measure = properties.Mass();
    signature.centroidMm = pointVector(properties.CentreOfMass());
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    if (std::isfinite(first) && std::isfinite(last) && last > first) {
        try {
            gp_Pnt point;
            gp_Vec tangent;
            curve.D1(first + (last - first) * 0.5, point, tangent);
            if (tangent.SquareMagnitude() > 1e-24) {
                tangent.Normalize();
                signature.direction = Vector3{tangent.X(), tangent.Y(), tangent.Z()};
            }
        } catch (const Standard_Failure&) {
        }
    }
    return signature;
}

DatumTopologyGeometrySignature describeVertex(const TopoDS_Vertex& vertex)
{
    DatumTopologyGeometrySignature signature;
    signature.geometryType = "point";
    signature.centroidMm = pointVector(BRep_Tool::Pnt(vertex));
    return signature;
}

DatumTopologyNeighborSignature neighborSignature(DatumSubshapeType type,
                                                  const DatumTopologyGeometrySignature& geometry)
{
    return {type, geometry.geometryType, geometry.measure};
}

void appendAdjacency(DatumTopologyGeometrySignature& signature,
                     DatumSubshapeType type,
                     const DatumTopologyGeometrySignature& neighbor)
{
    signature.adjacent.push_back(neighborSignature(type, neighbor));
}

bool neighborLess(const DatumTopologyNeighborSignature& left,
                  const DatumTopologyNeighborSignature& right)
{
    return std::tie(left.subshapeType, left.geometryType, left.measure) <
           std::tie(right.subshapeType, right.geometryType, right.measure);
}

std::string buildTopologySignature(DatumSubshapeType type,
                                   const DatumTopologyGeometrySignature& geometry)
{
    std::vector<DatumTopologyNeighborSignature> adjacent = geometry.adjacent;
    std::sort(adjacent.begin(), adjacent.end(), neighborLess);
    std::ostringstream signature;
    signature.imbue(std::locale::classic());
    signature << datumSubshapeTypeName(type) << '|' << geometry.geometryType << '|';
    for (const DatumTopologyNeighborSignature& neighbor : adjacent) {
        signature << datumSubshapeTypeName(neighbor.subshapeType) << ':' << neighbor.geometryType << ':'
                  << std::fixed << std::setprecision(6) << neighbor.measure << ';';
    }
    return signature.str();
}

DatumTopologyReference makeReference(const std::string& objectId,
                                     const std::optional<std::string>& featureId,
                                     DatumSubshapeType type,
                                     std::size_t index,
                                     DatumTopologyGeometrySignature geometry)
{
    DatumTopologyReference reference;
    reference.id = createDatumUuidV4();
    reference.objectId = objectId;
    reference.featureId = featureId;
    reference.subshapeType = type;
    reference.topologySignature = buildTopologySignature(type, geometry);
    reference.geometrySignature = std::move(geometry);
    reference.transientIndexHint = static_cast<std::uint32_t>(index);
    return reference;
}

double boundingDiagonal(const TopoDS_Shape& shape)
{
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds, Standard_False);
    if (bounds.IsVoid() || bounds.IsOpenXmin() || bounds.IsOpenXmax() ||
        bounds.IsOpenYmin() || bounds.IsOpenYmax() || bounds.IsOpenZmin() || bounds.IsOpenZmax()) {
        return 1.0;
    }
    double xmin = 0.0, ymin = 0.0, zmin = 0.0, xmax = 0.0, ymax = 0.0, zmax = 0.0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double diagonal = std::sqrt((xmax - xmin) * (xmax - xmin) +
                                      (ymax - ymin) * (ymax - ymin) +
                                      (zmax - zmin) * (zmax - zmin));
    return diagonal > 1e-12 ? diagonal : 1.0;
}

double distance(const Vector3& left, const Vector3& right)
{
    const double x = left[0] - right[0];
    const double y = left[1] - right[1];
    const double z = left[2] - right[2];
    return std::sqrt(x * x + y * y + z * z);
}

double relativeError(double left, double right)
{
    const double denominator = std::max({std::abs(left), std::abs(right), 1e-12});
    return std::abs(left - right) / denominator;
}

double adjacencyMismatch(const std::vector<DatumTopologyNeighborSignature>& left,
                         const std::vector<DatumTopologyNeighborSignature>& right)
{
    std::vector<DatumTopologyNeighborSignature> leftSorted = left;
    std::vector<DatumTopologyNeighborSignature> rightSorted = right;
    std::sort(leftSorted.begin(), leftSorted.end(), neighborLess);
    std::sort(rightSorted.begin(), rightSorted.end(), neighborLess);
    std::vector<DatumTopologyNeighborSignature> intersection;
    std::set_intersection(leftSorted.begin(), leftSorted.end(), rightSorted.begin(), rightSorted.end(),
                          std::back_inserter(intersection), neighborLess);
    const std::size_t unionCount = leftSorted.size() + rightSorted.size() - intersection.size();
    return unionCount == 0 ? 0.0
                           : 1.0 - static_cast<double>(intersection.size()) / static_cast<double>(unionCount);
}

double directionMismatch(const std::optional<Vector3>& left, const std::optional<Vector3>& right)
{
    if (!left.has_value() || !right.has_value()) {
        return 0.0;
    }
    const double leftLength = std::sqrt((*left)[0] * (*left)[0] + (*left)[1] * (*left)[1] +
                                        (*left)[2] * (*left)[2]);
    const double rightLength = std::sqrt((*right)[0] * (*right)[0] + (*right)[1] * (*right)[1] +
                                         (*right)[2] * (*right)[2]);
    if (leftLength <= 1e-12 || rightLength <= 1e-12) {
        return 1.0;
    }
    const double dot = std::abs(((*left)[0] * (*right)[0] + (*left)[1] * (*right)[1] +
                                 (*left)[2] * (*right)[2]) / (leftLength * rightLength));
    return 1.0 - std::clamp(dot, 0.0, 1.0);
}

bool finiteVector(const Vector3& value)
{
    return std::all_of(value.begin(), value.end(), [](double component) {
        return std::isfinite(component);
    });
}

double candidateScore(const DatumTopologyReference& reference,
                      const DatumTopologyGeometrySignature& candidate,
                      double diagonal,
                      const std::string& candidateTopology)
{
    const double topologyPenalty = reference.topologySignature == candidateTopology ? 0.0 : 0.15;
    const double measureError = std::min(relativeError(reference.geometrySignature.measure, candidate.measure), 1.0);
    const double centroidRatio = std::min(distance(reference.geometrySignature.centroidMm, candidate.centroidMm) /
                                          diagonal, 1.0);
    return topologyPenalty + measureError * 0.25 + centroidRatio * 0.40 +
           directionMismatch(reference.geometrySignature.direction, candidate.direction) * 0.15 +
           adjacencyMismatch(reference.geometrySignature.adjacent, candidate.adjacent) * 0.05;
}

std::vector<DatumTopologyReference> captureFaces(const std::string& objectId,
                                                  const std::optional<std::string>& featureId,
                                                  const TopTools_IndexedMapOfShape& faces,
                                                  const TopTools_IndexedMapOfShape& edges)
{
    std::vector<DatumTopologyReference> references;
    references.reserve(static_cast<std::size_t>(faces.Extent()));
    for (Standard_Integer index = 1; index <= faces.Extent(); ++index) {
        const TopoDS_Face face = TopoDS::Face(faces(index));
        DatumTopologyGeometrySignature geometry = describeFace(face);
        for (TopExp_Explorer explorer(face, TopAbs_EDGE); explorer.More(); explorer.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
            if (edges.FindIndex(edge) > 0) {
                appendAdjacency(geometry, DatumSubshapeType::Edge, describeEdge(edge));
            }
        }
        references.push_back(makeReference(objectId, featureId, DatumSubshapeType::Face,
                                           static_cast<std::size_t>(index - 1), std::move(geometry)));
    }
    return references;
}

std::vector<DatumTopologyReference> captureEdges(
    const std::string& objectId,
    const std::optional<std::string>& featureId,
    const TopTools_IndexedMapOfShape& edges,
    const TopTools_IndexedMapOfShape& faces,
    const TopTools_IndexedMapOfShape& vertices,
    const TopTools_IndexedDataMapOfShapeListOfShape& edgeFaces,
    const std::vector<DatumTopologyReference>& faceReferences)
{
    std::vector<DatumTopologyReference> references;
    references.reserve(static_cast<std::size_t>(edges.Extent()));
    for (Standard_Integer index = 1; index <= edges.Extent(); ++index) {
        const TopoDS_Edge edge = TopoDS::Edge(edges(index));
        DatumTopologyGeometrySignature geometry = describeEdge(edge);
        DatumTopologyReference reference = makeReference(objectId, featureId, DatumSubshapeType::Edge,
                                                         static_cast<std::size_t>(index - 1), std::move(geometry));
        const Standard_Integer faceMapIndex = edgeFaces.FindIndex(edge);
        if (faceMapIndex > 0) {
            for (TopTools_ListIteratorOfListOfShape iterator(edgeFaces.FindFromIndex(faceMapIndex));
                 iterator.More(); iterator.Next()) {
                const TopoDS_Face adjacentFace = TopoDS::Face(iterator.Value());
                appendAdjacency(reference.geometrySignature, DatumSubshapeType::Face,
                                describeFace(adjacentFace));
                const Standard_Integer parentIndex = faces.FindIndex(adjacentFace);
                if (parentIndex > 0) {
                    reference.parentReferenceIds.push_back(faceReferences[static_cast<std::size_t>(parentIndex - 1)].id);
                }
            }
        }
        for (TopExp_Explorer explorer(edge, TopAbs_VERTEX); explorer.More(); explorer.Next()) {
            const TopoDS_Vertex vertex = TopoDS::Vertex(explorer.Current());
            if (vertices.FindIndex(vertex) > 0) {
                appendAdjacency(reference.geometrySignature, DatumSubshapeType::Vertex,
                                describeVertex(vertex));
            }
        }
        std::sort(reference.parentReferenceIds.begin(), reference.parentReferenceIds.end());
        reference.parentReferenceIds.erase(
            std::unique(reference.parentReferenceIds.begin(), reference.parentReferenceIds.end()),
            reference.parentReferenceIds.end());
        reference.topologySignature = buildTopologySignature(reference.subshapeType, reference.geometrySignature);
        references.push_back(std::move(reference));
    }
    return references;
}

std::vector<DatumTopologyReference> captureVertices(
    const std::string& objectId,
    const std::optional<std::string>& featureId,
    const TopTools_IndexedMapOfShape& vertices,
    const TopTools_IndexedMapOfShape& edges,
    const TopTools_IndexedDataMapOfShapeListOfShape& vertexEdges,
    const std::vector<DatumTopologyReference>& edgeReferences)
{
    std::vector<DatumTopologyReference> references;
    references.reserve(static_cast<std::size_t>(vertices.Extent()));
    for (Standard_Integer index = 1; index <= vertices.Extent(); ++index) {
        const TopoDS_Vertex vertex = TopoDS::Vertex(vertices(index));
        DatumTopologyGeometrySignature geometry = describeVertex(vertex);
        DatumTopologyReference reference = makeReference(objectId, featureId, DatumSubshapeType::Vertex,
                                                         static_cast<std::size_t>(index - 1), std::move(geometry));
        const Standard_Integer edgeMapIndex = vertexEdges.FindIndex(vertex);
        if (edgeMapIndex > 0) {
            for (TopTools_ListIteratorOfListOfShape iterator(vertexEdges.FindFromIndex(edgeMapIndex));
                 iterator.More(); iterator.Next()) {
                const TopoDS_Edge adjacentEdge = TopoDS::Edge(iterator.Value());
                appendAdjacency(reference.geometrySignature, DatumSubshapeType::Edge,
                                describeEdge(adjacentEdge));
                const Standard_Integer parentIndex = edges.FindIndex(adjacentEdge);
                if (parentIndex > 0) {
                    reference.parentReferenceIds.push_back(edgeReferences[static_cast<std::size_t>(parentIndex - 1)].id);
                }
            }
        }
        std::sort(reference.parentReferenceIds.begin(), reference.parentReferenceIds.end());
        reference.parentReferenceIds.erase(
            std::unique(reference.parentReferenceIds.begin(), reference.parentReferenceIds.end()),
            reference.parentReferenceIds.end());
        reference.topologySignature = buildTopologySignature(reference.subshapeType, reference.geometrySignature);
        references.push_back(std::move(reference));
    }
    return references;
}

}  // namespace

std::vector<DatumTopologyReference> DatumTopologyResolver::captureAll(
    const std::string& objectId,
    const TopoDS_Shape& shape,
    std::optional<std::string> featureId)
{
    if (!isDatumUuid(objectId) || (featureId.has_value() && !isDatumUuid(*featureId))) {
        throw std::invalid_argument("topology capture requires valid object and optional feature UUIDs");
    }
    if (shape.IsNull()) {
        throw std::invalid_argument("cannot capture topology from a null OCCT shape");
    }

    TopTools_IndexedMapOfShape faces;
    TopTools_IndexedMapOfShape edges;
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);

    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopTools_IndexedDataMapOfShapeListOfShape vertexEdges;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    TopExp::MapShapesAndAncestors(shape, TopAbs_VERTEX, TopAbs_EDGE, vertexEdges);

    std::vector<DatumTopologyReference> faceReferences =
        captureFaces(objectId, featureId, faces, edges);
    std::vector<DatumTopologyReference> edgeReferences =
        captureEdges(objectId, featureId, edges, faces, vertices, edgeFaces, faceReferences);
    std::vector<DatumTopologyReference> vertexReferences =
        captureVertices(objectId, featureId, vertices, edges, vertexEdges, edgeReferences);

    std::vector<DatumTopologyReference> references;
    references.reserve(faceReferences.size() + edgeReferences.size() + vertexReferences.size());
    references.insert(references.end(), std::make_move_iterator(faceReferences.begin()),
                      std::make_move_iterator(faceReferences.end()));
    references.insert(references.end(), std::make_move_iterator(edgeReferences.begin()),
                      std::make_move_iterator(edgeReferences.end()));
    references.insert(references.end(), std::make_move_iterator(vertexReferences.begin()),
                      std::make_move_iterator(vertexReferences.end()));
    return references;
}

DatumTopologyResolution DatumTopologyResolver::resolve(
    const DatumTopologyReference& reference,
    const std::string& currentObjectId,
    const TopoDS_Shape& currentShape,
    const DatumTopologyResolveOptions& options)
{
    DatumTopologyResolution result;
    if (!std::isfinite(options.maximumRelativeMeasureError) || options.maximumRelativeMeasureError < 0.0 ||
        !std::isfinite(options.maximumCentroidDistanceRatio) || options.maximumCentroidDistanceRatio < 0.0 ||
        !std::isfinite(options.maximumScore) || options.maximumScore < 0.0 ||
        !std::isfinite(options.ambiguityTolerance) || options.ambiguityTolerance < 0.0) {
        throw std::invalid_argument("topology resolver tolerances must be finite and non-negative");
    }
    if (currentObjectId != reference.objectId) {
        result.status = DatumTopologyResolutionStatus::OwnerMismatch;
        return result;
    }
    if (reference.featureId.has_value()) {
        result.status = DatumTopologyResolutionStatus::UnsupportedFeature;
        return result;
    }
    if (currentShape.IsNull()) {
        result.status = DatumTopologyResolutionStatus::NotFound;
        return result;
    }
    if (reference.topologySignature.empty() || reference.geometrySignature.geometryType.empty() ||
        !std::isfinite(reference.geometrySignature.measure) || reference.geometrySignature.measure < 0.0 ||
        !finiteVector(reference.geometrySignature.centroidMm) ||
        (reference.geometrySignature.direction.has_value() &&
         !finiteVector(*reference.geometrySignature.direction))) {
        throw std::invalid_argument("topology resolver received an invalid reference signature");
    }
    if (reference.geometrySignature.direction.has_value()) {
        const Vector3& direction = *reference.geometrySignature.direction;
        const double lengthSquared = direction[0] * direction[0] + direction[1] * direction[1] +
                                     direction[2] * direction[2];
        if (lengthSquared <= 1e-24) {
            throw std::invalid_argument("topology resolver received a zero reference direction");
        }
    }
    for (const DatumTopologyNeighborSignature& neighbor : reference.geometrySignature.adjacent) {
        if (neighbor.geometryType.empty() || !std::isfinite(neighbor.measure) || neighbor.measure < 0.0) {
            throw std::invalid_argument("topology resolver received invalid adjacency data");
        }
        switch (neighbor.subshapeType) {
        case DatumSubshapeType::Face:
        case DatumSubshapeType::Edge:
        case DatumSubshapeType::Vertex:
            break;
        default:
            throw std::invalid_argument("topology resolver received an unsupported adjacency type");
        }
    }

    TopTools_IndexedMapOfShape candidates;
    TopAbs_ShapeEnum shapeType = TopAbs_SHAPE;
    switch (reference.subshapeType) {
    case DatumSubshapeType::Face:
        shapeType = TopAbs_FACE;
        break;
    case DatumSubshapeType::Edge:
        shapeType = TopAbs_EDGE;
        break;
    case DatumSubshapeType::Vertex:
        shapeType = TopAbs_VERTEX;
        break;
    default:
        throw std::invalid_argument("topology resolver received an unsupported subshape type");
    }
    TopExp::MapShapes(currentShape, shapeType, candidates);
    if (candidates.IsEmpty()) {
        result.status = DatumTopologyResolutionStatus::NotFound;
        return result;
    }

    TopTools_IndexedMapOfShape faces;
    TopTools_IndexedMapOfShape edges;
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(currentShape, TopAbs_FACE, faces);
    TopExp::MapShapes(currentShape, TopAbs_EDGE, edges);
    TopExp::MapShapes(currentShape, TopAbs_VERTEX, vertices);
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopTools_IndexedDataMapOfShapeListOfShape vertexEdges;
    TopExp::MapShapesAndAncestors(currentShape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    TopExp::MapShapesAndAncestors(currentShape, TopAbs_VERTEX, TopAbs_EDGE, vertexEdges);
    const double diagonal = boundingDiagonal(currentShape);

    std::vector<DatumTopologyReference> candidateReferences;
    if (reference.subshapeType == DatumSubshapeType::Face) {
        candidateReferences = captureFaces(currentObjectId, std::nullopt, faces, edges);
    } else if (reference.subshapeType == DatumSubshapeType::Edge) {
        const auto faceReferences = captureFaces(currentObjectId, std::nullopt, faces, edges);
        candidateReferences = captureEdges(currentObjectId, std::nullopt, edges, faces, vertices,
                                           edgeFaces, faceReferences);
    } else {
        const auto faceReferences = captureFaces(currentObjectId, std::nullopt, faces, edges);
        const auto edgeReferences = captureEdges(currentObjectId, std::nullopt, edges, faces, vertices,
                                                 edgeFaces, faceReferences);
        candidateReferences = captureVertices(currentObjectId, std::nullopt, vertices, edges,
                                               vertexEdges, edgeReferences);
    }

    struct ScoredCandidate {
        std::size_t index;
        TopoDS_Shape shape;
        double score;
    };
    std::vector<ScoredCandidate> scored;
    scored.reserve(candidateReferences.size());
    for (std::size_t index = 0; index < candidateReferences.size(); ++index) {
        const DatumTopologyReference& candidate = candidateReferences[index];
        if (candidate.geometrySignature.geometryType != reference.geometrySignature.geometryType) {
            continue;
        }
        const double measureError = relativeError(reference.geometrySignature.measure,
                                                  candidate.geometrySignature.measure);
        const double centroidRatio = distance(reference.geometrySignature.centroidMm,
                                              candidate.geometrySignature.centroidMm) / diagonal;
        if (measureError > options.maximumRelativeMeasureError ||
            centroidRatio > options.maximumCentroidDistanceRatio) {
            continue;
        }
        scored.push_back({index, candidates(static_cast<Standard_Integer>(index + 1)),
                          candidateScore(reference, candidate.geometrySignature, diagonal,
                                         candidate.topologySignature)});
    }
    if (scored.empty()) {
        result.status = DatumTopologyResolutionStatus::NotFound;
        return result;
    }
    std::sort(scored.begin(), scored.end(), [](const ScoredCandidate& left, const ScoredCandidate& right) {
        if (left.score != right.score) {
            return left.score < right.score;
        }
        return left.index < right.index;
    });
    if (scored.front().score > options.maximumScore) {
        result.status = DatumTopologyResolutionStatus::NotFound;
        return result;
    }
    if (scored.size() > 1 &&
        std::abs(scored[0].score - scored[1].score) <= options.ambiguityTolerance) {
        result.status = DatumTopologyResolutionStatus::Ambiguous;
        return result;
    }
    result.status = DatumTopologyResolutionStatus::Resolved;
    result.subshape = scored.front().shape;
    result.transientIndex = scored.front().index;
    result.score = scored.front().score;
    return result;
}

DatumTopologyHitResolution DatumTopologyResolver::resolveHitAtPoint(
    const std::vector<DatumTopologyReference>& references,
    const std::string& objectId,
    const TopoDS_Shape& shape,
    DatumSubshapeType subshapeType,
    const Vector3& localPointMm,
    const std::optional<Vector3>& localNormal,
    double toleranceMm,
    const DatumTopologyResolveOptions& options)
{
    if (!isDatumUuid(objectId)) {
        throw std::invalid_argument("topology hit resolution requires a valid object UUID");
    }
    if (!finiteVector(localPointMm)) {
        throw std::invalid_argument("topology hit position must contain finite millimetre values");
    }
    if (!std::isfinite(toleranceMm) || toleranceMm <= 0.0) {
        throw std::invalid_argument("topology hit tolerance must be finite and greater than zero");
    }
    if (localNormal.has_value()) {
        if (!finiteVector(*localNormal)) {
            throw std::invalid_argument("topology hit normal must contain finite values");
        }
        const double lengthSquared = (*localNormal)[0] * (*localNormal)[0] +
                                     (*localNormal)[1] * (*localNormal)[1] +
                                     (*localNormal)[2] * (*localNormal)[2];
        if (lengthSquared <= 1e-24) {
            throw std::invalid_argument("topology hit normal must be non-zero");
        }
    }
    if (!std::isfinite(options.maximumRelativeMeasureError) || options.maximumRelativeMeasureError < 0.0 ||
        !std::isfinite(options.maximumCentroidDistanceRatio) || options.maximumCentroidDistanceRatio < 0.0 ||
        !std::isfinite(options.maximumScore) || options.maximumScore < 0.0 ||
        !std::isfinite(options.ambiguityTolerance) || options.ambiguityTolerance < 0.0) {
        throw std::invalid_argument("topology resolver tolerances must be finite and non-negative");
    }

    DatumTopologyHitResolution result;
    if (shape.IsNull()) {
        result.status = DatumTopologyResolutionStatus::NotFound;
        return result;
    }
    TopAbs_ShapeEnum shapeType = TopAbs_SHAPE;
    switch (subshapeType) {
    case DatumSubshapeType::Face:
        shapeType = TopAbs_FACE;
        break;
    case DatumSubshapeType::Edge:
        shapeType = TopAbs_EDGE;
        break;
    case DatumSubshapeType::Vertex:
        shapeType = TopAbs_VERTEX;
        break;
    default:
        throw std::invalid_argument("topology hit resolution received an unsupported subshape type");
    }

    bool hasUnsupportedFeatureReference = false;
    std::vector<const DatumTopologyReference*> objectReferences;
    for (const DatumTopologyReference& reference : references) {
        if (reference.objectId != objectId || reference.subshapeType != subshapeType) {
            continue;
        }
        if (reference.featureId.has_value()) {
            hasUnsupportedFeatureReference = true;
        } else {
            objectReferences.push_back(&reference);
        }
    }
    if (objectReferences.empty()) {
        result.status = hasUnsupportedFeatureReference
            ? DatumTopologyResolutionStatus::UnsupportedFeature
            : DatumTopologyResolutionStatus::NotFound;
        return result;
    }

    TopTools_IndexedMapOfShape candidates;
    TopExp::MapShapes(shape, shapeType, candidates);
    if (candidates.IsEmpty()) {
        result.status = DatumTopologyResolutionStatus::NotFound;
        return result;
    }
    std::vector<DatumTopologyReference> candidateReferences;
    for (DatumTopologyReference& candidate : captureAll(objectId, shape)) {
        if (candidate.subshapeType == subshapeType) {
            candidateReferences.push_back(std::move(candidate));
        }
    }
    if (candidateReferences.size() != static_cast<std::size_t>(candidates.Extent())) {
        throw std::logic_error("OCCT topology candidate order did not match its generated signatures");
    }

    const TopoDS_Vertex hitVertex = BRepBuilderAPI_MakeVertex(
        gp_Pnt(localPointMm[0], localPointMm[1], localPointMm[2])).Vertex();
    struct ScoredHit {
        std::size_t index;
        double distanceMm;
        double score;
    };
    std::vector<ScoredHit> hits;
    hits.reserve(candidateReferences.size());
    for (std::size_t index = 0; index < candidateReferences.size(); ++index) {
        BRepExtrema_DistShapeShape distanceQuery(hitVertex,
            candidates(static_cast<Standard_Integer>(index + 1)));
        distanceQuery.Perform();
        if (!distanceQuery.IsDone()) {
            continue;
        }
        const double distanceMm = distanceQuery.Value();
        if (!std::isfinite(distanceMm) || distanceMm > toleranceMm) {
            continue;
        }
        double score = distanceMm / toleranceMm;
        const DatumTopologyGeometrySignature& geometry = candidateReferences[index].geometrySignature;
        if (subshapeType == DatumSubshapeType::Face && localNormal.has_value() &&
            geometry.geometryType == "plane") {
            score += directionMismatch(geometry.direction, localNormal) * 0.5;
        }
        hits.push_back({index, distanceMm, score});
    }
    if (hits.empty()) {
        result.status = DatumTopologyResolutionStatus::NotFound;
        return result;
    }
    std::sort(hits.begin(), hits.end(), [](const ScoredHit& left, const ScoredHit& right) {
        return left.score < right.score;
    });
    const double hitAmbiguityTolerance = std::max(options.ambiguityTolerance, 1e-6);
    if (hits.size() > 1 && std::abs(hits[0].score - hits[1].score) <= hitAmbiguityTolerance) {
        result.status = DatumTopologyResolutionStatus::Ambiguous;
        return result;
    }
    const std::size_t pickedCandidateIndex = hits.front().index;
    result.transientIndex = pickedCandidateIndex;
    result.distanceMm = hits.front().distanceMm;

    const double diagonal = boundingDiagonal(shape);
    const double matchAmbiguityTolerance = std::max(options.ambiguityTolerance, 1e-8);
    bool ambiguousReferenceMatch = false;
    std::vector<std::string> matchingReferenceIds;
    for (const DatumTopologyReference* reference : objectReferences) {
        std::vector<std::pair<std::size_t, double>> scores;
        scores.reserve(candidateReferences.size());
        for (std::size_t index = 0; index < candidateReferences.size(); ++index) {
            const DatumTopologyReference& candidate = candidateReferences[index];
            if (candidate.geometrySignature.geometryType != reference->geometrySignature.geometryType) {
                continue;
            }
            const double measureError = relativeError(reference->geometrySignature.measure,
                                                      candidate.geometrySignature.measure);
            const double centroidRatio = distance(reference->geometrySignature.centroidMm,
                                                  candidate.geometrySignature.centroidMm) / diagonal;
            if (measureError > options.maximumRelativeMeasureError ||
                centroidRatio > options.maximumCentroidDistanceRatio) {
                continue;
            }
            const double score = candidateScore(*reference, candidate.geometrySignature, diagonal,
                                                candidate.topologySignature);
            if (score <= options.maximumScore) {
                scores.emplace_back(index, score);
            }
        }
        std::sort(scores.begin(), scores.end(), [](const auto& left, const auto& right) {
            return left.second < right.second;
        });
        if (scores.empty()) {
            continue;
        }
        if (scores.size() > 1 && std::abs(scores[0].second - scores[1].second) <= matchAmbiguityTolerance) {
            for (const auto& [index, score] : scores) {
                if (score - scores[0].second > matchAmbiguityTolerance) {
                    break;
                }
                if (index == pickedCandidateIndex) {
                    ambiguousReferenceMatch = true;
                    break;
                }
            }
            continue;
        }
        if (scores[0].first == pickedCandidateIndex) {
            matchingReferenceIds.push_back(reference->id);
        }
    }
    if (matchingReferenceIds.size() == 1 && !ambiguousReferenceMatch) {
        result.status = DatumTopologyResolutionStatus::Resolved;
        result.referenceId = std::move(matchingReferenceIds.front());
    } else if (matchingReferenceIds.size() > 1 || ambiguousReferenceMatch) {
        result.status = DatumTopologyResolutionStatus::Ambiguous;
    } else {
        result.status = DatumTopologyResolutionStatus::NotFound;
    }
    return result;
}

}  // namespace datumloop
