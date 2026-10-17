#include "datumloop/io/ElectronicsAdapter.hpp"

#include "datumloop/core/DatumUuid.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace datumloop {
namespace {

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().toStdString());
}

QString q(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string s(const QJsonValue& value, const QString& field, const std::string& fallback = {})
{
    if (value.isUndefined() || value.isNull()) return fallback;
    if (!value.isString()) fail(QStringLiteral("Circuit JSON field '%1' must be a string.").arg(field));
    const QByteArray bytes = value.toString().toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

double numberMm(const QJsonValue& value, const QString& field, double fallback = 0.0)
{
    if (value.isUndefined() || value.isNull()) return fallback;
    if (value.isDouble() && std::isfinite(value.toDouble())) return value.toDouble();
    if (value.isString()) {
        QString text = value.toString().trimmed();
        if (text.endsWith(QStringLiteral("mm"), Qt::CaseInsensitive)) text.chop(2);
        bool ok = false;
        const double parsed = text.trimmed().toDouble(&ok);
        if (ok && std::isfinite(parsed)) return parsed;
    }
    fail(QStringLiteral("Circuit JSON field '%1' must be a finite millimetre value.").arg(field));
}

double rotationDeg(const QJsonValue& value, const QString& field)
{
    if (value.isUndefined() || value.isNull()) return 0.0;
    if (value.isDouble() && std::isfinite(value.toDouble())) return value.toDouble();
    if (value.isString()) {
        QString text = value.toString().trimmed();
        if (text.endsWith(QStringLiteral("deg"), Qt::CaseInsensitive)) text.chop(3);
        bool ok = false;
        const double parsed = text.trimmed().toDouble(&ok);
        if (ok && std::isfinite(parsed)) return parsed;
    }
    fail(QStringLiteral("Circuit JSON field '%1' must be a finite degree value.").arg(field));
}

std::string newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

QString externalId(const QJsonObject& item, const QString& field, const QString& fallbackField = QStringLiteral("id"))
{
    QString result = item.value(field).toString();
    if (result.isEmpty()) result = item.value(fallbackField).toString();
    return result;
}

std::string sourceNetId(const QJsonObject& item)
{
    std::string id = s(item.value(QStringLiteral("pcb_net_id")), QStringLiteral("pcb_net_id"));
    if (id.empty()) id = s(item.value(QStringLiteral("source_net_id")), QStringLiteral("source_net_id"));
    return id;
}

std::string defaultBoardMaterial(const std::string& label)
{
    return label.empty() ? "Board material unspecified" : label + " (properties unspecified)";
}

}  // namespace

ElectronicsImportResult CircuitJsonAdapter::importBytes(const QByteArray& bytes)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(QStringLiteral("Could not parse Circuit JSON: %1").arg(parseError.errorString()));
    }

    QJsonArray elements;
    if (document.isArray()) {
        elements = document.array();
    } else if (document.isObject() && document.object().value(QStringLiteral("elements")).isArray()) {
        elements = document.object().value(QStringLiteral("elements")).toArray();
    } else {
        fail(QStringLiteral("Circuit JSON must be an element array or contain an elements array."));
    }

    std::vector<QJsonObject> boards;
    std::vector<QJsonObject> components;
    std::vector<QJsonObject> pads;
    std::vector<QJsonObject> traces;
    std::vector<QJsonObject> vias;
    std::vector<QJsonObject> nets;
    std::vector<QJsonObject> sourceNets;
    std::vector<QJsonObject> sourcePorts;
    std::vector<QJsonObject> pcbPorts;
    std::size_t skippedPlatedHoles = 0;
    std::unordered_map<std::string, QJsonObject> sourceComponents;
    for (const QJsonValue& value : elements) {
        if (!value.isObject()) continue;
        const QJsonObject item = value.toObject();
        const std::string type = s(item.value(QStringLiteral("type")), QStringLiteral("type"));
        if (type == "pcb_board") boards.push_back(item);
        else if (type == "pcb_component") components.push_back(item);
        else if (type == "pcb_smtpad") pads.push_back(item);
        else if (type == "pcb_plated_hole") ++skippedPlatedHoles;
        else if (type == "pcb_trace") traces.push_back(item);
        else if (type == "pcb_via") vias.push_back(item);
        else if (type == "pcb_net") nets.push_back(item);
        else if (type == "source_net") sourceNets.push_back(item);
        else if (type == "source_port") sourcePorts.push_back(item);
        else if (type == "pcb_port") pcbPorts.push_back(item);
        else if (type == "source_component") {
            const std::string id = s(item.value(QStringLiteral("source_component_id")), QStringLiteral("source_component_id"));
            if (!id.empty()) sourceComponents.emplace(id, item);
        }
    }
    if (boards.size() != 1) fail(QStringLiteral("Circuit JSON import requires exactly one pcb_board element."));
    const QJsonObject boardElement = boards.front();
    const double widthMm = numberMm(boardElement.value(QStringLiteral("width")), QStringLiteral("pcb_board.width"));
    const double heightMm = numberMm(boardElement.value(QStringLiteral("height")), QStringLiteral("pcb_board.height"));
    const double thicknessMm = numberMm(boardElement.value(QStringLiteral("thickness")), QStringLiteral("pcb_board.thickness"));
    if (widthMm <= 0.0 || heightMm <= 0.0 || thicknessMm <= 0.0) {
        fail(QStringLiteral("Circuit JSON board width, height, and thickness must be positive."));
    }

    ElectronicsImportResult result;
    if (skippedPlatedHoles != 0) result.warnings.push_back("Skipped plated-hole pads because drill geometry is not represented by the current PcbPad model.");
    DatumElectronicsModel& model = result.model;
    const std::string boardMaterialLabel = s(boardElement.value(QStringLiteral("material")), QStringLiteral("pcb_board.material"));
    MaterialDefinition boardMaterial;
    boardMaterial.id = newId();
    boardMaterial.name = defaultBoardMaterial(boardMaterialLabel);
    boardMaterial.provenance = {"Circuit JSON pcb_board.material label; composition and physical properties remain unknown."};
    model.materials.push_back(boardMaterial);
    MaterialDefinition copperMaterial;
    copperMaterial.id = newId();
    copperMaterial.name = "Copper (grade and properties unspecified)";
    copperMaterial.provenance = {"PCB copper layer classification; no conductivity, temper, or plating values inferred."};
    model.materials.push_back(copperMaterial);

    std::unordered_map<std::string, std::string> externalComponents;
    std::unordered_map<std::string, std::string> externalPcbComponents;
    std::unordered_map<std::string, std::string> externalNets;
    std::unordered_map<std::string, std::string> externalLayers;
    std::unordered_map<std::string, std::string> externalPadsToPins;
    CircuitDocument circuit;
    circuit.id = newId();
    circuit.name = "Circuit JSON PCB connectivity";
    PcbDocument pcb;
    pcb.id = newId();
    pcb.name = "Circuit JSON PCB";
    pcb.circuitDocumentId = circuit.id;
    pcb.boardMaterialDefinitionId = boardMaterial.id;
    pcb.widthM = widthMm / 1000.0;
    pcb.heightM = heightMm / 1000.0;
    pcb.thicknessM = thicknessMm / 1000.0;
    const QJsonObject boardCenter = boardElement.value(QStringLiteral("center")).toObject();
    pcb.centerM = {numberMm(boardCenter.value(QStringLiteral("x")), QStringLiteral("pcb_board.center.x")) / 1000.0,
                   numberMm(boardCenter.value(QStringLiteral("y")), QStringLiteral("pcb_board.center.y")) / 1000.0};
    pcb.stackup.id = newId();

    auto getLayer = [&](const std::string& name) -> std::string {
        if (name.empty()) return {};
        if (const auto found = externalLayers.find(name); found != externalLayers.end()) return found->second;
        const bool copper = name.ends_with(".Cu") || name == "F.Cu" || name == "B.Cu";
        if (!copper) return {};
        PcbLayer layer;
        layer.id = newId();
        layer.name = name;
        layer.role = PcbLayerRole::Copper;
        layer.materialDefinitionId = copperMaterial.id;
        pcb.stackup.layers.push_back(layer);
        externalLayers.emplace(name, layer.id);
        return layer.id;
    };
    (void)getLayer("F.Cu");
    (void)getLayer("B.Cu");

    std::unordered_map<std::string, std::string> sourceGroupToNet;
    std::unordered_map<std::string, std::string> sourcePortToGroup;
    std::unordered_map<std::string, std::string> pcbPortToSourcePort;
    for (const QJsonObject& item : sourceNets) {
        const std::string sourceId = s(item.value(QStringLiteral("source_net_id")), QStringLiteral("source_net_id"));
        if (sourceId.empty()) continue;
        const std::string internalId = newId();
        const std::string name = s(item.value(QStringLiteral("name")), QStringLiteral("source_net.name"), sourceId);
        const bool ground = item.value(QStringLiteral("is_ground")).toBool(false);
        circuit.nets.push_back({internalId, sourceId, name, ground, {}});
        externalNets.emplace(sourceId, internalId);
        for (const QJsonValue& groupValue : item.value(QStringLiteral("member_source_group_ids")).toArray()) {
            const std::string groupId = s(groupValue, QStringLiteral("member_source_group_ids[]"));
            if (!groupId.empty()) sourceGroupToNet.emplace(groupId, internalId);
        }
    }
    for (const QJsonObject& item : nets) {
        const std::string pcbNetId = s(item.value(QStringLiteral("pcb_net_id")), QStringLiteral("pcb_net_id"));
        const std::string sourceId = s(item.value(QStringLiteral("source_net_id")), QStringLiteral("source_net_id"));
        if (sourceId.empty()) continue;
        const auto sourceNet = externalNets.find(sourceId);
        if (sourceNet != externalNets.end() && !pcbNetId.empty()) externalNets.emplace(pcbNetId, sourceNet->second);
    }
    for (const QJsonObject& item : sourcePorts) {
        const std::string sourcePortId = s(item.value(QStringLiteral("source_port_id")), QStringLiteral("source_port_id"));
        const std::string groupId = s(item.value(QStringLiteral("source_group_id")), QStringLiteral("source_group_id"));
        if (!sourcePortId.empty() && !groupId.empty()) sourcePortToGroup.emplace(sourcePortId, groupId);
    }
    for (const QJsonObject& item : pcbPorts) {
        const std::string pcbPortId = s(item.value(QStringLiteral("pcb_port_id")), QStringLiteral("pcb_port_id"));
        const std::string sourcePortId = s(item.value(QStringLiteral("source_port_id")), QStringLiteral("source_port_id"));
        if (!pcbPortId.empty() && !sourcePortId.empty()) pcbPortToSourcePort.emplace(pcbPortId, sourcePortId);
    }
    auto netForPort = [&](const std::string& pcbPortId) -> std::string {
        const auto pcbPort = pcbPortToSourcePort.find(pcbPortId);
        if (pcbPort == pcbPortToSourcePort.end()) return {};
        const auto sourcePort = sourcePortToGroup.find(pcbPort->second);
        if (sourcePort == sourcePortToGroup.end()) return {};
        const auto group = sourceGroupToNet.find(sourcePort->second);
        return group == sourceGroupToNet.end() ? std::string{} : group->second;
    };
    auto getNet = [&](const std::string& sourceId) -> std::string {
        if (sourceId.empty()) return {};
        if (const auto found = externalNets.find(sourceId); found != externalNets.end()) return found->second;
        const std::string id = newId();
        externalNets.emplace(sourceId, id);
        circuit.nets.push_back({id, sourceId, sourceId, false, {}});
        return id;
    };

    std::unordered_map<std::string, QJsonObject> componentElementById;
    for (const QJsonObject& item : components) {
        const QString extId = externalId(item, QStringLiteral("pcb_component_id"));
        if (extId.isEmpty()) fail(QStringLiteral("Circuit JSON pcb_component is missing pcb_component_id."));
        const std::string raw = extId.toStdString();
        const std::string internalId = newId();
        const std::string pcbComponentId = newId();
        externalComponents.emplace(raw, internalId);
        externalPcbComponents.emplace(raw, pcbComponentId);
        componentElementById.emplace(raw, item);
        const std::string sourceComponentId = s(item.value(QStringLiteral("source_component_id")), QStringLiteral("source_component_id"));
        const auto sourceIt = sourceComponents.find(sourceComponentId);
        const QJsonObject source = sourceIt == sourceComponents.end() ? QJsonObject{} : sourceIt->second;
        const std::string reference = s(source.value(QStringLiteral("name")), QStringLiteral("source_component.name"), raw);
        const std::string displayName = s(source.value(QStringLiteral("display_name")), QStringLiteral("source_component.display_name"), reference);
        CircuitComponent circuitComponent;
        circuitComponent.id = internalId;
        circuitComponent.sourceId = sourceComponentId.empty() ? raw : sourceComponentId;
        circuitComponent.reference = reference;
        circuitComponent.displayName = displayName;
        circuit.components.push_back(circuitComponent);

        PcbComponent pcbComponent;
        pcbComponent.id = pcbComponentId;
        pcbComponent.sourceId = raw;
        pcbComponent.reference = reference;
        pcbComponent.footprint = s(source.value(QStringLiteral("footprint")), QStringLiteral("source_component.footprint"));
        pcbComponent.circuitComponentId = internalId;
        const QJsonObject center = item.value(QStringLiteral("center")).toObject();
        pcbComponent.centerM = {numberMm(center.value(QStringLiteral("x")), QStringLiteral("pcb_component.center.x")) / 1000.0,
                                numberMm(center.value(QStringLiteral("y")), QStringLiteral("pcb_component.center.y")) / 1000.0};
        pcbComponent.rotationRad = rotationDeg(item.value(QStringLiteral("rotation")), QStringLiteral("pcb_component.rotation")) * std::numbers::pi / 180.0;
        pcbComponent.widthM = numberMm(item.value(QStringLiteral("width")), QStringLiteral("pcb_component.width")) / 1000.0;
        pcbComponent.heightM = numberMm(item.value(QStringLiteral("height")), QStringLiteral("pcb_component.height")) / 1000.0;
        pcbComponent.layerName = s(item.value(QStringLiteral("layer")), QStringLiteral("pcb_component.layer"), "F.Cu");
        pcb.components.push_back(std::move(pcbComponent));
    }

    std::unordered_map<std::string, std::vector<std::string>> pinsByNet;
    for (const QJsonObject& item : pads) {
        const QString extPad = externalId(item, QStringLiteral("pcb_smtpad_id"), QStringLiteral("pcb_plated_hole_id"));
        const std::string componentRaw = s(item.value(QStringLiteral("pcb_component_id")), QStringLiteral("pcb_component_id"));
        const auto componentIt = externalComponents.find(componentRaw);
        const auto pcbComponentIt = externalPcbComponents.find(componentRaw);
        if (componentIt == externalComponents.end() || pcbComponentIt == externalPcbComponents.end()) {
            result.warnings.push_back("Skipped a PCB pad without a matching pcb_component_id.");
            continue;
        }
        const std::string componentId = componentIt->second;
        const std::string pcbComponentId = pcbComponentIt->second;
        const std::string pinId = newId();
        const std::string sourcePort = s(item.value(QStringLiteral("pcb_port_id")), QStringLiteral("pcb_port_id"));
        const std::string number = s(item.value(QStringLiteral("pad_number")), QStringLiteral("pad_number"),
                                     sourcePort.empty() ? extPad.toStdString() : sourcePort);
        CircuitPin pin{pinId, componentId, number, number};
        circuit.pins.push_back(pin);
        auto circuitComponentIt = std::find_if(circuit.components.begin(), circuit.components.end(),
                                               [&](const CircuitComponent& c) { return c.id == componentId; });
        circuitComponentIt->pinIds.push_back(pinId);
        if (!extPad.isEmpty()) externalPadsToPins.emplace(extPad.toStdString(), pinId);

        const std::string netId = getNet(sourceNetId(item));
        const std::string portNetId = netId.empty() ? netForPort(sourcePort) : netId;
        if (!portNetId.empty()) pinsByNet[portNetId].push_back(pinId);
        PcbPad pad;
        pad.id = newId();
        pad.sourceId = extPad.toStdString();
        pad.componentId = pcbComponentId;
        pad.pinId = pinId;
        if (!portNetId.empty()) pad.netId = portNetId;
        const std::string shape = s(item.value(QStringLiteral("shape")), QStringLiteral("shape"));
        pad.shape = shape == "circle" ? PcbPadShape::Circle : PcbPadShape::Rectangle;
        pad.centerM = {numberMm(item.value(QStringLiteral("x")), QStringLiteral("pcb_smtpad.x")) / 1000.0,
                       numberMm(item.value(QStringLiteral("y")), QStringLiteral("pcb_smtpad.y")) / 1000.0};
        if (shape == "circle") {
            const double radius = numberMm(item.value(QStringLiteral("radius")), QStringLiteral("pcb_smtpad.radius"));
            pad.widthM = 2.0 * radius / 1000.0;
            pad.heightM = pad.widthM;
        } else {
            pad.widthM = numberMm(item.value(QStringLiteral("width")), QStringLiteral("pcb_smtpad.width")) / 1000.0;
            pad.heightM = numberMm(item.value(QStringLiteral("height")), QStringLiteral("pcb_smtpad.height")) / 1000.0;
        }
        pad.rotationRad = rotationDeg(item.value(QStringLiteral("ccw_rotation")), QStringLiteral("pcb_smtpad.ccw_rotation")) * std::numbers::pi / 180.0;
        if (item.value(QStringLiteral("ccw_rotation")).isUndefined()) {
            pad.rotationRad = rotationDeg(item.value(QStringLiteral("rotation")), QStringLiteral("pcb_smtpad.rotation")) * std::numbers::pi / 180.0;
        }
        const std::string layerName = s(item.value(QStringLiteral("layer")), QStringLiteral("pcb_smtpad.layer"), "F.Cu");
        const std::string layerId = getLayer(layerName);
        if (!layerId.empty()) pad.layerIds.push_back(layerId);
        pcb.pads.push_back(std::move(pad));
    }
    for (CircuitNet& net : circuit.nets) {
        if (const auto found = pinsByNet.find(net.id); found != pinsByNet.end()) net.pinIds = found->second;
    }

    for (const QJsonObject& item : traces) {
        const QJsonValue routeValue = item.value(QStringLiteral("route"));
        if (!routeValue.isArray()) continue;
        PcbTrace trace;
        trace.id = newId();
        trace.sourceId = externalId(item, QStringLiteral("pcb_trace_id")).toStdString();
        std::string netId = getNet(sourceNetId(item));
        if (!netId.empty()) trace.netId = netId;
        for (const QJsonValue& pointValue : routeValue.toArray()) {
            if (!pointValue.isObject()) continue;
            const QJsonObject point = pointValue.toObject();
            if (s(point.value(QStringLiteral("route_type")), QStringLiteral("route_type"), "wire") != "wire") continue;
            if (netId.empty()) {
                for (const QString& portField : {QStringLiteral("start_pcb_port_id"), QStringLiteral("end_pcb_port_id")}) {
                    const std::string candidate = netForPort(s(point.value(portField), portField));
                    if (!candidate.empty()) {
                        netId = candidate;
                        trace.netId = netId;
                        break;
                    }
                }
            }
            const double width = numberMm(point.value(QStringLiteral("width")), QStringLiteral("route.width"));
            const std::string layerId = getLayer(s(point.value(QStringLiteral("layer")), QStringLiteral("route.layer"), "F.Cu"));
            if (width > 0.0 && trace.widthM == 0.0) {
                trace.widthM = width / 1000.0;
                trace.layerId = layerId;
            }
            trace.routeM.push_back({numberMm(point.value(QStringLiteral("x")), QStringLiteral("route.x")) / 1000.0,
                                    numberMm(point.value(QStringLiteral("y")), QStringLiteral("route.y")) / 1000.0});
        }
        if (trace.routeM.size() >= 2 && trace.widthM > 0.0 && !trace.layerId.empty()) pcb.traces.push_back(std::move(trace));
        else result.warnings.push_back("Skipped a pcb_trace without two wire points and a positive width.");
    }
    for (const QJsonObject& item : vias) {
        PcbVia via;
        via.id = newId();
        via.sourceId = externalId(item, QStringLiteral("pcb_via_id")).toStdString();
        const std::string netId = getNet(sourceNetId(item));
        if (!netId.empty()) via.netId = netId;
        via.centerM = {numberMm(item.value(QStringLiteral("x")), QStringLiteral("pcb_via.x")) / 1000.0,
                       numberMm(item.value(QStringLiteral("y")), QStringLiteral("pcb_via.y")) / 1000.0};
        via.outerDiameterM = numberMm(item.value(QStringLiteral("outer_diameter")), QStringLiteral("pcb_via.outer_diameter")) / 1000.0;
        via.holeDiameterM = numberMm(item.value(QStringLiteral("hole_diameter")), QStringLiteral("pcb_via.hole_diameter")) / 1000.0;
        for (const QJsonValue& layerValue : item.value(QStringLiteral("layers")).toArray()) {
            const std::string layerId = getLayer(s(layerValue, QStringLiteral("pcb_via.layers[]")));
            if (!layerId.empty()) via.layerIds.push_back(layerId);
        }
        if (via.layerIds.empty()) {
            via.layerIds = {getLayer("F.Cu"), getLayer("B.Cu")};
        }
        pcb.vias.push_back(std::move(via));
    }

    model.circuits.push_back(std::move(circuit));
    model.pcbs.push_back(std::move(pcb));
    try {
        model.validate();
    } catch (const std::invalid_argument& error) {
        fail(QStringLiteral("Imported Circuit JSON model is invalid: %1").arg(QString::fromUtf8(error.what())));
    }
    return result;
}

QByteArray CircuitJsonAdapter::exportPcb(const DatumElectronicsModel& model, const std::string& pcbId)
{
    model.validate();
    const PcbDocument* pcb = model.pcb(pcbId);
    if (pcb == nullptr) fail(QStringLiteral("Requested PCB is not present in the electronics model."));
    const CircuitDocument* circuit = pcb->circuitDocumentId.has_value() ? model.circuit(*pcb->circuitDocumentId) : nullptr;
    QJsonArray elements;
    QJsonObject board{{QStringLiteral("type"), QStringLiteral("pcb_board")},
                      {QStringLiteral("pcb_board_id"), q(pcb->id)},
                      {QStringLiteral("center"), QJsonObject{{QStringLiteral("x"), pcb->centerM[0] * 1000.0},
                                                               {QStringLiteral("y"), pcb->centerM[1] * 1000.0}}},
                      {QStringLiteral("width"), pcb->widthM * 1000.0},
                      {QStringLiteral("height"), pcb->heightM * 1000.0},
                      {QStringLiteral("thickness"), pcb->thicknessM * 1000.0},
                      {QStringLiteral("material"), QStringLiteral("fr4")},
                      {QStringLiteral("num_layers"), static_cast<int>(std::count_if(pcb->stackup.layers.begin(), pcb->stackup.layers.end(),
                           [](const PcbLayer& layer) { return layer.role == PcbLayerRole::Copper; }))}};
    elements.append(board);

    auto sourceId = [](const std::string& source, const std::string& id) -> QString {
        return q(source.empty() ? id : source);
    };
    if (circuit != nullptr) {
        for (const CircuitNet& net : circuit->nets) {
            QJsonArray memberGroups;
            for (const std::string& pinId : net.pinIds) memberGroups.append(q(pinId));
            elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("source_net")},
                                        {QStringLiteral("source_net_id"), sourceId(net.sourceId, net.id)},
                                        {QStringLiteral("name"), q(net.name)},
                                        {QStringLiteral("member_source_group_ids"), memberGroups}});
            elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("pcb_net")},
                                        {QStringLiteral("pcb_net_id"), sourceId(net.sourceId, net.id)},
                                        {QStringLiteral("source_net_id"), sourceId(net.sourceId, net.id)}});
        }
        for (const CircuitComponent& component : circuit->components) {
            elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("source_component")},
                                        {QStringLiteral("source_component_id"), sourceId(component.sourceId, component.id)},
                                        {QStringLiteral("name"), q(component.reference)},
                                        {QStringLiteral("display_name"), q(component.displayName)}});
            for (const std::string& pinId : component.pinIds) {
                if (const CircuitPin* pin = circuit->pin(pinId)) {
                    elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("source_port")},
                                                {QStringLiteral("source_port_id"), q(pin->id)},
                                                {QStringLiteral("source_component_id"), sourceId(component.sourceId, component.id)},
                                                {QStringLiteral("source_group_id"), q(pin->id)},
                                                {QStringLiteral("name"), q(pin->name)},
                                                {QStringLiteral("pin_number"), QString::fromStdString(pin->number).toInt()}});
                }
            }
        }
    }
    std::unordered_map<std::string, std::string> layerNames;
    for (const PcbLayer& layer : pcb->stackup.layers) layerNames.emplace(layer.id, layer.name);
    for (const PcbComponent& component : pcb->components) {
        elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("pcb_component")},
                                    {QStringLiteral("pcb_component_id"), sourceId(component.sourceId, component.id)},
                                    {QStringLiteral("source_component_id"), [&]() {
                                         if (component.circuitComponentId.has_value() && circuit != nullptr) {
                                             const CircuitComponent* linked = circuit->component(*component.circuitComponentId);
                                             if (linked != nullptr) return sourceId(linked->sourceId, linked->id);
                                         }
                                         return sourceId(component.sourceId, component.id);
                                     }()},
                                    {QStringLiteral("center"), QJsonObject{{QStringLiteral("x"), component.centerM[0] * 1000.0},
                                                                            {QStringLiteral("y"), component.centerM[1] * 1000.0}}},
                                    {QStringLiteral("layer"), q(component.layerName)},
                                    {QStringLiteral("rotation"), component.rotationRad * 180.0 / std::numbers::pi},
                                    {QStringLiteral("width"), component.widthM * 1000.0},
                                    {QStringLiteral("height"), component.heightM * 1000.0}});
    }
    for (const PcbPad& pad : pcb->pads) {
        QStringList layers;
        for (const std::string& layerId : pad.layerIds) {
            if (const auto found = layerNames.find(layerId); found != layerNames.end()) layers.push_back(q(found->second));
        }
        QJsonObject item{{QStringLiteral("type"), QStringLiteral("pcb_smtpad")},
                         {QStringLiteral("pcb_smtpad_id"), sourceId(pad.sourceId, pad.id)},
                         {QStringLiteral("pcb_component_id"), [&]() {
                              const auto component = std::find_if(pcb->components.begin(), pcb->components.end(),
                                  [&](const PcbComponent& c) { return c.id == pad.componentId; });
                              if (component == pcb->components.end()) fail(QStringLiteral("PCB pad references a missing component."));
                              return sourceId(component->sourceId, component->id);
                          }()},
                         {QStringLiteral("x"), pad.centerM[0] * 1000.0}, {QStringLiteral("y"), pad.centerM[1] * 1000.0},
                         {QStringLiteral("shape"), pad.shape == PcbPadShape::Circle ? QStringLiteral("circle") :
                              (std::abs(pad.rotationRad) > 1e-12 ? QStringLiteral("rotated_rect") : QStringLiteral("rect"))},
                         {QStringLiteral("layer"), layers.isEmpty() ? QStringLiteral("F.Cu") : layers.front()}};
        if (pad.shape == PcbPadShape::Circle) item.insert(QStringLiteral("radius"), pad.widthM * 500.0);
        else {
            item.insert(QStringLiteral("width"), pad.widthM * 1000.0);
            item.insert(QStringLiteral("height"), pad.heightM * 1000.0);
            if (std::abs(pad.rotationRad) > 1e-12) {
                item.insert(QStringLiteral("ccw_rotation"), pad.rotationRad * 180.0 / std::numbers::pi);
            }
        }
        if (pad.pinId.has_value() && circuit != nullptr) {
            if (const CircuitPin* pin = circuit->pin(*pad.pinId)) {
                item.insert(QStringLiteral("pcb_port_id"), q(pin->id));
                QJsonArray portLayers;
                for (const QString& layer : layers) portLayers.append(layer);
                elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("pcb_port")},
                                            {QStringLiteral("pcb_port_id"), q(pin->id)},
                                            {QStringLiteral("source_port_id"), q(pin->id)},
                                            {QStringLiteral("pcb_component_id"), item.value(QStringLiteral("pcb_component_id"))},
                                            {QStringLiteral("x"), pad.centerM[0] * 1000.0},
                                            {QStringLiteral("y"), pad.centerM[1] * 1000.0},
                                            {QStringLiteral("layers"), portLayers}});
            }
        }
        elements.append(item);
    }
    for (const PcbTrace& trace : pcb->traces) {
        QJsonArray route;
        const std::string layerName = layerNames.contains(trace.layerId) ? layerNames.at(trace.layerId) : "F.Cu";
        for (std::size_t index = 0; index < trace.routeM.size(); ++index) {
            const Point2M& point = trace.routeM[index];
            QJsonObject routePoint{{QStringLiteral("route_type"), QStringLiteral("wire")},
                                   {QStringLiteral("x"), point[0] * 1000.0}, {QStringLiteral("y"), point[1] * 1000.0},
                                   {QStringLiteral("width"), trace.widthM * 1000.0}, {QStringLiteral("layer"), q(layerName)}};
            for (const PcbPad& pad : pcb->pads) {
                if (!pad.pinId.has_value() || circuit == nullptr ||
                    std::abs(pad.centerM[0] - point[0]) > 1e-9 || std::abs(pad.centerM[1] - point[1]) > 1e-9) continue;
                if (index == 0) routePoint.insert(QStringLiteral("start_pcb_port_id"), q(*pad.pinId));
                if (index + 1 == trace.routeM.size()) routePoint.insert(QStringLiteral("end_pcb_port_id"), q(*pad.pinId));
            }
            route.append(routePoint);
        }
        QJsonObject item{{QStringLiteral("type"), QStringLiteral("pcb_trace")},
                         {QStringLiteral("pcb_trace_id"), sourceId(trace.sourceId, trace.id)},
                         {QStringLiteral("route"), route}};
        if (trace.netId.has_value() && circuit != nullptr) {
            if (const CircuitNet* net = circuit->net(*trace.netId)) item.insert(QStringLiteral("source_net_id"), sourceId(net->sourceId, net->id));
        }
        elements.append(item);
    }
    for (const PcbVia& via : pcb->vias) {
        QJsonArray layers;
        for (const std::string& layerId : via.layerIds) {
            if (const auto found = layerNames.find(layerId); found != layerNames.end()) layers.append(q(found->second));
        }
        QJsonObject item{{QStringLiteral("type"), QStringLiteral("pcb_via")},
                         {QStringLiteral("pcb_via_id"), sourceId(via.sourceId, via.id)},
                         {QStringLiteral("x"), via.centerM[0] * 1000.0}, {QStringLiteral("y"), via.centerM[1] * 1000.0},
                         {QStringLiteral("outer_diameter"), via.outerDiameterM * 1000.0},
                         {QStringLiteral("hole_diameter"), via.holeDiameterM * 1000.0}, {QStringLiteral("layers"), layers}};
        if (via.netId.has_value() && circuit != nullptr) {
            if (const CircuitNet* net = circuit->net(*via.netId)) item.insert(QStringLiteral("source_net_id"), sourceId(net->sourceId, net->id));
        }
        elements.append(item);
    }
    return QJsonDocument(elements).toJson(QJsonDocument::Indented);
}

}  // namespace datumloop
