#include "datumloop/core/DatumDocument.hpp"

#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/topology/DatumTopologyResolver.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace datumloop {
namespace {

bool finite(const Vector3& value)
{
    return std::all_of(value.begin(), value.end(), [](double component) {
        return std::isfinite(component);
    });
}

bool hasEngineeringIdentity(const DatumEngineeringModel& model, const std::string& id)
{
    for (const DatumMechanism& mechanism : model.mechanisms) {
        if (mechanism.id == id ||
            std::any_of(mechanism.bodies.begin(), mechanism.bodies.end(), [&id](const auto& body) {
                return body.id == id;
            }) ||
            std::any_of(mechanism.joints.begin(), mechanism.joints.end(), [&id](const auto& joint) {
                return joint.id == id;
            }) ||
            std::any_of(mechanism.actuators.begin(), mechanism.actuators.end(), [&id](const auto& actuator) {
                return actuator.id == id;
            }) ||
            std::any_of(mechanism.contacts.begin(), mechanism.contacts.end(), [&id](const auto& contact) {
                return contact.id == id;
            }) ||
            std::any_of(mechanism.requirements.begin(), mechanism.requirements.end(), [&id](const auto& requirement) {
                return requirement.id == id;
            }) ||
            std::any_of(mechanism.axes.begin(), mechanism.axes.end(), [&id](const auto& axis) {
                return axis.id == id;
            })) {
            return true;
        }
    }
    if (std::any_of(model.motors.begin(), model.motors.end(), [&id](const auto& motor) {
            return motor.id == id;
        })) {
        return true;
    }
    for (const StructuralAnalysisStudy& study : model.structuralStudies) {
        if (study.id == id ||
            std::any_of(study.boundaryConditions.begin(), study.boundaryConditions.end(),
                        [&id](const BoundaryCondition& condition) {
                            return boundaryConditionId(condition) == id;
                        })) {
            return true;
        }
        for (const StructuralAnalysisRun& run : study.runs) {
            if (run.provenance.solverRunId == id || run.provenance.mesh.revisionId == id ||
                std::any_of(run.fields.begin(), run.fields.end(), [&id](const FieldDataset& field) {
                    return field.id == id;
                })) {
                return true;
            }
        }
    }
    return false;
}

void reserveEngineeringIdentities(const DatumEngineeringModel& model,
                                 std::unordered_set<std::string>& reservedIds)
{
    for (const DatumMechanism& mechanism : model.mechanisms) {
        reservedIds.insert(mechanism.id);
        for (const DatumMechanismBody& body : mechanism.bodies) reservedIds.insert(body.id);
        for (const DatumJoint& joint : mechanism.joints) reservedIds.insert(joint.id);
        for (const DatumActuator& actuator : mechanism.actuators) reservedIds.insert(actuator.id);
        for (const DatumContactPair& contact : mechanism.contacts) reservedIds.insert(contact.id);
        for (const DatumRequirement& requirement : mechanism.requirements) reservedIds.insert(requirement.id);
        for (const DatumAxis& axis : mechanism.axes) {
            reservedIds.insert(axis.id);
        }
    }
    for (const DatumMotor& motor : model.motors) {
        reservedIds.insert(motor.id);
    }
    for (const StructuralAnalysisStudy& study : model.structuralStudies) {
        reservedIds.insert(study.id);
        for (const BoundaryCondition& condition : study.boundaryConditions) {
            reservedIds.insert(boundaryConditionId(condition));
        }
        for (const StructuralAnalysisRun& run : study.runs) {
            reservedIds.insert(run.provenance.solverRunId);
            reservedIds.insert(run.provenance.mesh.revisionId);
            for (const FieldDataset& field : run.fields) reservedIds.insert(field.id);
        }
    }
}

void reserveElectronicsIdentities(const DatumElectronicsModel& model,
                                  std::unordered_set<std::string>& reservedIds)
{
    const auto reserve = [&reservedIds](const std::string& id) {
        if (!reservedIds.insert(id).second) {
            throw std::invalid_argument("electronics entity UUID collides with an existing document identity");
        }
    };
    for (const MaterialDefinition& material : model.materials) {
        reserve(material.id);
        for (const MaterialSource& source : material.sources) reserve(source.id);
    }
    for (const MaterialState& state : model.materialStates) reserve(state.id);
    for (const CircuitDocument& circuit : model.circuits) {
        reserve(circuit.id);
        for (const CircuitComponent& component : circuit.components) reserve(component.id);
        for (const CircuitPin& pin : circuit.pins) reserve(pin.id);
        for (const CircuitNet& net : circuit.nets) reserve(net.id);
    }
    for (const PcbDocument& pcb : model.pcbs) {
        reserve(pcb.id);
        reserve(pcb.stackup.id);
        for (const PcbLayer& layer : pcb.stackup.layers) reserve(layer.id);
        for (const PcbComponent& component : pcb.components) reserve(component.id);
        for (const PcbTrace& trace : pcb.traces) reserve(trace.id);
        for (const PcbVia& via : pcb.vias) reserve(via.id);
        for (const PcbPad& pad : pcb.pads) reserve(pad.id);
        for (const ElectronicMaterialRegion& region : pcb.materialRegions) reserve(region.id);
    }
}

}  // namespace

DatumDocument::DatumDocument(std::string name, std::string projectId)
    : name_(std::move(name)), projectId_(projectId.empty() ? createDatumUuidV4() : std::move(projectId))
{
    if (name_.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("project name must not be empty");
    }
    if (!isDatumUuid(projectId_)) {
        throw std::invalid_argument("project id must be a UUID");
    }
}

void DatumDocument::setName(std::string name)
{
    if (name.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("project name must not be empty");
    }
    if (name_ != name) {
        name_ = std::move(name);
        ++revision_;
        dirty_ = true;
    }
}

void DatumDocument::clear(std::string name)
{
    if (name.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("project name must not be empty");
    }
    name_ = std::move(name);
    projectId_ = createDatumUuidV4();
    objects_.clear();
    topologyReferences_.clear();
    engineeringModel_ = {};
    electronicsModel_ = {};
    ++revision_;
    dirty_ = true;
}

void DatumDocument::replaceWith(DatumDocument replacement)
{
    name_ = std::move(replacement.name_);
    projectId_ = std::move(replacement.projectId_);
    objects_ = std::move(replacement.objects_);
    topologyReferences_ = std::move(replacement.topologyReferences_);
    engineeringModel_ = std::move(replacement.engineeringModel_);
    electronicsModel_ = std::move(replacement.electronicsModel_);
    ++revision_;
    dirty_ = replacement.dirty_;
}

const DatumObject& DatumDocument::createBox(std::string name, const Vector3& dimensions)
{
    if (name.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("object name must not be empty");
    }
    const std::string objectId = createDatumUuidV4();
    const std::string featureId = createDatumUuidV4();
    (void)addObject(objectId, std::move(name), OcctKernel::box(dimensions),
                    {0.0, 0.0, 0.0}, std::nullopt, false);
    DatumFeature feature;
    feature.id = featureId;
    feature.ownerObjectId = objectId;
    feature.type = DatumFeatureType::Box;
    feature.parameters = {{"xMm", dimensions[0]}, {"yMm", dimensions[1]}, {"zMm", dimensions[2]}};
    DatumFeatureGraph graph(objectId, {std::move(feature)}, featureId);
    setFeatureGraph(objectId, std::move(graph));
    return *object(objectId);
}

const DatumObject& DatumDocument::addObject(std::string id,
                                            std::string name,
                                            TopoDS_Shape shape,
                                            const Vector3& position,
                                            std::optional<std::string> parentId,
                                            bool captureTopologyReferences)
{
    if (!isDatumUuid(id)) {
        throw std::invalid_argument("object id must be a UUID");
    }
    if (name.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("object name must not be empty");
    }
    if (object(id) != nullptr ||
        std::any_of(objects_.begin(), objects_.end(), [&id](const DatumObject& existing) {
            return std::any_of(existing.featureGraph.features().begin(), existing.featureGraph.features().end(),
                               [&id](const DatumFeature& feature) { return feature.id == id; });
        }) ||
        std::any_of(topologyReferences_.begin(), topologyReferences_.end(), [&id](const auto& reference) {
            return reference.id == id;
        }) || hasEngineeringIdentity(engineeringModel_, id) || electronicsModel_.containsIdentity(id)) {
        throw std::invalid_argument("object id must be unique within a project");
    }
    if (id == projectId_) {
        throw std::invalid_argument("object id must be distinct from the project id");
    }
    if (parentId.has_value()) {
        if (!isDatumUuid(*parentId) || object(*parentId) == nullptr) {
            throw std::invalid_argument("object parent must reference an existing object UUID");
        }
    }
    if (!finite(position)) {
        throw std::invalid_argument("object position must contain finite millimetre values");
    }

    DatumObject object;
    object.id = std::move(id);
    object.name = std::move(name);
    object.parentId = std::move(parentId);
    object.shape = std::move(shape);
    object.metrics = OcctKernel::measure(object.shape);
    object.position = position;
    if (!object.metrics.valid) {
        throw std::invalid_argument("object shape must be a valid OCCT B-Rep");
    }
    std::vector<DatumTopologyReference> generatedReferences;
    if (captureTopologyReferences) {
        generatedReferences = DatumTopologyResolver::captureAll(object.id, object.shape);
        std::unordered_set<std::string> reservedIds;
        reservedIds.reserve(1 + objects_.size() + topologyReferences_.size() + generatedReferences.size());
        reservedIds.insert(projectId_);
        reservedIds.insert(object.id);
        for (const DatumObject& existing : objects_) {
            reservedIds.insert(existing.id);
            for (const DatumFeature& feature : existing.featureGraph.features()) {
                reservedIds.insert(feature.id);
            }
        }
        for (const DatumTopologyReference& existing : topologyReferences_) {
            reservedIds.insert(existing.id);
        }
        reserveEngineeringIdentities(engineeringModel_, reservedIds);
        for (const DatumTopologyReference& reference : generatedReferences) {
            if (!reservedIds.insert(reference.id).second) {
                throw std::runtime_error("generated topology reference UUID is not unique within the document");
            }
        }
    }
    objects_.reserve(objects_.size() + 1);
    topologyReferences_.reserve(topologyReferences_.size() + generatedReferences.size());
    objects_.push_back(std::move(object));
    topologyReferences_.insert(topologyReferences_.end(),
                               std::make_move_iterator(generatedReferences.begin()),
                               std::make_move_iterator(generatedReferences.end()));
    ++revision_;
    dirty_ = true;
    return objects_.back();
}

const DatumObject* DatumDocument::object(const std::string& id) const noexcept
{
    const auto found = std::find_if(objects_.begin(), objects_.end(), [&id](const DatumObject& object) {
        return object.id == id;
    });
    return found == objects_.end() ? nullptr : &*found;
}

bool DatumDocument::setPosition(const std::string& id, const Vector3& position) noexcept
{
    if (!finite(position)) {
        return false;
    }
    const auto found = std::find_if(objects_.begin(), objects_.end(), [&id](const DatumObject& object) {
        return object.id == id;
    });
    if (found == objects_.end()) {
        return false;
    }
    if (found->position != position) {
        found->position = position;
        ++revision_;
        dirty_ = true;
    }
    return true;
}

void DatumDocument::setProperty(const std::string& id, std::string key, DatumPropertyValue value)
{
    if (key.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("property key must not be empty");
    }
    if (const auto* number = std::get_if<double>(&value); number != nullptr && !std::isfinite(*number)) {
        throw std::invalid_argument("floating-point properties must be finite");
    }
    const auto found = std::find_if(objects_.begin(), objects_.end(), [&id](const DatumObject& object) {
        return object.id == id;
    });
    if (found == objects_.end()) {
        throw std::invalid_argument("property references an unknown object id");
    }
    const auto existing = found->properties.find(key);
    if (existing == found->properties.end() || existing->second != value) {
        found->properties.insert_or_assign(std::move(key), std::move(value));
        ++revision_;
        dirty_ = true;
    }
}

const DatumTopologyReference& DatumDocument::addTopologyReference(DatumTopologyReference reference)
{
    if (!isDatumUuid(reference.id) || !isDatumUuid(reference.objectId)) {
        throw std::invalid_argument("topology reference and owner ids must be UUIDs");
    }
    const bool featureIdCollision = std::any_of(objects_.begin(), objects_.end(), [&reference](const DatumObject& item) {
        return std::any_of(item.featureGraph.features().begin(), item.featureGraph.features().end(),
                           [&reference](const DatumFeature& feature) { return feature.id == reference.id; });
    });
    if (reference.id == projectId_ || object(reference.id) != nullptr || featureIdCollision ||
        hasEngineeringIdentity(engineeringModel_, reference.id) || electronicsModel_.containsIdentity(reference.id) ||
        std::any_of(topologyReferences_.begin(), topologyReferences_.end(), [&](const auto& existing) {
            return existing.id == reference.id;
        })) {
        throw std::invalid_argument("topology reference id must be unique within a project");
    }
    if (object(reference.objectId) == nullptr) {
        throw std::invalid_argument("topology reference must belong to an existing object");
    }
    if (reference.featureId.has_value() && !isDatumUuid(*reference.featureId)) {
        throw std::invalid_argument("topology reference feature id must be a UUID");
    }
    try {
        (void)datumSubshapeTypeName(reference.subshapeType);
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("topology reference subshape type is invalid");
    }
    if (reference.topologySignature.empty() || reference.geometrySignature.geometryType.empty() ||
        !std::isfinite(reference.geometrySignature.measure) || reference.geometrySignature.measure < 0.0 ||
        !finite(reference.geometrySignature.centroidMm)) {
        throw std::invalid_argument("topology reference signature data is incomplete or invalid");
    }
    if (reference.geometrySignature.direction.has_value()) {
        if (!finite(*reference.geometrySignature.direction)) {
            throw std::invalid_argument("topology reference direction must be finite");
        }
        const double lengthSquared = (*reference.geometrySignature.direction)[0] * (*reference.geometrySignature.direction)[0] +
                                     (*reference.geometrySignature.direction)[1] * (*reference.geometrySignature.direction)[1] +
                                     (*reference.geometrySignature.direction)[2] * (*reference.geometrySignature.direction)[2];
        if (lengthSquared <= 1e-24) {
            throw std::invalid_argument("topology reference direction must be non-zero");
        }
    }
    for (const DatumTopologyNeighborSignature& neighbor : reference.geometrySignature.adjacent) {
        if (neighbor.geometryType.empty() || !std::isfinite(neighbor.measure) || neighbor.measure < 0.0) {
            throw std::invalid_argument("topology reference adjacency data is invalid");
        }
        try {
            (void)datumSubshapeTypeName(neighbor.subshapeType);
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument("topology reference adjacency type is invalid");
        }
    }
    std::vector<std::string> uniqueParents;
    uniqueParents.reserve(reference.parentReferenceIds.size());
    for (const std::string& parentId : reference.parentReferenceIds) {
        if (!isDatumUuid(parentId) || parentId == reference.id ||
            std::find(uniqueParents.begin(), uniqueParents.end(), parentId) != uniqueParents.end()) {
            throw std::invalid_argument("topology reference parent ids must be unique UUIDs");
        }
        const auto parent = std::find_if(topologyReferences_.begin(), topologyReferences_.end(),
                                         [&parentId](const auto& existing) { return existing.id == parentId; });
        if (parent == topologyReferences_.end() || parent->objectId != reference.objectId ||
            parent->featureId != reference.featureId) {
            throw std::invalid_argument("topology reference parent must already exist on the same owner and feature");
        }
        uniqueParents.push_back(parentId);
    }
    topologyReferences_.push_back(std::move(reference));
    ++revision_;
    dirty_ = true;
    return topologyReferences_.back();
}

void DatumDocument::setEngineeringModel(DatumEngineeringModel model)
{
    model.validate();
    std::unordered_set<std::string> reservedIds;
    reservedIds.reserve(1 + objects_.size() + topologyReferences_.size());
    reservedIds.insert(projectId_);
    for (const DatumObject& item : objects_) {
        reservedIds.insert(item.id);
        for (const DatumFeature& feature : item.featureGraph.features()) {
            reservedIds.insert(feature.id);
        }
    }
    for (const DatumTopologyReference& reference : topologyReferences_) {
        reservedIds.insert(reference.id);
    }
    reserveElectronicsIdentities(electronicsModel_, reservedIds);
    for (const DatumMechanism& mechanism : model.mechanisms) {
        if (!reservedIds.insert(mechanism.id).second) {
            throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
        }
        for (const std::string& bodyObjectId : mechanism.bodyObjectIds) {
            if (object(bodyObjectId) == nullptr) {
                throw std::invalid_argument("mechanism bodyObjectIds must reference existing CAD objects");
            }
        }
        for (const DatumMechanismBody& body : mechanism.bodies) {
            if (!reservedIds.insert(body.id).second) {
                throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
            }
            if (object(body.cadObjectId) == nullptr) {
                throw std::invalid_argument("mechanism body geometry source must reference an existing CAD object");
            }
            if (body.geometrySource.kind == DatumGeometrySourceKind::NativeFeatureGraph) {
                const DatumObject* cadObject = object(body.cadObjectId);
                const bool sourceFeatureExists = cadObject != nullptr &&
                    std::any_of(cadObject->featureGraph.features().begin(), cadObject->featureGraph.features().end(),
                                [&body](const DatumFeature& feature) {
                                    return feature.id == body.geometrySource.locator;
                                });
                if (!sourceFeatureExists) {
                    throw std::invalid_argument("native mechanism body source must reference a feature in its CAD object's feature graph");
                }
            }
        }
        for (const DatumAxis& axis : mechanism.axes) {
            if (!reservedIds.insert(axis.id).second) {
                throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
            }
        }
        for (const DatumJoint& joint : mechanism.joints) {
            if (!reservedIds.insert(joint.id).second) {
                throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
            }
        }
        for (const DatumActuator& actuator : mechanism.actuators) {
            if (!reservedIds.insert(actuator.id).second) {
                throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
            }
        }
        for (const DatumContactPair& contact : mechanism.contacts) {
            if (!reservedIds.insert(contact.id).second) {
                throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
            }
        }
        for (const DatumRequirement& requirement : mechanism.requirements) {
            if (!reservedIds.insert(requirement.id).second) {
                throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
            }
        }
    }
    for (const DatumMotor& motor : model.motors) {
        if (!reservedIds.insert(motor.id).second) {
            throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
        }
        const DatumMechanism* mechanism = model.mechanism(motor.mechanismId);
        if (mechanism == nullptr) {
            throw std::invalid_argument("motor references an unknown mechanism");
        }
        const auto isMechanismBody = [mechanism](const std::string& objectId) {
            return std::find(mechanism->bodyObjectIds.begin(), mechanism->bodyObjectIds.end(), objectId) !=
                   mechanism->bodyObjectIds.end();
        };
        if (motor.cadRepresentationObjectId.has_value() &&
            (object(*motor.cadRepresentationObjectId) == nullptr ||
             !isMechanismBody(*motor.cadRepresentationObjectId))) {
            throw std::invalid_argument("motor CAD representation must be an object in its mechanism");
        }
        for (const DatumMountingReference& mounting : motor.mountingReferences) {
            if (object(mounting.objectId) == nullptr || !isMechanismBody(mounting.objectId)) {
                throw std::invalid_argument("motor mounting reference must point to an object in its mechanism");
            }
            if (mounting.topologyReferenceId.has_value()) {
                const auto topology = std::find_if(
                    topologyReferences_.begin(), topologyReferences_.end(), [&mounting](const auto& reference) {
                        return reference.id == *mounting.topologyReferenceId;
                    });
                if (topology == topologyReferences_.end() || topology->objectId != mounting.objectId) {
                    throw std::invalid_argument("motor mounting topology reference must belong to its CAD object");
                }
            }
        }
    }
    for (const DatumMechanism& mechanism : model.mechanisms) {
        const auto bodyForId = [&mechanism](const std::string& bodyId) -> const DatumMechanismBody* {
            const auto found = std::find_if(mechanism.bodies.begin(), mechanism.bodies.end(), [&bodyId](const auto& body) {
                return body.id == bodyId;
            });
            return found == mechanism.bodies.end() ? nullptr : &*found;
        };
        for (const DatumJoint& joint : mechanism.joints) {
            if (bodyForId(joint.bodyAId) == nullptr || bodyForId(joint.bodyBId) == nullptr) {
                throw std::invalid_argument("joint endpoints must reference explicit mechanism body definitions");
            }
        }
        for (const DatumContactPair& contact : mechanism.contacts) {
            const DatumMechanismBody* bodyA = bodyForId(contact.bodyAId);
            const DatumMechanismBody* bodyB = bodyForId(contact.bodyBId);
            if (bodyA == nullptr || bodyB == nullptr) {
                throw std::invalid_argument("contact pairs must reference explicit mechanism body definitions");
            }
            const auto validateSurface = [this](const std::optional<std::string>& referenceId,
                                                const DatumMechanismBody* body) {
                if (!referenceId.has_value()) return;
                const auto topology = std::find_if(topologyReferences_.begin(), topologyReferences_.end(),
                                                   [&referenceId](const auto& reference) {
                                                       return reference.id == *referenceId;
                                                   });
                if (topology != topologyReferences_.end() &&
                    (topology->objectId != body->cadObjectId || topology->subshapeType != DatumSubshapeType::Face)) {
                    throw std::invalid_argument("contact surface reference must be a face on its corresponding CAD body");
                }
            };
            validateSurface(contact.surfaceAReferenceId, bodyA);
            validateSurface(contact.surfaceBReferenceId, bodyB);
        }
        for (const DatumAxis& axis : mechanism.axes) {
            if (object(axis.baseObjectId) == nullptr || object(axis.movingObjectId) == nullptr) {
                throw std::invalid_argument("axis base and moving references must be existing CAD objects");
            }
        }
    }
    for (const StructuralAnalysisStudy& study : model.structuralStudies) {
        if (!reservedIds.insert(study.id).second) {
            throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
        }
        if (object(study.cadObjectId) == nullptr) {
            throw std::invalid_argument("structural study must reference an existing CAD object");
        }
        const MaterialDefinition* material = electronicsModel_.material(study.materialDefinitionId);
        if (material == nullptr) {
            throw std::invalid_argument("structural study must reference an existing material definition");
        }
        try {
            material->validate();
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument(std::string("structural study material is invalid: ") + error.what());
        }
        if (study.materialStateId.has_value()) {
            const auto state = std::find_if(
                electronicsModel_.materialStates.begin(), electronicsModel_.materialStates.end(),
                [&study](const MaterialState& item) { return item.id == *study.materialStateId; });
            if (state == electronicsModel_.materialStates.end() ||
                state->materialDefinitionId != study.materialDefinitionId) {
                throw std::invalid_argument("structural study material state must reference its assigned material");
            }
        }
        for (const BoundaryCondition& condition : study.boundaryConditions) {
            const std::string conditionId = boundaryConditionId(condition);
            if (!reservedIds.insert(conditionId).second ||
                boundaryConditionCadObjectId(condition) != study.cadObjectId) {
                throw std::invalid_argument("structural condition UUID or CAD object reference is invalid");
            }
            const std::optional<std::string> topologyId = boundaryConditionTopologyReferenceId(condition);
            if (topologyId.has_value()) {
                const auto topology = std::find_if(
                    topologyReferences_.begin(), topologyReferences_.end(), [&topologyId](const auto& reference) {
                        return reference.id == *topologyId;
                    });
                if (topology == topologyReferences_.end() || topology->objectId != study.cadObjectId ||
                    topology->subshapeType != DatumSubshapeType::Face) {
                    throw std::invalid_argument("structural boundary conditions must reference a persisted CAD face on their body");
                }
            }
        }
        for (const StructuralAnalysisRun& run : study.runs) {
            if (!reservedIds.insert(run.provenance.solverRunId).second ||
                !reservedIds.insert(run.provenance.mesh.revisionId).second ||
                run.provenance.sourceProjectId != projectId_ ||
                run.provenance.cadObjectId != study.cadObjectId ||
                run.provenance.materialDefinitionId != study.materialDefinitionId ||
                run.provenance.materialStateId != study.materialStateId ||
                run.provenance.equationDefinitionId != study.equationDefinitionId) {
                throw std::invalid_argument("structural result provenance must match its project and study inputs");
            }
            for (const std::string& topologyId : run.provenance.topologyReferenceIds) {
                const auto topology = std::find_if(topologyReferences_.begin(), topologyReferences_.end(),
                                                   [&topologyId](const auto& reference) {
                                                       return reference.id == topologyId;
                                                   });
                if (topology == topologyReferences_.end() || topology->objectId != study.cadObjectId ||
                    topology->subshapeType != DatumSubshapeType::Face) {
                    throw std::invalid_argument("analysis provenance must reference existing faces on its CAD body");
                }
            }
            for (const FieldDataset& field : run.fields) {
                if (!reservedIds.insert(field.id).second) {
                    throw std::invalid_argument("engineering entity UUID collides with an existing document identity");
                }
            }
        }
    }

    if (engineeringModel_ != model) {
        engineeringModel_ = std::move(model);
        ++revision_;
        dirty_ = true;
    }
}

void DatumDocument::setElectronicsModel(DatumElectronicsModel model)
{
    model.validate();
    std::unordered_set<std::string> reservedIds;
    reservedIds.reserve(1 + objects_.size() + topologyReferences_.size());
    reservedIds.insert(projectId_);
    for (const DatumObject& item : objects_) {
        reservedIds.insert(item.id);
        for (const DatumFeature& feature : item.featureGraph.features()) {
            reservedIds.insert(feature.id);
        }
    }
    for (const DatumTopologyReference& reference : topologyReferences_) {
        reservedIds.insert(reference.id);
    }
    reserveEngineeringIdentities(engineeringModel_, reservedIds);
    reserveElectronicsIdentities(model, reservedIds);
    for (const StructuralAnalysisStudy& study : engineeringModel_.structuralStudies) {
        if (model.material(study.materialDefinitionId) == nullptr) {
            throw std::invalid_argument("material catalog update would orphan a structural study");
        }
        if (study.materialStateId.has_value()) {
            const auto state = std::find_if(
                model.materialStates.begin(), model.materialStates.end(), [&study](const MaterialState& item) {
                    return item.id == *study.materialStateId;
                });
            if (state == model.materialStates.end() ||
                state->materialDefinitionId != study.materialDefinitionId) {
                throw std::invalid_argument("material catalog update would orphan a structural study state");
            }
        }
    }

    if (electronicsModel_ != model) {
        electronicsModel_ = std::move(model);
        ++revision_;
        dirty_ = true;
    }
}

void DatumDocument::setFeatureGraph(const std::string& objectId,
                                    DatumFeatureGraph graph,
                                    bool captureTopologyReferences)
{
    DatumObject* target = nullptr;
    for (DatumObject& candidate : objects_) {
        if (candidate.id == objectId) {
            target = &candidate;
            break;
        }
    }
    if (target == nullptr) {
        throw std::invalid_argument("feature graph references an unknown object id");
    }
    if (graph.ownerObjectId() != objectId) {
        throw std::invalid_argument("feature graph owner does not match its document object");
    }
    graph.recompute();

    const bool hasValidOutput = graph.outputSucceeded();
    TopoDS_Shape outputShape;
    ShapeMetrics outputMetrics;
    std::vector<DatumTopologyReference> generatedReferences;
    std::unordered_set<std::string> reservedIds;
    reservedIds.reserve(1 + objects_.size() + topologyReferences_.size());
    reservedIds.insert(projectId_);
    reserveEngineeringIdentities(engineeringModel_, reservedIds);
    reserveElectronicsIdentities(electronicsModel_, reservedIds);
    reservedIds.insert(objectId);
    for (const DatumObject& existing : objects_) {
        if (existing.id != objectId) {
            reservedIds.insert(existing.id);
            for (const DatumFeature& feature : existing.featureGraph.features()) {
                reservedIds.insert(feature.id);
            }
        }
    }
    for (const DatumTopologyReference& reference : topologyReferences_) {
        reservedIds.insert(reference.id);
    }
    for (const DatumFeature& feature : graph.features()) {
        if (!reservedIds.insert(feature.id).second) {
            throw std::invalid_argument("feature UUID is not unique within the document");
        }
    }
    if (hasValidOutput) {
        outputShape = graph.outputShape();
        outputMetrics = OcctKernel::measure(outputShape);
        if (!outputMetrics.valid) {
            throw std::invalid_argument("feature graph output must be a valid exact OCCT B-Rep");
        }
        if (captureTopologyReferences) {
            generatedReferences = DatumTopologyResolver::captureAll(
                objectId, outputShape, graph.outputFeatureId());
            for (const DatumTopologyReference& reference : generatedReferences) {
                if (!reservedIds.insert(reference.id).second) {
                    throw std::runtime_error("generated topology reference UUID is not unique within the document");
                }
            }
        }
    }

    if (hasValidOutput && captureTopologyReferences) {
        const std::size_t replacedReferenceCount = static_cast<std::size_t>(std::count_if(
            topologyReferences_.begin(), topologyReferences_.end(), [&objectId](const auto& reference) {
                return reference.objectId == objectId;
            }));
        topologyReferences_.reserve(topologyReferences_.size() - replacedReferenceCount + generatedReferences.size());
    }

    target->featureGraph = std::move(graph);
    if (hasValidOutput) {
        target->shape = std::move(outputShape);
        target->metrics = outputMetrics;
        if (captureTopologyReferences) {
            std::erase_if(topologyReferences_, [&objectId](const DatumTopologyReference& reference) {
                return reference.objectId == objectId;
            });
            topologyReferences_.insert(topologyReferences_.end(),
                                       std::make_move_iterator(generatedReferences.begin()),
                                       std::make_move_iterator(generatedReferences.end()));
        }
    }
    ++revision_;
    dirty_ = true;
}

DatumObjectSnapshot DatumDocument::snapshotObject(const std::string& objectId) const
{
    const DatumObject* target = object(objectId);
    if (target == nullptr) {
        throw std::invalid_argument("cannot snapshot an unknown document object");
    }
    DatumObjectSnapshot snapshot;
    snapshot.object = *target;
    std::copy_if(topologyReferences_.begin(), topologyReferences_.end(),
                 std::back_inserter(snapshot.topologyReferences), [&objectId](const auto& reference) {
                     return reference.objectId == objectId;
                 });
    return snapshot;
}

void DatumDocument::restoreObjectSnapshot(DatumObjectSnapshot snapshot)
{
    const auto objectIterator = std::find_if(objects_.begin(), objects_.end(), [&snapshot](const DatumObject& item) {
        return item.id == snapshot.object.id;
    });
    if (objectIterator == objects_.end()) {
        throw std::invalid_argument("cannot restore an object snapshot for an unknown object");
    }
    for (const DatumTopologyReference& reference : snapshot.topologyReferences) {
        if (reference.objectId != snapshot.object.id) {
            throw std::invalid_argument("object snapshot contains a topology reference for another object");
        }
    }
    std::unordered_set<std::string> reservedIds;
    reservedIds.insert(projectId_);
    reservedIds.insert(snapshot.object.id);
    reserveEngineeringIdentities(engineeringModel_, reservedIds);
    reserveElectronicsIdentities(electronicsModel_, reservedIds);
    for (const DatumObject& existing : objects_) {
        if (existing.id != snapshot.object.id) {
            reservedIds.insert(existing.id);
            for (const DatumFeature& feature : existing.featureGraph.features()) {
                reservedIds.insert(feature.id);
            }
        }
    }
    for (const DatumTopologyReference& reference : topologyReferences_) {
        if (reference.objectId != snapshot.object.id) {
            reservedIds.insert(reference.id);
        }
    }
    for (const DatumFeature& feature : snapshot.object.featureGraph.features()) {
        if (!reservedIds.insert(feature.id).second) {
            throw std::invalid_argument("restored feature UUID collides with another document identity");
        }
    }
    for (const DatumTopologyReference& reference : snapshot.topologyReferences) {
        if (!reservedIds.insert(reference.id).second) {
            throw std::invalid_argument("restored topology UUID collides with another document identity");
        }
    }

    const std::size_t replacedReferenceCount = static_cast<std::size_t>(std::count_if(
        topologyReferences_.begin(), topologyReferences_.end(), [&snapshot](const auto& reference) {
            return reference.objectId == snapshot.object.id;
        }));
    topologyReferences_.reserve(topologyReferences_.size() - replacedReferenceCount +
                                snapshot.topologyReferences.size());
    *objectIterator = std::move(snapshot.object);
    std::erase_if(topologyReferences_, [&objectIterator](const DatumTopologyReference& reference) {
        return reference.objectId == objectIterator->id;
    });
    topologyReferences_.insert(topologyReferences_.end(),
                               std::make_move_iterator(snapshot.topologyReferences.begin()),
                               std::make_move_iterator(snapshot.topologyReferences.end()));
    ++revision_;
    dirty_ = true;
}

}  // namespace datumloop
