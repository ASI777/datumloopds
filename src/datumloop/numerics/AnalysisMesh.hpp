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
    double minimumScaledJacobian = 0.001;
    double maximumBoundaryAreaRelativeError = 0.01;
    double maximumVolumeRelativeError = 0.01;
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

struct AnalysisTetElement {
    std::array<std::uint32_t, 4> nodeIndices{};
    friend bool operator==(const AnalysisTetElement&, const AnalysisTetElement&) = default;
};

struct AnalysisBoundaryTriangle {
    std::array<std::uint32_t, 3> nodeIndices{};
    int attribute = 0;
    friend bool operator==(const AnalysisBoundaryTriangle&, const AnalysisBoundaryTriangle&) = default;
};

struct AnalysisBoundaryRegion {
    std::string topologyReferenceId;
    int attribute = 0;
    Vector3 outwardNormal{0.0, 0.0, 0.0};
    double areaM2 = 0.0;
    double exactCadAreaM2 = 0.0;

    friend bool operator==(const AnalysisBoundaryRegion&, const AnalysisBoundaryRegion&) = default;
};

struct AnalysisMesh {
    AnalysisMeshProvenance provenance;
    std::string cadObjectId;
    std::uint64_t sourceDocumentRevision = 0;
    std::string sourceProjectId;
    std::string geometrySha256;
    std::string backendBuildInfo;
    double meshingWallTimeS = 0.0;
    std::vector<std::string> mesherLog;
    std::vector<std::string> topologyReferenceIds;
    std::vector<AnalysisNode> nodes;
    std::vector<AnalysisHexElement> elements;
    std::vector<AnalysisBoundaryElement> boundaryElements;
    std::vector<AnalysisBoundaryRegion> boundaryRegions;
    std::vector<AnalysisTetElement> tetrahedra;
    std::vector<AnalysisBoundaryTriangle> boundaryTriangles;

    [[nodiscard]] std::size_t elementCount() const noexcept { return elements.size() + tetrahedra.size(); }
    [[nodiscard]] std::size_t boundaryElementCount() const noexcept { return boundaryElements.size() + boundaryTriangles.size(); }

    void validate() const;

    friend bool operator==(const AnalysisMesh&, const AnalysisMesh&) = default;
};

struct AnalysisMeshingRequest {
    std::string sourceProjectId;
    std::uint64_t sourceDocumentRevision = 0;
    std::string cadObjectId;
    AnalysisMeshSettings settings;
    void validateAgainst(const DatumDocument& document) const;
};

class IAnalysisMesher {
public:
    virtual ~IAnalysisMesher() = default;
    [[nodiscard]] virtual AnalysisMesh generate(const DatumDocument& document,
                                                const AnalysisMeshingRequest& request) const = 0;
};

// Nominal exact BRep only; display triangulations/normals never enter the hash.
[[nodiscard]] std::string analysisBrepBytes(const DatumObject& object);
[[nodiscard]] std::string analysisGeometrySha256(const DatumObject& object);
void validateTetrahedralMeshAgainstCad(const DatumDocument& document, const AnalysisMesh& mesh,
                                       double maximumBoundaryAreaRelativeError,
                                       double maximumVolumeRelativeError);

class OcctPrismAnalysisMesher final {
public:
    // The first supported production mesh is a structured hex mesh derived
    // from one axis-aligned rectangular OCCT solid. Other B-Reps fail closed.
    [[nodiscard]] static AnalysisMesh generate(const DatumDocument& document,
                                               const std::string& cadObjectId,
                                               const AnalysisMeshSettings& settings);
};

}  // namespace datumloop
