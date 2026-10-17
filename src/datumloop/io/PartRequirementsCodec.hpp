#pragma once
#include "datumloop/engineering/PartDesign.hpp"
#include <QJsonObject>
#include <QString>
namespace datumloop {
// Ingest reviewed engineering-source records, not unverified OCR output.
PartRequirements decodePartRequirements(const QJsonObject& object);
QJsonObject encodePartRequirements(const PartRequirements& part);
PartRequirements loadPartRequirements(const QString& path);
} // namespace datumloop
