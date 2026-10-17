#pragma once

#include "datumloop/materials/MaterialModel.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace datumloop {

using Point2M = std::array<double, 2>;

enum class CircuitComponentKind {
    Unspecified,
    VoltageSource,
    CurrentSource,
    Resistor,
};

struct CircuitComponent {
    std::string id;
    std::string sourceId;
    std::string reference;
    std::string displayName;
    CircuitComponentKind kind = CircuitComponentKind::Unspecified;
    std::optional<double> electricalValue;
    std::string electricalValueUnit;
    std::vector<std::string> pinIds;

    friend bool operator==(const CircuitComponent&, const CircuitComponent&) = default;
};

struct CircuitPin {
    std::string id;
    std::string componentId;
    std::string number;
    std::string name;

    friend bool operator==(const CircuitPin&, const CircuitPin&) = default;
};

struct CircuitNet {
    std::string id;
    std::string sourceId;
    std::string name;
    bool referenceNode = false;
    std::vector<std::string> pinIds;

    friend bool operator==(const CircuitNet&, const CircuitNet&) = default;
};

struct CircuitDocument {
    std::string id;
    std::string name;
    std::vector<CircuitComponent> components;
    std::vector<CircuitPin> pins;
    std::vector<CircuitNet> nets;

    void validate() const;
    [[nodiscard]] const CircuitComponent* component(const std::string& id) const noexcept;
    [[nodiscard]] const CircuitPin* pin(const std::string& id) const noexcept;
    [[nodiscard]] const CircuitNet* net(const std::string& id) const noexcept;
    [[nodiscard]] const CircuitNet* netForPin(const std::string& pinId) const noexcept;

    friend bool operator==(const CircuitDocument&, const CircuitDocument&) = default;
};

enum class PcbLayerRole {
    Copper,
    Dielectric,
    SolderMask,
    SolderPaste,
    Silkscreen,
    Other,
};

struct PcbLayer {
    std::string id;
    std::string name;
    PcbLayerRole role = PcbLayerRole::Other;
    std::string materialDefinitionId;
    std::optional<double> thicknessM;
    std::optional<double> surfaceRoughnessM;
    std::optional<double> dielectricPermittivity;
    std::optional<double> lossTangent;

    friend bool operator==(const PcbLayer&, const PcbLayer&) = default;
};

struct PcbStackup {
    std::string id;
    std::vector<PcbLayer> layers;

    friend bool operator==(const PcbStackup&, const PcbStackup&) = default;
};

struct PcbComponent {
    std::string id;
    std::string sourceId;
    std::string reference;
    std::string footprint;
    std::optional<std::string> circuitComponentId;
    Point2M centerM{0.0, 0.0};
    double rotationRad = 0.0;
    double widthM = 0.0;
    double heightM = 0.0;
    std::optional<double> packageHeightM;
    std::string layerName;

    friend bool operator==(const PcbComponent&, const PcbComponent&) = default;
};

struct PcbTrace {
    std::string id;
    std::string sourceId;
    std::optional<std::string> netId;
    std::string layerId;
    double widthM = 0.0;
    std::vector<Point2M> routeM;

    friend bool operator==(const PcbTrace&, const PcbTrace&) = default;
};

struct PcbVia {
    std::string id;
    std::string sourceId;
    std::optional<std::string> netId;
    Point2M centerM{0.0, 0.0};
    double outerDiameterM = 0.0;
    double holeDiameterM = 0.0;
    std::vector<std::string> layerIds;

    friend bool operator==(const PcbVia&, const PcbVia&) = default;
};

enum class PcbPadShape {
    Rectangle,
    Circle,
};

struct PcbPad {
    std::string id;
    std::string sourceId;
    std::string componentId;
    std::optional<std::string> pinId;
    std::optional<std::string> netId;
    PcbPadShape shape = PcbPadShape::Rectangle;
    Point2M centerM{0.0, 0.0};
    double widthM = 0.0;
    double heightM = 0.0;
    double rotationRad = 0.0;
    std::vector<std::string> layerIds;

    friend bool operator==(const PcbPad&, const PcbPad&) = default;
};

struct ElectronicMaterialRegion {
    std::string id;
    std::string name;
    std::string materialDefinitionId;
    std::optional<std::string> layerId;
    std::vector<Point2M> outlineM;

    friend bool operator==(const ElectronicMaterialRegion&, const ElectronicMaterialRegion&) = default;
};

struct PcbDocument {
    std::string id;
    std::string name;
    std::optional<std::string> circuitDocumentId;
    std::optional<std::string> boardMaterialDefinitionId;
    double widthM = 0.0;
    double heightM = 0.0;
    double thicknessM = 0.0;
    Point2M centerM{0.0, 0.0};
    PcbStackup stackup;
    std::vector<PcbComponent> components;
    std::vector<PcbTrace> traces;
    std::vector<PcbVia> vias;
    std::vector<PcbPad> pads;
    std::vector<ElectronicMaterialRegion> materialRegions;

    friend bool operator==(const PcbDocument&, const PcbDocument&) = default;
};

struct DatumElectronicsModel {
    std::vector<MaterialDefinition> materials;
    std::vector<MaterialState> materialStates;
    std::vector<CircuitDocument> circuits;
    std::vector<PcbDocument> pcbs;

    void validate() const;
    [[nodiscard]] const MaterialDefinition* material(const std::string& id) const noexcept;
    [[nodiscard]] const CircuitDocument* circuit(const std::string& id) const noexcept;
    [[nodiscard]] const PcbDocument* pcb(const std::string& id) const noexcept;
    [[nodiscard]] bool containsIdentity(const std::string& id) const noexcept;

    friend bool operator==(const DatumElectronicsModel&, const DatumElectronicsModel&) = default;
};

[[nodiscard]] std::string circuitComponentKindName(CircuitComponentKind kind);
[[nodiscard]] std::optional<CircuitComponentKind> circuitComponentKindFromName(const std::string& name);
[[nodiscard]] std::string pcbLayerRoleName(PcbLayerRole role);
[[nodiscard]] std::optional<PcbLayerRole> pcbLayerRoleFromName(const std::string& name);

}  // namespace datumloop
