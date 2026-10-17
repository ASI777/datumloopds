#pragma once

#include <string>
#include <string_view>

namespace datumloop {

[[nodiscard]] std::string createDatumUuidV4();
[[nodiscard]] bool isDatumUuid(std::string_view value) noexcept;

}  // namespace datumloop
