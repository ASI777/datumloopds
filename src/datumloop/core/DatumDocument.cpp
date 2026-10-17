#include "datumloop/core/DatumDocument.hpp"

#include "datumloop/geometry/OcctKernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace datumloop {
namespace {

std::string makeId()
{
    std::array<unsigned char, 16> bytes{};
    std::random_device random;
    for (unsigned char& byte : bytes) {
        byte = static_cast<unsigned char>(random());
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);

    std::ostringstream id;
    id << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        id << std::setw(2) << static_cast<unsigned int>(bytes[index]);
        if (index == 3 || index == 5 || index == 7 || index == 9) {
            id << '-';
        }
    }
    return id.str();
}

bool finite(const Vector3& value)
{
    return std::all_of(value.begin(), value.end(), [](double component) {
        return std::isfinite(component);
    });
}

bool validUuid(const std::string& value)
{
    if (value.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const bool separator = index == 8 || index == 13 || index == 18 || index == 23;
        const char character = value[index];
        if (separator) {
            if (character != '-') {
                return false;
            }
        } else if (!((character >= '0' && character <= '9') ||
                     (character >= 'a' && character <= 'f') ||
                     (character >= 'A' && character <= 'F'))) {
            return false;
        }
    }
    return true;
}

}  // namespace

DatumDocument::DatumDocument(std::string name, std::string projectId)
    : name_(std::move(name)), projectId_(projectId.empty() ? makeId() : std::move(projectId))
{
    if (name_.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("project name must not be empty");
    }
    if (!validUuid(projectId_)) {
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
    projectId_ = makeId();
    objects_.clear();
    ++revision_;
    dirty_ = true;
}

void DatumDocument::replaceWith(DatumDocument replacement)
{
    name_ = std::move(replacement.name_);
    projectId_ = std::move(replacement.projectId_);
    objects_ = std::move(replacement.objects_);
    ++revision_;
    dirty_ = replacement.dirty_;
}

const DatumObject& DatumDocument::createBox(std::string name, const Vector3& dimensions)
{
    if (name.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("object name must not be empty");
    }

    return addObject(makeId(), std::move(name), OcctKernel::box(dimensions));
}

const DatumObject& DatumDocument::addObject(std::string id,
                                            std::string name,
                                            TopoDS_Shape shape,
                                            const Vector3& position,
                                            std::optional<std::string> parentId)
{
    if (!validUuid(id)) {
        throw std::invalid_argument("object id must be a UUID");
    }
    if (name.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("object name must not be empty");
    }
    if (object(id) != nullptr) {
        throw std::invalid_argument("object id must be unique within a project");
    }
    if (id == projectId_) {
        throw std::invalid_argument("object id must be distinct from the project id");
    }
    if (parentId.has_value()) {
        if (!validUuid(*parentId) || object(*parentId) == nullptr) {
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

}  // namespace datumloop
