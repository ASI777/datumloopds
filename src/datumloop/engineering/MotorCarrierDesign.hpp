#pragma once
#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/engineering/PartDesign.hpp"

namespace datumloop {
struct MotorCarrierHole {
    std::string name;
    std::string semanticRole;
    std::string interfaceId;
    double xMm = 0;
    double yMm = 0;
    double diameterMm = 0;
    std::map<std::string, FeatureParameter> parameterDefinitions;
};
struct MotorCarrierCandidate {
    DatumDocument document;
    std::string cadObjectId;
    std::vector<MotorCarrierHole> holes;
};
// Restricted, editable rectangular carrier recipe with native semantic through
// bores. Uses reviewed requirements; does not infer motor or gantry dimensions.
std::vector<MotorCarrierHole> motorCarrierHoleLayout(const PartRequirements& part);
MotorCarrierCandidate generateMotorCarrier(PartRequirements part);
} // namespace datumloop
