#pragma once

#include "datumloop/electronics/ElectronicsModel.hpp"

#include <QJsonObject>

namespace datumloop {

[[nodiscard]] QJsonObject encodeElectronicsModel(const DatumElectronicsModel& model);
[[nodiscard]] DatumElectronicsModel decodeElectronicsModel(const QJsonObject& object);

}  // namespace datumloop
