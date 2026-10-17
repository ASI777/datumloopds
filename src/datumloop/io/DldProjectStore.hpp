#pragma once

#include "datumloop/core/DatumDocument.hpp"

#include <QString>

namespace datumloop {

class DldProjectStore final {
public:
    static constexpr int SchemaVersion = 6;

    [[nodiscard]] static DatumDocument load(const QString& filePath);
    static void save(DatumDocument& document, const QString& filePath);
};

}  // namespace datumloop
