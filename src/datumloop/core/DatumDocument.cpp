#include "datumloop/core/DatumDocument.hpp"

#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
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

    return addObject(createDatumUuidV4(), std::move(name), OcctKernel::box(dimensions));
}

const DatumObject& DatumDocument::addObject(std::string id,
                                            std::string name,
                                            TopoDS_Shape shape,
                                            const Vector3& position,
                                            std::optional<std::string> parentId)
{
    if (!isDatumUuid(id)) {
        throw std::invalid_argument("object id must be a UUID");
    }
    if (name.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("object name must not be empty");
    }
    if (object(id) != nullptr ||
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
    objects_.push_back(std::move(object));
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
    if (reference.id == projectId_ || object(reference.id) != nullptr ||
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

}  // namespace datumloop
