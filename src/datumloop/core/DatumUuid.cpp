#include "datumloop/core/DatumUuid.hpp"

#include <array>
#include <iomanip>
#include <random>
#include <sstream>

namespace datumloop {

std::string createDatumUuidV4()
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

bool isDatumUuid(std::string_view value) noexcept
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

}  // namespace datumloop
