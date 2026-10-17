#include "datumloop/electronics/ElectronicsModel.hpp"

#include "datumloop/core/DatumUuid.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace datumloop {
namespace {

bool hasText(const std::string& value)
{
    return value.find_first_not_of(" \t\n\r") != std::string::npos;
}

bool finite(const Point2M& point)
{
    return std::isfinite(point[0]) && std::isfinite(point[1]);
}

void reserve(std::unordered_set<std::string>& ids, const std::string& id, const char* kind)
{
    if (!isDatumUuid(id) || !ids.insert(id).second) {
        throw std::invalid_argument(std::string(kind) + " requires a unique UUID");
    }
}

void requireOptionalPositive(const std::optional<double>& value, const char* field)
{
    if (value.has_value() && (!std::isfinite(*value) || *value <= 0.0)) {
        throw std::invalid_argument(std::string(field) + " must be finite and positive when specified");
    }
}

void validateCircuit(const CircuitDocument& circuit, std::unordered_set<std::string>& ids)
{
    reserve(ids, circuit.id, "circuit document");
    if (!hasText(circuit.name)) {
        throw std::invalid_argument("circuit document requires a name");
    }

    std::unordered_map<std::string, const CircuitComponent*> components;
    std::unordered_map<std::string, const CircuitPin*> pins;
    std::unordered_map<std::string, const CircuitNet*> nets;
    for (const CircuitComponent& component : circuit.components) {
        reserve(ids, component.id, "circuit component");
        if (!hasText(component.reference)) {
            throw std::invalid_argument("circuit component requires a reference");
        }
        if (component.electricalValue.has_value() && !std::isfinite(*component.electricalValue)) {
            throw std::invalid_argument("circuit component electricalValue must be finite when specified");
        }
        if (component.kind == CircuitComponentKind::Resistor && component.electricalValue.has_value() &&
            *component.electricalValue <= 0.0) {
            throw std::invalid_argument("resistor value must be positive");
        }
        if (component.electricalValue.has_value() && !hasText(component.electricalValueUnit)) {
            throw std::invalid_argument("circuit component values require explicit units");
        }
        components.emplace(component.id, &component);
    }
    for (const CircuitPin& pin : circuit.pins) {
        reserve(ids, pin.id, "circuit pin");
        if (!hasText(pin.componentId) || !hasText(pin.number) || !components.contains(pin.componentId)) {
            throw std::invalid_argument("circuit pins require an existing component and non-empty number");
        }
        pins.emplace(pin.id, &pin);
    }
    std::unordered_map<std::string, std::string> pinOwners;
    for (const CircuitComponent& component : circuit.components) {
        std::unordered_set<std::string> uniquePins;
        for (const std::string& pinId : component.pinIds) {
            const auto pin = pins.find(pinId);
            if (pin == pins.end() || pin->second->componentId != component.id || !uniquePins.insert(pinId).second) {
                throw std::invalid_argument("component pinIds must reference its own unique circuit pins");
            }
        }
        if (uniquePins.size() != static_cast<std::size_t>(std::count_if(
                circuit.pins.begin(), circuit.pins.end(), [&component](const CircuitPin& pin) {
                    return pin.componentId == component.id;
                }))) {
            throw std::invalid_argument("every circuit pin must be listed by its owning component");
        }
    }
    for (const CircuitNet& net : circuit.nets) {
        reserve(ids, net.id, "circuit net");
        if (!hasText(net.name)) {
            throw std::invalid_argument("circuit net requires a name");
        }
        std::unordered_set<std::string> uniquePins;
        for (const std::string& pinId : net.pinIds) {
            if (!pins.contains(pinId) || !uniquePins.insert(pinId).second || !pinOwners.emplace(pinId, net.id).second) {
                throw std::invalid_argument("net pinIds must be valid and assigned to at most one net");
            }
        }
        nets.emplace(net.id, &net);
    }
    (void)nets;
}

}  // namespace

std::string circuitComponentKindName(CircuitComponentKind kind)
{
    switch (kind) {
    case CircuitComponentKind::Unspecified: return "unspecified";
    case CircuitComponentKind::VoltageSource: return "voltage_source";
    case CircuitComponentKind::CurrentSource: return "current_source";
    case CircuitComponentKind::Resistor: return "resistor";
    }
    throw std::invalid_argument("unknown circuit component kind");
}

std::optional<CircuitComponentKind> circuitComponentKindFromName(const std::string& name)
{
    if (name == "unspecified") return CircuitComponentKind::Unspecified;
    if (name == "voltage_source") return CircuitComponentKind::VoltageSource;
    if (name == "current_source") return CircuitComponentKind::CurrentSource;
    if (name == "resistor") return CircuitComponentKind::Resistor;
    return std::nullopt;
}

std::string pcbLayerRoleName(PcbLayerRole role)
{
    switch (role) {
    case PcbLayerRole::Copper: return "copper";
    case PcbLayerRole::Dielectric: return "dielectric";
    case PcbLayerRole::SolderMask: return "solder_mask";
    case PcbLayerRole::SolderPaste: return "solder_paste";
    case PcbLayerRole::Silkscreen: return "silkscreen";
    case PcbLayerRole::Other: return "other";
    }
    throw std::invalid_argument("unknown PCB layer role");
}

std::optional<PcbLayerRole> pcbLayerRoleFromName(const std::string& name)
{
    if (name == "copper") return PcbLayerRole::Copper;
    if (name == "dielectric") return PcbLayerRole::Dielectric;
    if (name == "solder_mask") return PcbLayerRole::SolderMask;
    if (name == "solder_paste") return PcbLayerRole::SolderPaste;
    if (name == "silkscreen") return PcbLayerRole::Silkscreen;
    if (name == "other") return PcbLayerRole::Other;
    return std::nullopt;
}

void CircuitDocument::validate() const
{
    std::unordered_set<std::string> ids;
    validateCircuit(*this, ids);
}

const CircuitComponent* CircuitDocument::component(const std::string& componentId) const noexcept
{
    const auto found = std::find_if(components.begin(), components.end(), [&componentId](const auto& item) {
        return item.id == componentId;
    });
    return found == components.end() ? nullptr : &*found;
}

const CircuitPin* CircuitDocument::pin(const std::string& pinId) const noexcept
{
    const auto found = std::find_if(pins.begin(), pins.end(), [&pinId](const auto& item) {
        return item.id == pinId;
    });
    return found == pins.end() ? nullptr : &*found;
}

const CircuitNet* CircuitDocument::net(const std::string& netId) const noexcept
{
    const auto found = std::find_if(nets.begin(), nets.end(), [&netId](const auto& item) {
        return item.id == netId;
    });
    return found == nets.end() ? nullptr : &*found;
}

const CircuitNet* CircuitDocument::netForPin(const std::string& pinId) const noexcept
{
    const auto found = std::find_if(nets.begin(), nets.end(), [&pinId](const CircuitNet& net) {
        return std::find(net.pinIds.begin(), net.pinIds.end(), pinId) != net.pinIds.end();
    });
    return found == nets.end() ? nullptr : &*found;
}

const MaterialDefinition* DatumElectronicsModel::material(const std::string& id) const noexcept
{
    const auto found = std::find_if(materials.begin(), materials.end(), [&id](const auto& item) {
        return item.id == id;
    });
    return found == materials.end() ? nullptr : &*found;
}

const CircuitDocument* DatumElectronicsModel::circuit(const std::string& id) const noexcept
{
    const auto found = std::find_if(circuits.begin(), circuits.end(), [&id](const auto& item) {
        return item.id == id;
    });
    return found == circuits.end() ? nullptr : &*found;
}

const PcbDocument* DatumElectronicsModel::pcb(const std::string& id) const noexcept
{
    const auto found = std::find_if(pcbs.begin(), pcbs.end(), [&id](const auto& item) {
        return item.id == id;
    });
    return found == pcbs.end() ? nullptr : &*found;
}

bool DatumElectronicsModel::containsIdentity(const std::string& id) const noexcept
{
    for (const MaterialDefinition& item : materials) if (item.id == id) return true;
    for (const MaterialState& item : materialStates) if (item.id == id) return true;
    for (const CircuitDocument& item : circuits) {
        if (item.id == id) return true;
        for (const CircuitComponent& nested : item.components) if (nested.id == id) return true;
        for (const CircuitPin& nested : item.pins) if (nested.id == id) return true;
        for (const CircuitNet& nested : item.nets) if (nested.id == id) return true;
    }
    for (const PcbDocument& item : pcbs) {
        if (item.id == id || item.stackup.id == id) return true;
        for (const PcbLayer& nested : item.stackup.layers) if (nested.id == id) return true;
        for (const PcbComponent& nested : item.components) if (nested.id == id) return true;
        for (const PcbTrace& nested : item.traces) if (nested.id == id) return true;
        for (const PcbVia& nested : item.vias) if (nested.id == id) return true;
        for (const PcbPad& nested : item.pads) if (nested.id == id) return true;
        for (const ElectronicMaterialRegion& nested : item.materialRegions) if (nested.id == id) return true;
    }
    return false;
}

void DatumElectronicsModel::validate() const
{
    std::unordered_set<std::string> ids;
    ids.reserve(materials.size() + materialStates.size() + circuits.size() + pcbs.size());
    for (const MaterialDefinition& material : materials) {
        material.validate();
        reserve(ids, material.id, "material definition");
    }
    for (const MaterialState& state : materialStates) {
        state.validate();
        reserve(ids, state.id, "material state");
        if (material(state.materialDefinitionId) == nullptr) {
            throw std::invalid_argument("material state references an unknown material definition");
        }
    }
    for (const CircuitDocument& circuitDocument : circuits) {
        validateCircuit(circuitDocument, ids);
    }

    for (const PcbDocument& pcbDocument : pcbs) {
        reserve(ids, pcbDocument.id, "PCB document");
        reserve(ids, pcbDocument.stackup.id, "PCB stackup");
        if (!hasText(pcbDocument.name) || !std::isfinite(pcbDocument.widthM) || pcbDocument.widthM <= 0.0 ||
            !std::isfinite(pcbDocument.heightM) || pcbDocument.heightM <= 0.0 ||
            !std::isfinite(pcbDocument.thicknessM) || pcbDocument.thicknessM <= 0.0 ||
            !finite(pcbDocument.centerM)) {
            throw std::invalid_argument("PCB document requires a name and positive metre dimensions");
        }
        if (pcbDocument.circuitDocumentId.has_value() && circuit(*pcbDocument.circuitDocumentId) == nullptr) {
            throw std::invalid_argument("PCB circuitDocumentId references an unknown circuit");
        }
        if (pcbDocument.boardMaterialDefinitionId.has_value() &&
            material(*pcbDocument.boardMaterialDefinitionId) == nullptr) {
            throw std::invalid_argument("PCB board material references an unknown material definition");
        }

        std::unordered_map<std::string, const PcbLayer*> layers;
        for (const PcbLayer& layer : pcbDocument.stackup.layers) {
            reserve(ids, layer.id, "PCB layer");
            if (!hasText(layer.name) || material(layer.materialDefinitionId) == nullptr) {
                throw std::invalid_argument("PCB layers require a name and a common material definition reference");
            }
            requireOptionalPositive(layer.thicknessM, "PCB layer thicknessM");
            requireOptionalPositive(layer.surfaceRoughnessM, "PCB surfaceRoughnessM");
            requireOptionalPositive(layer.dielectricPermittivity, "PCB dielectricPermittivity");
            if (layer.lossTangent.has_value() &&
                (!std::isfinite(*layer.lossTangent) || *layer.lossTangent < 0.0)) {
                throw std::invalid_argument("PCB lossTangent must be finite and non-negative");
            }
            layers.emplace(layer.id, &layer);
        }

        std::unordered_map<std::string, const PcbComponent*> components;
        for (const PcbComponent& component : pcbDocument.components) {
            reserve(ids, component.id, "PCB component");
            if (!hasText(component.reference) || !finite(component.centerM) ||
                !std::isfinite(component.rotationRad) || !std::isfinite(component.widthM) || component.widthM <= 0.0 ||
                !std::isfinite(component.heightM) || component.heightM <= 0.0) {
                throw std::invalid_argument("PCB components require a reference, finite placement, and positive footprint bounds");
            }
            requireOptionalPositive(component.packageHeightM, "PCB packageHeightM");
            if (component.circuitComponentId.has_value() &&
                (pcbDocument.circuitDocumentId.has_value() ? circuit(*pcbDocument.circuitDocumentId) : nullptr) == nullptr) {
                throw std::invalid_argument("PCB component circuit reference requires a linked circuit document");
            }
            if (component.circuitComponentId.has_value() &&
                circuit(*pcbDocument.circuitDocumentId)->component(*component.circuitComponentId) == nullptr) {
                throw std::invalid_argument("PCB component circuitComponentId references an unknown component");
            }
            components.emplace(component.id, &component);
        }

        const CircuitDocument* linkedCircuit = pcbDocument.circuitDocumentId.has_value()
            ? circuit(*pcbDocument.circuitDocumentId) : nullptr;
        for (const PcbTrace& trace : pcbDocument.traces) {
            reserve(ids, trace.id, "PCB trace");
            if (!std::isfinite(trace.widthM) || trace.widthM <= 0.0 || trace.routeM.size() < 2 ||
                !layers.contains(trace.layerId)) {
                throw std::invalid_argument("PCB traces require a positive width, at least two route points, and a stackup layer");
            }
            for (const Point2M& point : trace.routeM) {
                if (!finite(point)) throw std::invalid_argument("PCB trace route points must be finite metres");
            }
            if (trace.netId.has_value() &&
                (linkedCircuit == nullptr || linkedCircuit->net(*trace.netId) == nullptr)) {
                throw std::invalid_argument("PCB trace netId requires a matching linked circuit net");
            }
        }
        for (const PcbVia& via : pcbDocument.vias) {
            reserve(ids, via.id, "PCB via");
            if (!finite(via.centerM) || !std::isfinite(via.outerDiameterM) || via.outerDiameterM <= 0.0 ||
                !std::isfinite(via.holeDiameterM) || via.holeDiameterM <= 0.0 ||
                via.holeDiameterM >= via.outerDiameterM || via.layerIds.size() < 2) {
                throw std::invalid_argument("PCB vias require finite geometry, a smaller drill, and at least two layers");
            }
            for (const std::string& layerId : via.layerIds) {
                if (!layers.contains(layerId)) throw std::invalid_argument("PCB via references an unknown stackup layer");
            }
            if (via.netId.has_value() && (linkedCircuit == nullptr || linkedCircuit->net(*via.netId) == nullptr)) {
                throw std::invalid_argument("PCB via netId requires a matching linked circuit net");
            }
        }
        for (const PcbPad& pad : pcbDocument.pads) {
            reserve(ids, pad.id, "PCB pad");
            if (!components.contains(pad.componentId) || !finite(pad.centerM) ||
                !std::isfinite(pad.rotationRad) || !std::isfinite(pad.widthM) || pad.widthM <= 0.0 ||
                !std::isfinite(pad.heightM) || pad.heightM <= 0.0 || pad.layerIds.empty()) {
                throw std::invalid_argument("PCB pads require an owning component, finite placement, positive size, and layer");
            }
            for (const std::string& layerId : pad.layerIds) {
                if (!layers.contains(layerId)) throw std::invalid_argument("PCB pad references an unknown stackup layer");
            }
            if (pad.pinId.has_value() && (linkedCircuit == nullptr || linkedCircuit->pin(*pad.pinId) == nullptr)) {
                throw std::invalid_argument("PCB pad pinId requires a matching linked circuit pin");
            }
            if (pad.netId.has_value() && (linkedCircuit == nullptr || linkedCircuit->net(*pad.netId) == nullptr)) {
                throw std::invalid_argument("PCB pad netId requires a matching linked circuit net");
            }
        }
        for (const ElectronicMaterialRegion& region : pcbDocument.materialRegions) {
            reserve(ids, region.id, "PCB material region");
            if (!hasText(region.name) || material(region.materialDefinitionId) == nullptr) {
                throw std::invalid_argument("PCB material regions require a name and common material definition");
            }
            if (region.layerId.has_value() && !layers.contains(*region.layerId)) {
                throw std::invalid_argument("PCB material region references an unknown stackup layer");
            }
            if (!region.outlineM.empty() && region.outlineM.size() < 3) {
                throw std::invalid_argument("PCB material region outline requires at least three points when specified");
            }
            for (const Point2M& point : region.outlineM) {
                if (!finite(point)) throw std::invalid_argument("PCB material region points must be finite metres");
            }
        }
    }
}

}  // namespace datumloop
