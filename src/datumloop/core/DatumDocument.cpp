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

}  // namespace

const DatumObject& DatumDocument::createBox(std::string name, const Vector3& dimensions)
{
    if (name.find_first_not_of(" \t\n\r") == std::string::npos) {
        throw std::invalid_argument("object name must not be empty");
    }

    DatumObject object;
    object.id = makeId();
    object.name = std::move(name);
    object.shape = OcctKernel::box(dimensions);
    object.metrics = OcctKernel::measure(object.shape);
    if (!object.metrics.valid) {
        throw std::runtime_error("OCCT produced an invalid box");
    }

    objects_.push_back(std::move(object));
    ++revision_;
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
    }
    return true;
}

}  // namespace datumloop
