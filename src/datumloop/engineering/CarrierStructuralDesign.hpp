#pragma once
#include "datumloop/engineering/MotorCarrierDesign.hpp"
#include "datumloop/numerics/AnalysisMesh.hpp"
namespace datumloop {
// This is an explicitly idealized static load path, not fastener/contact analysis.
StructuralAnalysisStudy makeMotorCarrierStaticStudy(const MotorCarrierCandidate& candidate);
struct CarrierStructuralResponse {
    double maximumInterfaceDisplacementM=0;
    Vector3 meanMotorDisplacementM{};
    Vector3 motorRotationRad{};
    double angularDeflectionRad=0;
    // Motor normal force / mean normal motion under the combined envelope.
    // Includes self inertia; this is not an isolated unit-load stiffness.
    double normalStiffnessNPerM=0;
    double nominalMassKg=0;
    std::vector<DesignMetric> designMetrics;
};
CarrierStructuralResponse evaluateMotorCarrierResponse(const MotorCarrierCandidate& candidate,
                                                       const AnalysisMesh& mesh,
                                                       const StructuralAnalysisRun& run);
// A bounded stiffness hypothesis, never a substituted simulation result.
std::optional<DesignParameterChange> proposeMotorCarrierThicknessChange(
    const PartRequirements& part, const CarrierStructuralResponse& response);
} // namespace datumloop
