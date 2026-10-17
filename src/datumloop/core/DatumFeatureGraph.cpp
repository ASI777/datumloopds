#include "datumloop/core/DatumFeatureGraph.hpp"

#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <Standard_Failure.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace datumloop {
namespace {

void requireParameters(const DatumFeature& feature,
                       std::initializer_list<const char*> names)
{
    if (feature.parameters.size() != names.size()) {
        throw std::invalid_argument("feature parameter set does not match its type");
    }
    for (const char* name : names) {
        const auto found = feature.parameters.find(name);
        if (found == feature.parameters.end() || !std::isfinite(found->second)) {
            throw std::invalid_argument(std::string("feature parameter '") + name + "' is missing or non-finite");
        }
    }
}

double parameter(const DatumFeature& feature, const char* name)
{
    return feature.parameters.at(name);
}

void requireInputCount(const DatumFeature& feature, std::size_t count)
{
    if (feature.inputFeatureIds.size() != count) {
        throw std::invalid_argument("feature has the wrong number of inputs for its type");
    }
}

TopoDS_Shape buildBoolean(const DatumFeature& feature,
                          const std::unordered_map<std::string, TopoDS_Shape>& priorShapes)
{
    const TopoDS_Shape& left = priorShapes.at(feature.inputFeatureIds[0]);
    const TopoDS_Shape& right = priorShapes.at(feature.inputFeatureIds[1]);
    switch (feature.type) {
    case DatumFeatureType::BooleanUnion: {
        BRepAlgoAPI_Fuse operation(left, right);
        if (!operation.IsDone()) {
            throw std::runtime_error("OCCT Boolean union did not complete");
        }
        return operation.Shape();
    }
    case DatumFeatureType::BooleanCut: {
        BRepAlgoAPI_Cut operation(left, right);
        if (!operation.IsDone()) {
            throw std::runtime_error("OCCT Boolean cut did not complete");
        }
        return operation.Shape();
    }
    case DatumFeatureType::BooleanIntersection: {
        BRepAlgoAPI_Common operation(left, right);
        if (!operation.IsDone()) {
            throw std::runtime_error("OCCT Boolean intersection did not complete");
        }
        return operation.Shape();
    }
    default:
        throw std::logic_error("non-Boolean feature passed to the Boolean builder");
    }
}

TopoDS_Shape evaluate(const DatumFeature& feature,
                      const std::unordered_map<std::string, TopoDS_Shape>& priorShapes)
{
    TopoDS_Shape shape;
    switch (feature.type) {
    case DatumFeatureType::Box:
    case DatumFeatureType::ExtrudeRectangle:
        requireInputCount(feature, 0);
        requireParameters(feature, {"xMm", "yMm", "zMm"});
        if (parameter(feature, "xMm") <= 0.0 || parameter(feature, "yMm") <= 0.0 ||
            parameter(feature, "zMm") <= 0.0) {
            throw std::invalid_argument("box dimensions must be greater than zero");
        }
        shape = BRepPrimAPI_MakeBox(parameter(feature, "xMm"),
                                    parameter(feature, "yMm"),
                                    parameter(feature, "zMm")).Shape();
        // Match DatumLoop's centered box convention.
        {
            gp_Trsf centered;
            centered.SetTranslation(gp_Vec(-parameter(feature, "xMm") * 0.5,
                                           -parameter(feature, "yMm") * 0.5,
                                           -parameter(feature, "zMm") * 0.5));
            shape = BRepBuilderAPI_Transform(shape, centered, Standard_True).Shape();
        }
        break;
    case DatumFeatureType::ThroughHole: {
        requireInputCount(feature, 1);
        requireParameters(feature, {"xMm", "yMm", "diameterMm"});
        const double radius = parameter(feature, "diameterMm") * 0.5;
        if (radius <= 0.0) throw std::invalid_argument("through-hole diameter must be positive");
        const auto& parent = priorShapes.at(feature.inputFeatureIds.front());
        const auto before = OcctKernel::measure(parent);
        const double depth = before.maximum[2] - before.minimum[2];
        const double margin = std::max(1e-4, depth * 1e-4);
        const auto tool = BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(parameter(feature, "xMm"), parameter(feature, "yMm"), before.minimum[2] - margin),
                   gp_Dir(0, 0, 1)), radius, depth + 2 * margin).Shape();
        BRepAlgoAPI_Cut cut(parent, tool);
        if (!cut.IsDone()) throw std::runtime_error("OCCT through-hole operation failed");
        shape = cut.Shape();
        const double expectedRemoval = std::acos(-1.0) * radius * radius * depth;
        const double removed = before.volume - OcctKernel::measure(shape).volume;
        if (std::abs(removed - expectedRemoval) > expectedRemoval * 1e-7) {
            throw std::invalid_argument("initial through-hole feature requires a complete constant-depth bore; overlapping holes and edge slots are unsupported");
        }
        break;
    }
    case DatumFeatureType::Cylinder:
        requireInputCount(feature, 0);
        requireParameters(feature, {"radiusMm", "heightMm"});
        if (parameter(feature, "radiusMm") <= 0.0 || parameter(feature, "heightMm") <= 0.0) {
            throw std::invalid_argument("cylinder radius and height must be greater than zero");
        }
        shape = BRepPrimAPI_MakeCylinder(parameter(feature, "radiusMm"),
                                         parameter(feature, "heightMm")).Shape();
        break;
    case DatumFeatureType::Sphere:
        requireInputCount(feature, 0);
        requireParameters(feature, {"radiusMm"});
        if (parameter(feature, "radiusMm") <= 0.0) {
            throw std::invalid_argument("sphere radius must be greater than zero");
        }
        shape = BRepPrimAPI_MakeSphere(parameter(feature, "radiusMm")).Shape();
        break;
    case DatumFeatureType::Cone:
        requireInputCount(feature, 0);
        requireParameters(feature, {"bottomRadiusMm", "topRadiusMm", "heightMm"});
        if (parameter(feature, "bottomRadiusMm") <= 0.0 || parameter(feature, "topRadiusMm") < 0.0 ||
            parameter(feature, "heightMm") <= 0.0 ||
            parameter(feature, "bottomRadiusMm") + parameter(feature, "topRadiusMm") <= 0.0) {
            throw std::invalid_argument("cone radii and height are outside the supported range");
        }
        shape = BRepPrimAPI_MakeCone(parameter(feature, "bottomRadiusMm"),
                                     parameter(feature, "topRadiusMm"),
                                     parameter(feature, "heightMm")).Shape();
        break;
    case DatumFeatureType::Translate: {
        requireInputCount(feature, 1);
        requireParameters(feature, {"xMm", "yMm", "zMm"});
        gp_Trsf transform;
        transform.SetTranslation(gp_Vec(parameter(feature, "xMm"),
                                        parameter(feature, "yMm"),
                                        parameter(feature, "zMm")));
        shape = BRepBuilderAPI_Transform(priorShapes.at(feature.inputFeatureIds[0]),
                                         transform, Standard_True).Shape();
        break;
    }
    case DatumFeatureType::BooleanUnion:
    case DatumFeatureType::BooleanCut:
    case DatumFeatureType::BooleanIntersection:
        requireInputCount(feature, 2);
        requireParameters(feature, {});
        shape = buildBoolean(feature, priorShapes);
        break;
    }

    if (!OcctKernel::isValid(shape)) {
        throw std::runtime_error("feature did not produce a valid exact OCCT B-Rep");
    }
    return shape;
}

}  // namespace

std::string datumFeatureTypeName(DatumFeatureType type)
{
    switch (type) {
    case DatumFeatureType::Box: return "primitive.box";
    case DatumFeatureType::Cylinder: return "primitive.cylinder";
    case DatumFeatureType::Sphere: return "primitive.sphere";
    case DatumFeatureType::Cone: return "primitive.cone";
    case DatumFeatureType::Translate: return "transform.translate";
    case DatumFeatureType::BooleanUnion: return "boolean.union";
    case DatumFeatureType::BooleanCut: return "boolean.cut";
    case DatumFeatureType::BooleanIntersection: return "boolean.intersection";
    case DatumFeatureType::ExtrudeRectangle: return "feature.extrude-rectangle";
    case DatumFeatureType::ThroughHole: return "feature.through-hole";
    }
    throw std::invalid_argument("unknown DatumLoop feature type");
}

std::optional<DatumFeatureType> datumFeatureTypeFromName(const std::string& name)
{
    if (name == "primitive.box") return DatumFeatureType::Box;
    if (name == "primitive.cylinder") return DatumFeatureType::Cylinder;
    if (name == "primitive.sphere") return DatumFeatureType::Sphere;
    if (name == "primitive.cone") return DatumFeatureType::Cone;
    if (name == "transform.translate") return DatumFeatureType::Translate;
    if (name == "boolean.union") return DatumFeatureType::BooleanUnion;
    if (name == "boolean.cut") return DatumFeatureType::BooleanCut;
    if (name == "boolean.intersection") return DatumFeatureType::BooleanIntersection;
    if (name == "feature.extrude-rectangle") return DatumFeatureType::ExtrudeRectangle;
    if (name == "feature.through-hole") return DatumFeatureType::ThroughHole;
    return std::nullopt;
}

std::string datumFeatureStatusName(DatumFeatureStatus status)
{
    switch (status) {
    case DatumFeatureStatus::Succeeded: return "succeeded";
    case DatumFeatureStatus::Failed: return "failed";
    }
    throw std::invalid_argument("unknown DatumLoop feature status");
}

DatumFeatureGraph::DatumFeatureGraph(std::string ownerObjectId,
                                     std::vector<DatumFeature> features,
                                     std::optional<std::string> outputFeatureId)
    : ownerObjectId_(std::move(ownerObjectId)),
      features_(std::move(features)),
      outputFeatureId_(std::move(outputFeatureId))
{
    recompute();
}

const DatumFeature* DatumFeatureGraph::feature(const std::string& id) const noexcept
{
    const auto found = std::find_if(features_.begin(), features_.end(), [&id](const DatumFeature& item) {
        return item.id == id;
    });
    return found == features_.end() ? nullptr : &*found;
}

void DatumFeatureGraph::recompute()
{
    if (!ownerObjectId_.empty() && !isDatumUuid(ownerObjectId_)) {
        throw std::invalid_argument("feature graph owner must be a UUID");
    }
    outputShape_.Nullify();
    std::unordered_map<std::string, std::size_t> indices;
    indices.reserve(features_.size());
    for (std::size_t index = 0; index < features_.size(); ++index) {
        DatumFeature& feature = features_[index];
        if (!isDatumUuid(feature.id) || (!ownerObjectId_.empty() && feature.ownerObjectId != ownerObjectId_) ||
            !isDatumUuid(feature.ownerObjectId) ||
            feature.kernelBackend.find_first_not_of(" \t\n\r") == std::string::npos ||
            !indices.emplace(feature.id, index).second) {
            throw std::invalid_argument("feature IDs must be unique UUIDs owned by the graph's object");
        }
        for (const auto& [name, value] : feature.parameters) {
            if (name.find_first_not_of(" \t\n\r") == std::string::npos || !std::isfinite(value)) {
                throw std::invalid_argument("feature parameter names and values must be non-empty and finite");
            }
        }
        if (feature.type == DatumFeatureType::ExtrudeRectangle || feature.type == DatumFeatureType::ThroughHole) {
            if (feature.name.empty() || feature.semanticRole.empty() ||
                feature.parameterDefinitions.size() != feature.parameters.size()) {
                throw std::invalid_argument("engineering features require names, roles and parameter provenance");
            }
        }
        for (const auto& [name, definition] : feature.parameterDefinitions) {
            if (!feature.parameters.contains(name) || definition.unit != "mm" ||
                !isDatumUuid(definition.partRequirementsId) || definition.designVariableIds.empty() ||
                definition.relationship.empty() || definition.sourceValues.size() != definition.designVariableIds.size()) {
                throw std::invalid_argument("feature parameter provenance is incomplete or unsupported");
            }
            for (const auto& key : definition.designVariableIds) {
                if (key.empty() || !definition.sourceValues.contains(key) ||
                    !std::isfinite(definition.sourceValues.at(key))) {
                    throw std::invalid_argument("missing or non-finite feature source parameter snapshot");
                }
            }
        }
    }
    if (outputFeatureId_.has_value() && !indices.contains(*outputFeatureId_)) {
        throw std::invalid_argument("feature graph output must reference a feature in the graph");
    }

    std::unordered_map<std::string, TopoDS_Shape> generated;
    generated.reserve(features_.size());
    for (std::size_t index = 0; index < features_.size(); ++index) {
        DatumFeature& feature = features_[index];
        feature.generatedShape.Nullify();
        feature.status = DatumFeatureStatus::Failed;
        feature.error.clear();
        std::unordered_set<std::string> uniqueInputs;
        bool dependencyFailed = false;
        for (const std::string& inputId : feature.inputFeatureIds) {
            const auto input = indices.find(inputId);
            if (input == indices.end() || input->second >= index || !uniqueInputs.insert(inputId).second) {
                throw std::invalid_argument("feature inputs must be unique references to earlier features");
            }
            if (features_[input->second].status != DatumFeatureStatus::Succeeded) {
                dependencyFailed = true;
            }
        }
        if (dependencyFailed) {
            feature.error = "One or more input features failed.";
            continue;
        }
        if (feature.kernelBackend != "occt") {
            feature.error = "Unsupported geometry backend '" + feature.kernelBackend + "'.";
            continue;
        }
        try {
            feature.generatedShape = evaluate(feature, generated);
            feature.status = DatumFeatureStatus::Succeeded;
            generated.emplace(feature.id, feature.generatedShape);
        } catch (const Standard_Failure& error) {
            const char* message = error.GetMessageString();
            feature.error = message == nullptr ? "OCCT feature operation failed." : message;
        } catch (const std::exception& error) {
            feature.error = error.what();
        }
    }
    if (outputFeatureId_.has_value()) {
        const DatumFeature* output = feature(*outputFeatureId_);
        if (output != nullptr && output->status == DatumFeatureStatus::Succeeded) {
            outputShape_ = output->generatedShape;
        }
    }
}

}  // namespace datumloop
