#include "datumloop/io/ElectronicsProjectCodec.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cmath>
#include <stdexcept>

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

std::string s(const QJsonValue& value, const QString& field)
{
    if (!value.isString()) fail(QStringLiteral("Electronics field '%1' must be a string.").arg(field));
    const QByteArray bytes = value.toString().toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

double number(const QJsonValue& value, const QString& field)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        fail(QStringLiteral("Electronics field '%1' must be a finite number.").arg(field));
    }
    return value.toDouble();
}

bool boolean(const QJsonValue& value, const QString& field)
{
    if (!value.isBool()) fail(QStringLiteral("Electronics field '%1' must be a boolean.").arg(field));
    return value.toBool();
}

QJsonValue optionalNumber(const std::optional<double>& value)
{
    return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

std::optional<double> readOptionalNumber(const QJsonValue& value, const QString& field)
{
    if (value.isNull()) return std::nullopt;
    return number(value, field);
}

QJsonValue optionalString(const std::optional<std::string>& value)
{
    return value.has_value() ? QJsonValue(q(*value)) : QJsonValue(QJsonValue::Null);
}

std::optional<std::string> readOptionalString(const QJsonValue& value, const QString& field)
{
    if (value.isNull()) return std::nullopt;
    return s(value, field);
}

QJsonArray strings(const std::vector<std::string>& values)
{
    QJsonArray array;
    for (const std::string& value : values) array.append(q(value));
    return array;
}

std::vector<std::string> readStrings(const QJsonValue& value, const QString& field)
{
    if (!value.isArray()) fail(QStringLiteral("Electronics field '%1' must be an array.").arg(field));
    std::vector<std::string> values;
    for (const QJsonValue& entry : value.toArray()) values.push_back(s(entry, field));
    return values;
}

QJsonArray vector2(const Point2M& value)
{
    return {value[0], value[1]};
}

Point2M readVector2(const QJsonValue& value, const QString& field)
{
    if (!value.isArray() || value.toArray().size() != 2) {
        fail(QStringLiteral("Electronics field '%1' must contain two coordinates in metres.").arg(field));
    }
    const QJsonArray array = value.toArray();
    return {number(array.at(0), field + QStringLiteral("[0]")),
            number(array.at(1), field + QStringLiteral("[1]"))};
}

QJsonObject encodeMicrostructure(const MaterialMicrostructure& value)
{
    return {{QStringLiteral("grainSizeM"), optionalNumber(value.grainSizeM)},
            {QStringLiteral("porosityFraction"), optionalNumber(value.porosityFraction)},
            {QStringLiteral("defectDensityPerM2"), optionalNumber(value.defectDensityPerM2)},
            {QStringLiteral("residualStressPa"), optionalNumber(value.residualStressPa)},
            {QStringLiteral("textureDescription"), q(value.textureDescription)}};
}

MaterialMicrostructure readMicrostructure(const QJsonValue& value, const QString& field)
{
    if (!value.isObject()) fail(QStringLiteral("Electronics field '%1' must be an object.").arg(field));
    const QJsonObject object = value.toObject();
    MaterialMicrostructure result;
    result.grainSizeM = readOptionalNumber(object.value(QStringLiteral("grainSizeM")), field + QStringLiteral(".grainSizeM"));
    result.porosityFraction = readOptionalNumber(object.value(QStringLiteral("porosityFraction")), field + QStringLiteral(".porosityFraction"));
    result.defectDensityPerM2 = readOptionalNumber(object.value(QStringLiteral("defectDensityPerM2")), field + QStringLiteral(".defectDensityPerM2"));
    result.residualStressPa = readOptionalNumber(object.value(QStringLiteral("residualStressPa")), field + QStringLiteral(".residualStressPa"));
    result.textureDescription = s(object.value(QStringLiteral("textureDescription")), field + QStringLiteral(".textureDescription"));
    return result;
}

QJsonObject encodeMaterial(const MaterialDefinition& material)
{
    QJsonArray composition;
    for (const MaterialConstituent& item : material.composition) {
        composition.append(QJsonObject{{QStringLiteral("chemicalSymbol"), q(item.chemicalSymbol)},
                                       {QStringLiteral("massFraction"), item.massFraction}});
    }
    QJsonArray phases;
    for (const MaterialPhaseDefinition& item : material.phases) {
        phases.append(QJsonObject{{QStringLiteral("name"), q(item.name)},
                                  {QStringLiteral("crystalStructure"), q(item.crystalStructure)}});
    }
    QJsonArray history;
    for (const MaterialProcessHistoryEntry& item : material.processHistory) {
        history.append(QJsonObject{{QStringLiteral("process"), q(item.process)},
                                   {QStringLiteral("recordedAt"), q(item.recordedAt)},
                                   {QStringLiteral("temperatureK"), optionalNumber(item.temperatureK)},
                                   {QStringLiteral("pressurePa"), optionalNumber(item.pressurePa)},
                                   {QStringLiteral("durationS"), optionalNumber(item.durationS)},
                                   {QStringLiteral("provenance"), q(item.provenance)}});
    }
    QJsonArray properties;
    for (const MaterialPropertyModel& item : material.propertyModels) {
        QJsonArray table;
        for (const MaterialPropertyPoint& point : item.table) {
            table.append(QJsonObject{{QStringLiteral("independentValue"), point.independentValue},
                                     {QStringLiteral("propertyValue"), point.propertyValue}});
        }
        const QString law = item.law == MaterialPropertyLaw::Constant
            ? QStringLiteral("constant") : QStringLiteral("piecewise_linear");
        properties.append(QJsonObject{{QStringLiteral("propertyName"), q(item.propertyName)},
                                      {QStringLiteral("propertyUnit"), q(item.propertyUnit)},
                                      {QStringLiteral("independentVariable"), q(item.independentVariable)},
                                      {QStringLiteral("independentUnit"), q(item.independentUnit)},
                                      {QStringLiteral("law"), law},
                                      {QStringLiteral("constantValue"), optionalNumber(item.constantValue)},
                                      {QStringLiteral("table"), table},
                                      {QStringLiteral("source"), q(item.source)},
                                      {QStringLiteral("sourceVersion"), q(item.sourceVersion)},
                                      {QStringLiteral("confidence"), optionalNumber(item.confidence)}});
    }
    return {{QStringLiteral("id"), q(material.id)},
            {QStringLiteral("name"), q(material.name)},
            {QStringLiteral("crystalStructure"), q(material.crystalStructure)},
            {QStringLiteral("composition"), composition},
            {QStringLiteral("phases"), phases},
            {QStringLiteral("microstructure"), material.microstructure.has_value()
                 ? QJsonValue(encodeMicrostructure(*material.microstructure)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("processHistory"), history},
            {QStringLiteral("propertyModels"), properties},
            {QStringLiteral("provenance"), strings(material.provenance)}};
}

MaterialDefinition readMaterial(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("Material definition entries must be objects."));
    const QJsonObject object = value.toObject();
    MaterialDefinition result;
    result.id = s(object.value(QStringLiteral("id")), QStringLiteral("material.id"));
    result.name = s(object.value(QStringLiteral("name")), QStringLiteral("material.name"));
    result.crystalStructure = s(object.value(QStringLiteral("crystalStructure")), QStringLiteral("material.crystalStructure"));
    const QJsonValue composition = object.value(QStringLiteral("composition"));
    if (!composition.isArray()) fail(QStringLiteral("Material composition must be an array."));
    for (const QJsonValue& entry : composition.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Material constituent entries must be objects."));
        const QJsonObject item = entry.toObject();
        result.composition.push_back({s(item.value(QStringLiteral("chemicalSymbol")), QStringLiteral("chemicalSymbol")),
                                      number(item.value(QStringLiteral("massFraction")), QStringLiteral("massFraction"))});
    }
    const QJsonValue phases = object.value(QStringLiteral("phases"));
    if (!phases.isArray()) fail(QStringLiteral("Material phases must be an array."));
    for (const QJsonValue& entry : phases.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Material phase entries must be objects."));
        const QJsonObject item = entry.toObject();
        result.phases.push_back({s(item.value(QStringLiteral("name")), QStringLiteral("phase.name")),
                                 s(item.value(QStringLiteral("crystalStructure")), QStringLiteral("phase.crystalStructure"))});
    }
    const QJsonValue microstructure = object.value(QStringLiteral("microstructure"));
    if (microstructure.isObject()) result.microstructure = readMicrostructure(microstructure, QStringLiteral("material.microstructure"));
    else if (!microstructure.isNull()) fail(QStringLiteral("Material microstructure must be an object or null."));
    const QJsonValue history = object.value(QStringLiteral("processHistory"));
    if (!history.isArray()) fail(QStringLiteral("Material processHistory must be an array."));
    for (const QJsonValue& entry : history.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Material process history entries must be objects."));
        const QJsonObject item = entry.toObject();
        result.processHistory.push_back({s(item.value(QStringLiteral("process")), QStringLiteral("process")),
                                         s(item.value(QStringLiteral("recordedAt")), QStringLiteral("recordedAt")),
                                         readOptionalNumber(item.value(QStringLiteral("temperatureK")), QStringLiteral("temperatureK")),
                                         readOptionalNumber(item.value(QStringLiteral("pressurePa")), QStringLiteral("pressurePa")),
                                         readOptionalNumber(item.value(QStringLiteral("durationS")), QStringLiteral("durationS")),
                                         s(item.value(QStringLiteral("provenance")), QStringLiteral("provenance"))});
    }
    const QJsonValue properties = object.value(QStringLiteral("propertyModels"));
    if (!properties.isArray()) fail(QStringLiteral("Material propertyModels must be an array."));
    for (const QJsonValue& entry : properties.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Material property model entries must be objects."));
        const QJsonObject item = entry.toObject();
        MaterialPropertyModel property;
        property.propertyName = s(item.value(QStringLiteral("propertyName")), QStringLiteral("propertyName"));
        property.propertyUnit = s(item.value(QStringLiteral("propertyUnit")), QStringLiteral("propertyUnit"));
        property.independentVariable = s(item.value(QStringLiteral("independentVariable")), QStringLiteral("independentVariable"));
        property.independentUnit = s(item.value(QStringLiteral("independentUnit")), QStringLiteral("independentUnit"));
        const std::string law = s(item.value(QStringLiteral("law")), QStringLiteral("property.law"));
        if (law == "constant") property.law = MaterialPropertyLaw::Constant;
        else if (law == "piecewise_linear") property.law = MaterialPropertyLaw::PiecewiseLinear;
        else fail(QStringLiteral("Unknown material property law '%1'.").arg(q(law)));
        property.constantValue = readOptionalNumber(item.value(QStringLiteral("constantValue")), QStringLiteral("constantValue"));
        const QJsonValue table = item.value(QStringLiteral("table"));
        if (!table.isArray()) fail(QStringLiteral("Material property table must be an array."));
        for (const QJsonValue& pointValue : table.toArray()) {
            if (!pointValue.isObject()) fail(QStringLiteral("Material property table points must be objects."));
            const QJsonObject point = pointValue.toObject();
            property.table.push_back({number(point.value(QStringLiteral("independentValue")), QStringLiteral("independentValue")),
                                      number(point.value(QStringLiteral("propertyValue")), QStringLiteral("propertyValue"))});
        }
        property.source = s(item.value(QStringLiteral("source")), QStringLiteral("source"));
        property.sourceVersion = s(item.value(QStringLiteral("sourceVersion")), QStringLiteral("sourceVersion"));
        property.confidence = readOptionalNumber(item.value(QStringLiteral("confidence")), QStringLiteral("confidence"));
        result.propertyModels.push_back(std::move(property));
    }
    result.provenance = readStrings(object.value(QStringLiteral("provenance")), QStringLiteral("material.provenance"));
    return result;
}

QJsonArray phaseFractions(const std::vector<MaterialPhaseFraction>& values)
{
    QJsonArray array;
    for (const auto& item : values) array.append(QJsonObject{{QStringLiteral("phaseName"), q(item.phaseName)},
                                                             {QStringLiteral("fraction"), item.fraction}});
    return array;
}

std::vector<MaterialPhaseFraction> readPhaseFractions(const QJsonValue& value)
{
    if (!value.isArray()) fail(QStringLiteral("Material phaseFractions must be an array."));
    std::vector<MaterialPhaseFraction> result;
    for (const QJsonValue& entry : value.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Material phase fraction entries must be objects."));
        const QJsonObject object = entry.toObject();
        result.push_back({s(object.value(QStringLiteral("phaseName")), QStringLiteral("phaseName")),
                          number(object.value(QStringLiteral("fraction")), QStringLiteral("fraction"))});
    }
    return result;
}

QJsonObject encodeCircuit(const CircuitDocument& circuit)
{
    QJsonArray components;
    for (const CircuitComponent& item : circuit.components) {
        components.append(QJsonObject{{QStringLiteral("id"), q(item.id)},
                                      {QStringLiteral("sourceId"), q(item.sourceId)},
                                      {QStringLiteral("reference"), q(item.reference)},
                                      {QStringLiteral("displayName"), q(item.displayName)},
                                      {QStringLiteral("kind"), q(circuitComponentKindName(item.kind))},
                                      {QStringLiteral("electricalValue"), optionalNumber(item.electricalValue)},
                                      {QStringLiteral("electricalValueUnit"), q(item.electricalValueUnit)},
                                      {QStringLiteral("pinIds"), strings(item.pinIds)}});
    }
    QJsonArray pins;
    for (const CircuitPin& item : circuit.pins) {
        pins.append(QJsonObject{{QStringLiteral("id"), q(item.id)},
                                {QStringLiteral("componentId"), q(item.componentId)},
                                {QStringLiteral("number"), q(item.number)},
                                {QStringLiteral("name"), q(item.name)}});
    }
    QJsonArray nets;
    for (const CircuitNet& item : circuit.nets) {
        nets.append(QJsonObject{{QStringLiteral("id"), q(item.id)},
                                {QStringLiteral("sourceId"), q(item.sourceId)},
                                {QStringLiteral("name"), q(item.name)},
                                {QStringLiteral("referenceNode"), item.referenceNode},
                                {QStringLiteral("pinIds"), strings(item.pinIds)}});
    }
    return {{QStringLiteral("id"), q(circuit.id)}, {QStringLiteral("name"), q(circuit.name)},
            {QStringLiteral("components"), components}, {QStringLiteral("pins"), pins}, {QStringLiteral("nets"), nets}};
}

CircuitDocument readCircuit(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("Circuit document entries must be objects."));
    const QJsonObject object = value.toObject();
    CircuitDocument result{s(object.value(QStringLiteral("id")), QStringLiteral("circuit.id")),
                           s(object.value(QStringLiteral("name")), QStringLiteral("circuit.name")), {}, {}, {}};
    const QJsonValue components = object.value(QStringLiteral("components"));
    const QJsonValue pins = object.value(QStringLiteral("pins"));
    const QJsonValue nets = object.value(QStringLiteral("nets"));
    if (!components.isArray() || !pins.isArray() || !nets.isArray()) {
        fail(QStringLiteral("Circuit components, pins, and nets must be arrays."));
    }
    for (const QJsonValue& entry : components.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Circuit component entries must be objects."));
        const QJsonObject item = entry.toObject();
        CircuitComponent component;
        component.id = s(item.value(QStringLiteral("id")), QStringLiteral("component.id"));
        component.sourceId = s(item.value(QStringLiteral("sourceId")), QStringLiteral("component.sourceId"));
        component.reference = s(item.value(QStringLiteral("reference")), QStringLiteral("component.reference"));
        component.displayName = s(item.value(QStringLiteral("displayName")), QStringLiteral("component.displayName"));
        const std::string kind = s(item.value(QStringLiteral("kind")), QStringLiteral("component.kind"));
        const auto parsedKind = circuitComponentKindFromName(kind);
        if (!parsedKind.has_value()) fail(QStringLiteral("Unknown circuit component kind '%1'.").arg(q(kind)));
        component.kind = *parsedKind;
        component.electricalValue = readOptionalNumber(item.value(QStringLiteral("electricalValue")), QStringLiteral("component.electricalValue"));
        component.electricalValueUnit = s(item.value(QStringLiteral("electricalValueUnit")), QStringLiteral("component.electricalValueUnit"));
        component.pinIds = readStrings(item.value(QStringLiteral("pinIds")), QStringLiteral("component.pinIds"));
        result.components.push_back(std::move(component));
    }
    for (const QJsonValue& entry : pins.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Circuit pin entries must be objects."));
        const QJsonObject item = entry.toObject();
        result.pins.push_back({s(item.value(QStringLiteral("id")), QStringLiteral("pin.id")),
                               s(item.value(QStringLiteral("componentId")), QStringLiteral("pin.componentId")),
                               s(item.value(QStringLiteral("number")), QStringLiteral("pin.number")),
                               s(item.value(QStringLiteral("name")), QStringLiteral("pin.name"))});
    }
    for (const QJsonValue& entry : nets.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Circuit net entries must be objects."));
        const QJsonObject item = entry.toObject();
        result.nets.push_back({s(item.value(QStringLiteral("id")), QStringLiteral("net.id")),
                               s(item.value(QStringLiteral("sourceId")), QStringLiteral("net.sourceId")),
                               s(item.value(QStringLiteral("name")), QStringLiteral("net.name")),
                               boolean(item.value(QStringLiteral("referenceNode")), QStringLiteral("net.referenceNode")),
                               readStrings(item.value(QStringLiteral("pinIds")), QStringLiteral("net.pinIds"))});
    }
    return result;
}

QJsonObject encodeLayer(const PcbLayer& layer)
{
    return {{QStringLiteral("id"), q(layer.id)}, {QStringLiteral("name"), q(layer.name)},
            {QStringLiteral("role"), q(pcbLayerRoleName(layer.role))},
            {QStringLiteral("materialDefinitionId"), q(layer.materialDefinitionId)},
            {QStringLiteral("thicknessM"), optionalNumber(layer.thicknessM)},
            {QStringLiteral("surfaceRoughnessM"), optionalNumber(layer.surfaceRoughnessM)},
            {QStringLiteral("dielectricPermittivity"), optionalNumber(layer.dielectricPermittivity)},
            {QStringLiteral("lossTangent"), optionalNumber(layer.lossTangent)}};
}

PcbLayer readLayer(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("PCB layer entries must be objects."));
    const QJsonObject object = value.toObject();
    const std::string role = s(object.value(QStringLiteral("role")), QStringLiteral("layer.role"));
    const auto parsedRole = pcbLayerRoleFromName(role);
    if (!parsedRole.has_value()) fail(QStringLiteral("Unknown PCB layer role '%1'.").arg(q(role)));
    PcbLayer result;
    result.id = s(object.value(QStringLiteral("id")), QStringLiteral("layer.id"));
    result.name = s(object.value(QStringLiteral("name")), QStringLiteral("layer.name"));
    result.role = *parsedRole;
    result.materialDefinitionId = s(object.value(QStringLiteral("materialDefinitionId")), QStringLiteral("layer.materialDefinitionId"));
    result.thicknessM = readOptionalNumber(object.value(QStringLiteral("thicknessM")), QStringLiteral("layer.thicknessM"));
    result.surfaceRoughnessM = readOptionalNumber(object.value(QStringLiteral("surfaceRoughnessM")), QStringLiteral("layer.surfaceRoughnessM"));
    result.dielectricPermittivity = readOptionalNumber(object.value(QStringLiteral("dielectricPermittivity")), QStringLiteral("layer.dielectricPermittivity"));
    result.lossTangent = readOptionalNumber(object.value(QStringLiteral("lossTangent")), QStringLiteral("layer.lossTangent"));
    return result;
}

QJsonObject encodePcb(const PcbDocument& pcb)
{
    QJsonArray layers;
    for (const PcbLayer& item : pcb.stackup.layers) layers.append(encodeLayer(item));
    QJsonArray components;
    for (const PcbComponent& item : pcb.components) {
        components.append(QJsonObject{{QStringLiteral("id"), q(item.id)},
                                      {QStringLiteral("sourceId"), q(item.sourceId)},
                                      {QStringLiteral("reference"), q(item.reference)},
                                      {QStringLiteral("footprint"), q(item.footprint)},
                                      {QStringLiteral("circuitComponentId"), optionalString(item.circuitComponentId)},
                                      {QStringLiteral("centerM"), vector2(item.centerM)},
                                      {QStringLiteral("rotationRad"), item.rotationRad},
                                      {QStringLiteral("widthM"), item.widthM},
                                      {QStringLiteral("heightM"), item.heightM},
                                      {QStringLiteral("packageHeightM"), optionalNumber(item.packageHeightM)},
                                      {QStringLiteral("layerName"), q(item.layerName)}});
    }
    QJsonArray traces;
    for (const PcbTrace& item : pcb.traces) {
        QJsonArray route;
        for (const Point2M& point : item.routeM) route.append(vector2(point));
        traces.append(QJsonObject{{QStringLiteral("id"), q(item.id)}, {QStringLiteral("sourceId"), q(item.sourceId)},
                                  {QStringLiteral("netId"), optionalString(item.netId)},
                                  {QStringLiteral("layerId"), q(item.layerId)}, {QStringLiteral("widthM"), item.widthM},
                                  {QStringLiteral("routeM"), route}});
    }
    QJsonArray vias;
    for (const PcbVia& item : pcb.vias) {
        vias.append(QJsonObject{{QStringLiteral("id"), q(item.id)}, {QStringLiteral("sourceId"), q(item.sourceId)},
                                {QStringLiteral("netId"), optionalString(item.netId)},
                                {QStringLiteral("centerM"), vector2(item.centerM)},
                                {QStringLiteral("outerDiameterM"), item.outerDiameterM},
                                {QStringLiteral("holeDiameterM"), item.holeDiameterM},
                                {QStringLiteral("layerIds"), strings(item.layerIds)}});
    }
    QJsonArray pads;
    for (const PcbPad& item : pcb.pads) {
        pads.append(QJsonObject{{QStringLiteral("id"), q(item.id)}, {QStringLiteral("sourceId"), q(item.sourceId)},
                                {QStringLiteral("componentId"), q(item.componentId)},
                                {QStringLiteral("pinId"), optionalString(item.pinId)},
                                {QStringLiteral("netId"), optionalString(item.netId)},
                                {QStringLiteral("shape"), item.shape == PcbPadShape::Circle ? QStringLiteral("circle") : QStringLiteral("rectangle")},
                                {QStringLiteral("centerM"), vector2(item.centerM)},
                                {QStringLiteral("widthM"), item.widthM}, {QStringLiteral("heightM"), item.heightM},
                                {QStringLiteral("rotationRad"), item.rotationRad},
                                {QStringLiteral("layerIds"), strings(item.layerIds)}});
    }
    QJsonArray regions;
    for (const ElectronicMaterialRegion& item : pcb.materialRegions) {
        QJsonArray outline;
        for (const Point2M& point : item.outlineM) outline.append(vector2(point));
        regions.append(QJsonObject{{QStringLiteral("id"), q(item.id)}, {QStringLiteral("name"), q(item.name)},
                                   {QStringLiteral("materialDefinitionId"), q(item.materialDefinitionId)},
                                   {QStringLiteral("layerId"), optionalString(item.layerId)},
                                   {QStringLiteral("outlineM"), outline}});
    }
    return {{QStringLiteral("id"), q(pcb.id)}, {QStringLiteral("name"), q(pcb.name)},
            {QStringLiteral("circuitDocumentId"), optionalString(pcb.circuitDocumentId)},
            {QStringLiteral("boardMaterialDefinitionId"), optionalString(pcb.boardMaterialDefinitionId)},
            {QStringLiteral("widthM"), pcb.widthM}, {QStringLiteral("heightM"), pcb.heightM},
            {QStringLiteral("thicknessM"), pcb.thicknessM},
            {QStringLiteral("centerM"), vector2(pcb.centerM)},
            {QStringLiteral("stackup"), QJsonObject{{QStringLiteral("id"), q(pcb.stackup.id)}, {QStringLiteral("layers"), layers}}},
            {QStringLiteral("components"), components}, {QStringLiteral("traces"), traces},
            {QStringLiteral("vias"), vias}, {QStringLiteral("pads"), pads}, {QStringLiteral("materialRegions"), regions}};
}

PcbDocument readPcb(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("PCB document entries must be objects."));
    const QJsonObject object = value.toObject();
    PcbDocument result;
    result.id = s(object.value(QStringLiteral("id")), QStringLiteral("pcb.id"));
    result.name = s(object.value(QStringLiteral("name")), QStringLiteral("pcb.name"));
    result.circuitDocumentId = readOptionalString(object.value(QStringLiteral("circuitDocumentId")), QStringLiteral("pcb.circuitDocumentId"));
    result.boardMaterialDefinitionId = readOptionalString(object.value(QStringLiteral("boardMaterialDefinitionId")), QStringLiteral("pcb.boardMaterialDefinitionId"));
    result.widthM = number(object.value(QStringLiteral("widthM")), QStringLiteral("pcb.widthM"));
    result.heightM = number(object.value(QStringLiteral("heightM")), QStringLiteral("pcb.heightM"));
    result.thicknessM = number(object.value(QStringLiteral("thicknessM")), QStringLiteral("pcb.thicknessM"));
    result.centerM = readVector2(object.value(QStringLiteral("centerM")), QStringLiteral("pcb.centerM"));
    const QJsonValue stackupValue = object.value(QStringLiteral("stackup"));
    if (!stackupValue.isObject()) fail(QStringLiteral("PCB stackup must be an object."));
    const QJsonObject stackup = stackupValue.toObject();
    result.stackup.id = s(stackup.value(QStringLiteral("id")), QStringLiteral("stackup.id"));
    const QJsonValue layers = stackup.value(QStringLiteral("layers"));
    if (!layers.isArray()) fail(QStringLiteral("PCB stackup layers must be an array."));
    for (const QJsonValue& entry : layers.toArray()) result.stackup.layers.push_back(readLayer(entry));

    const QJsonValue components = object.value(QStringLiteral("components"));
    if (!components.isArray()) fail(QStringLiteral("PCB components must be an array."));
    for (const QJsonValue& entry : components.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("PCB component entries must be objects."));
        const QJsonObject item = entry.toObject();
        PcbComponent component;
        component.id = s(item.value(QStringLiteral("id")), QStringLiteral("pcbComponent.id"));
        component.sourceId = s(item.value(QStringLiteral("sourceId")), QStringLiteral("pcbComponent.sourceId"));
        component.reference = s(item.value(QStringLiteral("reference")), QStringLiteral("pcbComponent.reference"));
        component.footprint = s(item.value(QStringLiteral("footprint")), QStringLiteral("pcbComponent.footprint"));
        component.circuitComponentId = readOptionalString(item.value(QStringLiteral("circuitComponentId")), QStringLiteral("pcbComponent.circuitComponentId"));
        component.centerM = readVector2(item.value(QStringLiteral("centerM")), QStringLiteral("pcbComponent.centerM"));
        component.rotationRad = number(item.value(QStringLiteral("rotationRad")), QStringLiteral("pcbComponent.rotationRad"));
        component.widthM = number(item.value(QStringLiteral("widthM")), QStringLiteral("pcbComponent.widthM"));
        component.heightM = number(item.value(QStringLiteral("heightM")), QStringLiteral("pcbComponent.heightM"));
        component.packageHeightM = readOptionalNumber(item.value(QStringLiteral("packageHeightM")), QStringLiteral("pcbComponent.packageHeightM"));
        component.layerName = s(item.value(QStringLiteral("layerName")), QStringLiteral("pcbComponent.layerName"));
        result.components.push_back(std::move(component));
    }
    const QJsonValue traces = object.value(QStringLiteral("traces"));
    if (!traces.isArray()) fail(QStringLiteral("PCB traces must be an array."));
    for (const QJsonValue& entry : traces.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("PCB trace entries must be objects."));
        const QJsonObject item = entry.toObject();
        PcbTrace trace;
        trace.id = s(item.value(QStringLiteral("id")), QStringLiteral("pcbTrace.id"));
        trace.sourceId = s(item.value(QStringLiteral("sourceId")), QStringLiteral("pcbTrace.sourceId"));
        trace.netId = readOptionalString(item.value(QStringLiteral("netId")), QStringLiteral("pcbTrace.netId"));
        trace.layerId = s(item.value(QStringLiteral("layerId")), QStringLiteral("pcbTrace.layerId"));
        trace.widthM = number(item.value(QStringLiteral("widthM")), QStringLiteral("pcbTrace.widthM"));
        const QJsonValue route = item.value(QStringLiteral("routeM"));
        if (!route.isArray()) fail(QStringLiteral("PCB trace routeM must be an array."));
        for (const QJsonValue& point : route.toArray()) trace.routeM.push_back(readVector2(point, QStringLiteral("pcbTrace.routeM")));
        result.traces.push_back(std::move(trace));
    }
    const QJsonValue vias = object.value(QStringLiteral("vias"));
    if (!vias.isArray()) fail(QStringLiteral("PCB vias must be an array."));
    for (const QJsonValue& entry : vias.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("PCB via entries must be objects."));
        const QJsonObject item = entry.toObject();
        result.vias.push_back({s(item.value(QStringLiteral("id")), QStringLiteral("pcbVia.id")),
                               s(item.value(QStringLiteral("sourceId")), QStringLiteral("pcbVia.sourceId")),
                               readOptionalString(item.value(QStringLiteral("netId")), QStringLiteral("pcbVia.netId")),
                               readVector2(item.value(QStringLiteral("centerM")), QStringLiteral("pcbVia.centerM")),
                               number(item.value(QStringLiteral("outerDiameterM")), QStringLiteral("pcbVia.outerDiameterM")),
                               number(item.value(QStringLiteral("holeDiameterM")), QStringLiteral("pcbVia.holeDiameterM")),
                               readStrings(item.value(QStringLiteral("layerIds")), QStringLiteral("pcbVia.layerIds"))});
    }
    const QJsonValue pads = object.value(QStringLiteral("pads"));
    if (!pads.isArray()) fail(QStringLiteral("PCB pads must be an array."));
    for (const QJsonValue& entry : pads.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("PCB pad entries must be objects."));
        const QJsonObject item = entry.toObject();
        const std::string shape = s(item.value(QStringLiteral("shape")), QStringLiteral("pcbPad.shape"));
        if (shape != "circle" && shape != "rectangle") fail(QStringLiteral("Unsupported PCB pad shape."));
        PcbPad pad;
        pad.id = s(item.value(QStringLiteral("id")), QStringLiteral("pcbPad.id"));
        pad.sourceId = s(item.value(QStringLiteral("sourceId")), QStringLiteral("pcbPad.sourceId"));
        pad.componentId = s(item.value(QStringLiteral("componentId")), QStringLiteral("pcbPad.componentId"));
        pad.pinId = readOptionalString(item.value(QStringLiteral("pinId")), QStringLiteral("pcbPad.pinId"));
        pad.netId = readOptionalString(item.value(QStringLiteral("netId")), QStringLiteral("pcbPad.netId"));
        pad.shape = shape == "circle" ? PcbPadShape::Circle : PcbPadShape::Rectangle;
        pad.centerM = readVector2(item.value(QStringLiteral("centerM")), QStringLiteral("pcbPad.centerM"));
        pad.widthM = number(item.value(QStringLiteral("widthM")), QStringLiteral("pcbPad.widthM"));
        pad.heightM = number(item.value(QStringLiteral("heightM")), QStringLiteral("pcbPad.heightM"));
        pad.rotationRad = number(item.value(QStringLiteral("rotationRad")), QStringLiteral("pcbPad.rotationRad"));
        pad.layerIds = readStrings(item.value(QStringLiteral("layerIds")), QStringLiteral("pcbPad.layerIds"));
        result.pads.push_back(std::move(pad));
    }
    const QJsonValue regions = object.value(QStringLiteral("materialRegions"));
    if (!regions.isArray()) fail(QStringLiteral("PCB materialRegions must be an array."));
    for (const QJsonValue& entry : regions.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("PCB material region entries must be objects."));
        const QJsonObject item = entry.toObject();
        ElectronicMaterialRegion region;
        region.id = s(item.value(QStringLiteral("id")), QStringLiteral("materialRegion.id"));
        region.name = s(item.value(QStringLiteral("name")), QStringLiteral("materialRegion.name"));
        region.materialDefinitionId = s(item.value(QStringLiteral("materialDefinitionId")), QStringLiteral("materialRegion.materialDefinitionId"));
        region.layerId = readOptionalString(item.value(QStringLiteral("layerId")), QStringLiteral("materialRegion.layerId"));
        const QJsonValue outline = item.value(QStringLiteral("outlineM"));
        if (!outline.isArray()) fail(QStringLiteral("PCB material region outlineM must be an array."));
        for (const QJsonValue& point : outline.toArray()) region.outlineM.push_back(readVector2(point, QStringLiteral("materialRegion.outlineM")));
        result.materialRegions.push_back(std::move(region));
    }
    return result;
}

}  // namespace

QJsonObject encodeElectronicsModel(const DatumElectronicsModel& model)
{
    model.validate();
    QJsonArray materials;
    for (const MaterialDefinition& item : model.materials) materials.append(encodeMaterial(item));
    QJsonArray states;
    for (const MaterialState& item : model.materialStates) {
        states.append(QJsonObject{{QStringLiteral("id"), q(item.id)},
                                  {QStringLiteral("materialDefinitionId"), q(item.materialDefinitionId)},
                                  {QStringLiteral("temperatureK"), optionalNumber(item.temperatureK)},
                                  {QStringLiteral("pressurePa"), optionalNumber(item.pressurePa)},
                                  {QStringLiteral("phaseFractions"), phaseFractions(item.phaseFractions)},
                                  {QStringLiteral("microstructure"), item.microstructure.has_value()
                                       ? QJsonValue(encodeMicrostructure(*item.microstructure)) : QJsonValue(QJsonValue::Null)},
                                  {QStringLiteral("fieldDatasetReferences"), strings(item.fieldDatasetReferences)}});
    }
    QJsonArray circuits;
    for (const CircuitDocument& item : model.circuits) circuits.append(encodeCircuit(item));
    QJsonArray pcbs;
    for (const PcbDocument& item : model.pcbs) pcbs.append(encodePcb(item));
    return {{QStringLiteral("materials"), materials}, {QStringLiteral("materialStates"), states},
            {QStringLiteral("circuits"), circuits}, {QStringLiteral("pcbs"), pcbs}};
}

DatumElectronicsModel decodeElectronicsModel(const QJsonObject& object)
{
    DatumElectronicsModel model;
    const QJsonValue materials = object.value(QStringLiteral("materials"));
    const QJsonValue states = object.value(QStringLiteral("materialStates"));
    const QJsonValue circuits = object.value(QStringLiteral("circuits"));
    const QJsonValue pcbs = object.value(QStringLiteral("pcbs"));
    if (!materials.isArray() || !states.isArray() || !circuits.isArray() || !pcbs.isArray()) {
        fail(QStringLiteral("Electronics model materials, materialStates, circuits, and pcbs must be arrays."));
    }
    for (const QJsonValue& entry : materials.toArray()) model.materials.push_back(readMaterial(entry));
    for (const QJsonValue& entry : states.toArray()) {
        if (!entry.isObject()) fail(QStringLiteral("Material state entries must be objects."));
        const QJsonObject item = entry.toObject();
        MaterialState state;
        state.id = s(item.value(QStringLiteral("id")), QStringLiteral("materialState.id"));
        state.materialDefinitionId = s(item.value(QStringLiteral("materialDefinitionId")), QStringLiteral("materialState.materialDefinitionId"));
        state.temperatureK = readOptionalNumber(item.value(QStringLiteral("temperatureK")), QStringLiteral("materialState.temperatureK"));
        state.pressurePa = readOptionalNumber(item.value(QStringLiteral("pressurePa")), QStringLiteral("materialState.pressurePa"));
        state.phaseFractions = readPhaseFractions(item.value(QStringLiteral("phaseFractions")));
        const QJsonValue microstructure = item.value(QStringLiteral("microstructure"));
        if (microstructure.isObject()) state.microstructure = readMicrostructure(microstructure, QStringLiteral("materialState.microstructure"));
        else if (!microstructure.isNull()) fail(QStringLiteral("Material state microstructure must be an object or null."));
        state.fieldDatasetReferences = readStrings(item.value(QStringLiteral("fieldDatasetReferences")), QStringLiteral("materialState.fieldDatasetReferences"));
        model.materialStates.push_back(std::move(state));
    }
    for (const QJsonValue& entry : circuits.toArray()) model.circuits.push_back(readCircuit(entry));
    for (const QJsonValue& entry : pcbs.toArray()) model.pcbs.push_back(readPcb(entry));
    try {
        model.validate();
    } catch (const std::invalid_argument& error) {
        fail(QStringLiteral("Electronics model is invalid: %1").arg(QString::fromUtf8(error.what())));
    }
    return model;
}

}  // namespace datumloop
