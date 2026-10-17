#pragma once

#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/engineering/StructuralAnalysis.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace datumloop {

struct AnalysisMeshSettings {
    double maximumElementSizeM = 0.01;
    double maximumElementAspectRatio = 3.0;
    std::uint64_t maximumElementCount = 250000;
};

struct AnalysisNode {
    Vector3 positionM{0.0, 0.0, 0.0};

    friend bool operator==(const AnalysisNode&, const AnalysisNode&) = default;
};

struct AnalysisHexElement {
    std::array<std::uint32_t, 8> nodeIndices{};

    friend bool operator==(const AnalysisHexElement&, const AnalysisHexElement&) = default;
};

struct AnalysisBoundaryElement {
    std::array<std::uint32_t, 4> nodeIndices{};
    int attribute = 0;

    friend bool operator==(const AnalysisBoundaryElement&, const AnalysisBoundaryElement&) = default;
};

struct AnalysisBoundaryRegion {
    std::string topologyReferenceId;
    int attribute = 0;
    Vector3 outwardNormal{0.0, 0.0, 0.0};
    double areaM2 = 0.0;

    friend bool operator==(const AnalysisBoundaryRegion&, const AnalysisBoundaryRegion&) = default;
};

struct AnalysisMesh {
    AnalysisMeshProvenance provenance;
    std::string cadObjectId;
    std::uint64_t sourceDocumentRevision = 0;
    std::vector<std::string> topologyReferenceIds;
    std::vector<AnalysisNode> nodes;
    std::vector<AnalysisHexElement> elements;
    std::vector<AnalysisBoundaryElement> boundaryElements;
    std::vector<AnalysisBoundaryRegion> boundaryRegions;

    void validate() const;

    friend bool operator==(const AnalysisMesh&, const AnalysisMesh&) = default;
};

class OcctPrismAnalysisMesher final {
public:
    // The first supported production mesh is a structured hex mesh derived
    // from one axis-aligned rectangular OCCT solid. Other B-Reps fail closed.
    [[nodiscard]] static AnalysisMesh generate(const DatumDocument& document,
                                               const std::string& cadObjectId,
                                               const AnalysisMeshSettings& settings);
};

}  // namespace datumloop
