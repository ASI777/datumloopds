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
    ++revision_;
    dirty_ = true;
}

void DatumDocument::replaceWith(DatumDocument replacement)
{
    name_ = std::move(replacement.name_);
    projectId_ = std::move(replacement.projectId_);
    objects_ = std::move(replacement.objects_);
    topologyReferences_ = std::move(replacement.topologyReferences_);
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
        })) {
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
