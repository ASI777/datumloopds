#pragma once

#include "datumloop/electronics/ElectronicsModel.hpp"

#include <QByteArray>

#include <string>
#include <vector>

namespace datumloop {

struct ElectronicsImportResult {
    DatumElectronicsModel model;
    std::vector<std::string> warnings;
};

// Adapter boundary: external design tools exchange source elements, while
// DatumLoop stores validated, UUID-backed SI models of materials, circuits,
// and boards.
class CircuitJsonAdapter final {
public:
    [[nodiscard]] static ElectronicsImportResult importBytes(const QByteArray& bytes);
    [[nodiscard]] static QByteArray exportPcb(const DatumElectronicsModel& model,
                                              const std::string& pcbId);
};

class KiCadPcbAdapter final {
public:
    [[nodiscard]] static ElectronicsImportResult importBytes(const QByteArray& bytes,
                                                             std::string boardName = "Imported KiCad PCB");
};

}  // namespace datumloop
