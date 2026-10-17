#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/io/ElectronicsProjectCodec.hpp"
#include "datumloop/io/PartRequirementsCodec.hpp"
#include "datumloop/topology/DatumTopologyResolver.hpp"

#include "datumloop/geometry/OcctKernel.hpp"

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Standard_Failure.hxx>
#include <TopTools_FormatVersion.hxx>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace datumloop {
namespace {

constexpr auto FormatName = "datumloop-project";
constexpr auto GeometryFormat = "occt-brep-ascii-v1";

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().toStdString());
}

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::string toUtf8(const QString& value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString requiredString(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        fail(QStringLiteral("Project field '%1' must be a non-empty string.").arg(key));
    }
    return value.toString();
}

double requiredFiniteNumber(const QJsonValue& value, const QString& description)
{
    if (!value.isDouble()) {
        fail(QStringLiteral("Project field '%1' must be numeric.").arg(description));
    }
    const double number = value.toDouble();
    if (!std::isfinite(number)) {
        fail(QStringLiteral("Project field '%1' must be finite.").arg(description));
    }
    return number;
}

QJsonObject encodeProperty(const DatumPropertyValue& value)
{
    QJsonObject result;
    if (const auto* boolean = std::get_if<bool>(&value)) {
        result.insert(QStringLiteral("type"), QStringLiteral("bool"));
        result.insert(QStringLiteral("value"), *boolean);
    } else if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        result.insert(QStringLiteral("type"), QStringLiteral("int64"));
        result.insert(QStringLiteral("value"), QString::number(*integer));
    } else if (const auto* number = std::get_if<double>(&value)) {
        result.insert(QStringLiteral("type"), QStringLiteral("double"));
        result.insert(QStringLiteral("value"), *number);
    } else {
        result.insert(QStringLiteral("type"), QStringLiteral("string"));
        result.insert(QStringLiteral("value"), fromUtf8(std::get<std::string>(value)));
    }
    return result;
}

QJsonObject encodeFeature(const DatumFeature& feature)
{
    QJsonObject definitions;
    for (const auto& [key, definition] : feature.parameterDefinitions) {
        QJsonArray variables;
        for (const auto& id : definition.designVariableIds) variables.append(fromUtf8(id));
        QJsonObject sourceValues;
        for (const auto& [id, value] : definition.sourceValues) sourceValues.insert(fromUtf8(id), value);
        definitions.insert(fromUtf8(key), QJsonObject{
            {QStringLiteral("unit"), fromUtf8(definition.unit)},
            {QStringLiteral("partRequirementsId"), fromUtf8(definition.partRequirementsId)},
            {QStringLiteral("designVariableIds"), variables},
            {QStringLiteral("sourceValues"), sourceValues},
            {QStringLiteral("relationship"), fromUtf8(definition.relationship)}});
    }
    QJsonArray inputs;
    for (const std::string& inputId : feature.inputFeatureIds) {
        inputs.append(fromUtf8(inputId));
    }
    QJsonObject parameters;
    for (const auto& [name, value] : feature.parameters) {
        parameters.insert(fromUtf8(name), value);
    }
    return {
        {QStringLiteral("id"), fromUtf8(feature.id)},
        {QStringLiteral("ownerObjectId"), fromUtf8(feature.ownerObjectId)},
        {QStringLiteral("type"), fromUtf8(datumFeatureTypeName(feature.type))},
        {QStringLiteral("inputFeatureIds"), inputs},
        {QStringLiteral("parameters"), parameters},
        {QStringLiteral("kernelBackend"), fromUtf8(feature.kernelBackend)},
        {QStringLiteral("name"), fromUtf8(feature.name)},
        {QStringLiteral("semanticRole"), fromUtf8(feature.semanticRole)},
        {QStringLiteral("parameterDefinitions"), definitions},
    };
}

DatumFeature decodeFeature(const QJsonObject& object, const std::string& expectedOwnerId, int schemaVersion)
{
    DatumFeature feature;
    feature.id = toUtf8(requiredString(object, QStringLiteral("id")));
    feature.ownerObjectId = toUtf8(requiredString(object, QStringLiteral("ownerObjectId")));
    if (feature.ownerObjectId != expectedOwnerId) {
        fail(QStringLiteral("Feature '%1' belongs to a different object.").arg(fromUtf8(feature.id)));
    }
    const auto type = datumFeatureTypeFromName(toUtf8(requiredString(object, QStringLiteral("type"))));
    if (!type.has_value()) {
        fail(QStringLiteral("Feature '%1' uses an unsupported type.").arg(fromUtf8(feature.id)));
    }
    feature.type = *type;
    const QJsonValue inputsValue = object.value(QStringLiteral("inputFeatureIds"));
    if (!inputsValue.isArray()) {
        fail(QStringLiteral("Feature '%1' inputFeatureIds must be an array.").arg(fromUtf8(feature.id)));
    }
    for (const QJsonValue& input : inputsValue.toArray()) {
        if (!input.isString()) {
            fail(QStringLiteral("Feature '%1' contains a non-string input id.").arg(fromUtf8(feature.id)));
        }
        feature.inputFeatureIds.push_back(toUtf8(input.toString()));
    }
    const QJsonValue parametersValue = object.value(QStringLiteral("parameters"));
    if (!parametersValue.isObject()) {
        fail(QStringLiteral("Feature '%1' parameters must be an object.").arg(fromUtf8(feature.id)));
    }
    const QJsonObject parameters = parametersValue.toObject();
    for (auto iterator = parameters.begin(); iterator != parameters.end(); ++iterator) {
        feature.parameters.emplace(toUtf8(iterator.key()),
                                   requiredFiniteNumber(iterator.value(),
                                                        QStringLiteral("feature.parameters.%1").arg(iterator.key())));
    }
    feature.kernelBackend = toUtf8(requiredString(object, QStringLiteral("kernelBackend")));
    const auto definitions = object.value(QStringLiteral("parameterDefinitions"));
    if (!definitions.isUndefined() && !definitions.isObject()) fail(QStringLiteral("Feature parameter definitions must be an object."));
    const auto name = object.value(QStringLiteral("name"));
    const auto role = object.value(QStringLiteral("semanticRole"));
    if ((!name.isUndefined() && !name.isString()) || (!role.isUndefined() && !role.isString())) {
        fail(QStringLiteral("Feature name and role must be strings."));
    }
    feature.name = toUtf8(name.toString());
    feature.semanticRole = toUtf8(role.toString());
    const auto definitionObject = definitions.toObject();
    if (schemaVersion < 9 && (!definitionObject.isEmpty() || !feature.name.empty() || !feature.semanticRole.empty() ||
        feature.type == DatumFeatureType::ExtrudeRectangle || feature.type == DatumFeatureType::ThroughHole)) {
        fail(QStringLiteral("Engineering feature semantics require schema nine."));
    }
    for (auto it = definitionObject.begin(); it != definitionObject.end(); ++it) {
        if (!it.value().isObject()) fail(QStringLiteral("Invalid feature parameter definition."));
        const auto item = it.value().toObject();
        FeatureParameter definition;
        definition.unit = toUtf8(requiredString(item, QStringLiteral("unit")));
        definition.partRequirementsId = toUtf8(requiredString(item, QStringLiteral("partRequirementsId")));
        definition.relationship = toUtf8(requiredString(item, QStringLiteral("relationship")));
        const auto sourceValues = item.value(QStringLiteral("sourceValues"));
        if (!sourceValues.isObject()) fail(QStringLiteral("Feature sourceValues must be an object."));
        const auto snapshots = sourceValues.toObject();
        for (auto snapshot = snapshots.begin(); snapshot != snapshots.end(); ++snapshot) {
            definition.sourceValues.emplace(toUtf8(snapshot.key()), requiredFiniteNumber(snapshot.value(), snapshot.key()));
        }
        const auto variables = item.value(QStringLiteral("designVariableIds"));
        if (!variables.isArray()) fail(QStringLiteral("Feature source parameters must be an array."));
        for (const auto& id : variables.toArray()) {
            if (!id.isString()) fail(QStringLiteral("Feature source parameter key must be a string."));
            definition.designVariableIds.push_back(toUtf8(id.toString()));
        }
        feature.parameterDefinitions.emplace(toUtf8(it.key()), std::move(definition));
    }
    return feature;
}

bool closeMetric(double left, double right)
{
    return std::abs(left - right) <= 1e-9 * std::max({1.0, std::abs(left), std::abs(right)});
}

bool sameShapeMetrics(const ShapeMetrics& left, const ShapeMetrics& right)
{
    if (left.valid != right.valid || !closeMetric(left.volume, right.volume) ||
        !closeMetric(left.surfaceArea, right.surfaceArea)) {
        return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!closeMetric(left.centerOfMass[axis], right.centerOfMass[axis]) ||
            !closeMetric(left.minimum[axis], right.minimum[axis]) ||
            !closeMetric(left.maximum[axis], right.maximum[axis])) {
            return false;
        }
    }
    return true;
}

DatumPropertyValue decodeProperty(const QJsonObject& property, const QString& key)
{
    const QString type = requiredString(property, QStringLiteral("type"));
    const QJsonValue value = property.value(QStringLiteral("value"));
    if (type == QStringLiteral("bool")) {
        if (!value.isBool()) {
            fail(QStringLiteral("Boolean property '%1' has an invalid value.").arg(key));
        }
        return value.toBool();
    }
    if (type == QStringLiteral("int64")) {
        if (!value.isString()) {
            fail(QStringLiteral("Integer property '%1' must be encoded as a decimal string.").arg(key));
        }
        const QByteArray text = value.toString().toLatin1();
        std::int64_t integer = 0;
        const auto parsed = std::from_chars(text.constData(), text.constData() + text.size(), integer);
        if (parsed.ec != std::errc{} || parsed.ptr != text.constData() + text.size()) {
            fail(QStringLiteral("Integer property '%1' is outside the signed 64-bit range or malformed.").arg(key));
        }
        return integer;
    }
    if (type == QStringLiteral("double")) {
        return requiredFiniteNumber(value, QStringLiteral("properties.%1.value").arg(key));
    }
    if (type == QStringLiteral("string")) {
        if (!value.isString()) {
            fail(QStringLiteral("String property '%1' has an invalid value.").arg(key));
        }
        return toUtf8(value.toString());
    }
    fail(QStringLiteral("Property '%1' uses unsupported type '%2'.").arg(key, type));
}

QJsonObject encodeObject(const DatumObject& object)
{
    QJsonObject properties;
    for (const auto& [key, value] : object.properties) {
        properties.insert(fromUtf8(key), encodeProperty(value));
    }

    QJsonArray translation;
    for (const double component : object.position) {
        translation.append(component);
    }
    QJsonArray featureDefinitions;
    for (const DatumFeature& feature : object.featureGraph.features()) {
        featureDefinitions.append(encodeFeature(feature));
    }

    std::ostringstream brepStream(std::ios::out | std::ios::binary);
    BRepTools::Write(object.shape,
                     brepStream,
                     Standard_False,
                     Standard_False,
                     TopTools_FormatVersion_CURRENT);
    const std::string brep = brepStream.str();
    if (brep.empty()) {
        fail(QStringLiteral("OCCT did not serialize object '%1'.").arg(fromUtf8(object.name)));
    }

    QJsonObject geometry{
        {QStringLiteral("format"), QString::fromLatin1(GeometryFormat)},
        {QStringLiteral("occtFormatVersion"), static_cast<int>(TopTools_FormatVersion_CURRENT)},
        {QStringLiteral("encoding"), QStringLiteral("base64")},
        {QStringLiteral("data"), QString::fromLatin1(QByteArray(brep.data(), static_cast<qsizetype>(brep.size())).toBase64())},
    };
    QJsonObject transform{
        {QStringLiteral("translationMm"), translation},
    };
    QJsonObject result{
        {QStringLiteral("id"), fromUtf8(object.id)},
        {QStringLiteral("name"), fromUtf8(object.name)},
        {QStringLiteral("parentId"), object.parentId.has_value()
             ? QJsonValue(fromUtf8(*object.parentId))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("properties"), properties},
        {QStringLiteral("featureDefinitions"), featureDefinitions},
        {QStringLiteral("outputFeatureId"), object.featureGraph.outputFeatureId().has_value()
             ? QJsonValue(fromUtf8(*object.featureGraph.outputFeatureId()))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("references"), QJsonArray{}},
        {QStringLiteral("transform"), transform},
        {QStringLiteral("geometry"), geometry},
    };
    return result;
}

QJsonArray encodeVector(const Vector3& vector)
{
    QJsonArray result;
    for (const double component : vector) {
        result.append(component);
    }
    return result;
}

Vector3 decodeVector(const QJsonValue& value, const QString& description)
{
    if (!value.isArray() || value.toArray().size() != 3) {
        fail(QStringLiteral("Project field '%1' must contain three coordinates.").arg(description));
    }
    const QJsonArray array = value.toArray();
    return {
        requiredFiniteNumber(array.at(0), description + QStringLiteral("[0]")),
        requiredFiniteNumber(array.at(1), description + QStringLiteral("[1]")),
        requiredFiniteNumber(array.at(2), description + QStringLiteral("[2]")),
    };
}

QJsonValue encodeOptionalNumber(const std::optional<double>& value)
{
    return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

std::optional<double> decodeOptionalNumber(const QJsonObject& object, const QString& key)
{
    if (!object.contains(key)) {
        fail(QStringLiteral("Engineering field '%1' is missing.").arg(key));
    }
    const QJsonValue value = object.value(key);
    if (value.isNull()) {
        return std::nullopt;
    }
    return requiredFiniteNumber(value, QStringLiteral("engineering.%1").arg(key));
}

QJsonValue encodeOptionalString(const std::optional<std::string>& value)
{
    return value.has_value() ? QJsonValue(fromUtf8(*value)) : QJsonValue(QJsonValue::Null);
}

std::optional<std::string> decodeOptionalString(const QJsonObject& object, const QString& key)
{
    if (!object.contains(key)) {
        fail(QStringLiteral("Engineering field '%1' is missing.").arg(key));
    }
    const QJsonValue value = object.value(key);
    if (value.isNull()) {
        return std::nullopt;
    }
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        fail(QStringLiteral("Engineering field '%1' must be a non-empty string or null.").arg(key));
    }
    return toUtf8(value.toString());
}

QJsonObject encodeCoordinateFrame(const DatumCoordinateFrame& frame)
{
    QJsonArray orientation;
    for (double component : frame.orientationWxyz) orientation.append(component);
    return {
        {QStringLiteral("originM"), encodeVector(frame.originM)},
        {QStringLiteral("orientationWxyz"), orientation},
    };
}

DatumCoordinateFrame decodeCoordinateFrame(const QJsonValue& value, const QString& field)
{
    if (!value.isObject()) fail(QStringLiteral("%1 must be an object.").arg(field));
    const QJsonObject object = value.toObject();
    DatumCoordinateFrame frame;
    frame.originM = decodeVector(object.value(QStringLiteral("originM")), field + QStringLiteral(".originM"));
    const QJsonValue orientationValue = object.value(QStringLiteral("orientationWxyz"));
    if (!orientationValue.isArray() || orientationValue.toArray().size() != 4) {
        fail(QStringLiteral("%1.orientationWxyz must contain four quaternion components.").arg(field));
    }
    const QJsonArray orientation = orientationValue.toArray();
    for (qsizetype index = 0; index < 4; ++index) {
        frame.orientationWxyz[static_cast<std::size_t>(index)] = requiredFiniteNumber(
            orientation.at(index), QStringLiteral("%1.orientationWxyz[%2]").arg(field).arg(index));
    }
    return frame;
}

QJsonObject encodeMechanismBody(const DatumMechanismBody& body)
{
    QJsonValue inertiaValue(QJsonValue::Null);
    if (body.inertiaTensorKgM2.has_value()) {
        QJsonArray inertia;
        for (double component : *body.inertiaTensorKgM2) inertia.append(component);
        inertiaValue = inertia;
    }
    QJsonArray evidence;
    for (const DatumPropertyEvidence& item : body.propertyEvidence) {
        evidence.append(QJsonObject{
            {QStringLiteral("propertyId"), fromUtf8(item.propertyId)},
            {QStringLiteral("kind"), fromUtf8(datumPropertyEvidenceKindName(item.kind))},
            {QStringLiteral("reference"), fromUtf8(item.reference)},
            {QStringLiteral("revision"), fromUtf8(item.revision)},
            {QStringLiteral("confidence"), encodeOptionalNumber(item.confidence)},
        });
    }
    return {
        {QStringLiteral("id"), fromUtf8(body.id)},
        {QStringLiteral("name"), fromUtf8(body.name)},
        {QStringLiteral("cadObjectId"), fromUtf8(body.cadObjectId)},
        {QStringLiteral("geometrySource"), QJsonObject{
            {QStringLiteral("kind"), fromUtf8(datumGeometrySourceKindName(body.geometrySource.kind))},
            {QStringLiteral("locator"), fromUtf8(body.geometrySource.locator)},
            {QStringLiteral("revision"), fromUtf8(body.geometrySource.revision)},
        }},
        {QStringLiteral("massKg"), encodeOptionalNumber(body.massKg)},
        {QStringLiteral("centerOfMassLocalM"), body.centerOfMassLocalM.has_value()
             ? QJsonValue(encodeVector(*body.centerOfMassLocalM))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("inertiaTensorKgM2"), inertiaValue},
        {QStringLiteral("materialDefinitionId"), encodeOptionalString(body.materialDefinitionId)},
        {QStringLiteral("propertyEvidence"), evidence},
    };
}

DatumMechanismBody decodeMechanismBody(const QJsonObject& object)
{
    DatumMechanismBody body;
    body.id = toUtf8(requiredString(object, QStringLiteral("id")));
    body.name = toUtf8(requiredString(object, QStringLiteral("name")));
    body.cadObjectId = toUtf8(requiredString(object, QStringLiteral("cadObjectId")));
    const QJsonValue sourceValue = object.value(QStringLiteral("geometrySource"));
    if (!sourceValue.isObject()) fail(QStringLiteral("Mechanism body geometrySource must be an object."));
    const QJsonObject source = sourceValue.toObject();
    const auto sourceKind = datumGeometrySourceKindFromName(
        toUtf8(requiredString(source, QStringLiteral("kind"))));
    if (!sourceKind.has_value()) fail(QStringLiteral("Mechanism body geometry-source kind is unsupported."));
    body.geometrySource.kind = *sourceKind;
    body.geometrySource.locator = toUtf8(requiredString(source, QStringLiteral("locator")));
    const QJsonValue revisionValue = source.value(QStringLiteral("revision"));
    if (!revisionValue.isString()) fail(QStringLiteral("Mechanism body source revision must be a string."));
    body.geometrySource.revision = toUtf8(revisionValue.toString());
    body.massKg = decodeOptionalNumber(object, QStringLiteral("massKg"));
    const QJsonValue centerValue = object.value(QStringLiteral("centerOfMassLocalM"));
    if (centerValue.isNull()) body.centerOfMassLocalM = std::nullopt;
    else body.centerOfMassLocalM = decodeVector(centerValue, QStringLiteral("body.centerOfMassLocalM"));
    const QJsonValue inertiaValue = object.value(QStringLiteral("inertiaTensorKgM2"));
    if (inertiaValue.isNull()) {
        body.inertiaTensorKgM2 = std::nullopt;
    } else if (inertiaValue.isArray() && inertiaValue.toArray().size() == 6) {
        DatumInertiaTensorKgM2 inertia{};
        const QJsonArray values = inertiaValue.toArray();
        for (qsizetype index = 0; index < 6; ++index) {
            inertia[static_cast<std::size_t>(index)] = requiredFiniteNumber(
                values.at(index), QStringLiteral("body.inertiaTensorKgM2[%1]").arg(index));
        }
        body.inertiaTensorKgM2 = inertia;
    } else {
        fail(QStringLiteral("Mechanism body inertiaTensorKgM2 must be null or contain six values."));
    }
    body.materialDefinitionId = decodeOptionalString(object, QStringLiteral("materialDefinitionId"));
    const QJsonValue evidenceValue = object.value(QStringLiteral("propertyEvidence"));
    if (!evidenceValue.isArray()) fail(QStringLiteral("Mechanism body propertyEvidence must be an array."));
    for (const QJsonValue& evidenceEntry : evidenceValue.toArray()) {
        if (!evidenceEntry.isObject()) fail(QStringLiteral("Mechanism body property evidence entry must be an object."));
        const QJsonObject evidenceObject = evidenceEntry.toObject();
        const auto kind = datumPropertyEvidenceKindFromName(
            toUtf8(requiredString(evidenceObject, QStringLiteral("kind"))));
        if (!kind.has_value()) fail(QStringLiteral("Mechanism body property evidence kind is unsupported."));
        body.propertyEvidence.push_back({
            toUtf8(requiredString(evidenceObject, QStringLiteral("propertyId"))),
            *kind,
            toUtf8(requiredString(evidenceObject, QStringLiteral("reference"))),
            toUtf8(requiredString(evidenceObject, QStringLiteral("revision"))),
            decodeOptionalNumber(evidenceObject, QStringLiteral("confidence")),
        });
    }
    return body;
}

QJsonObject encodeMechanism(const MechanismSpec& mechanism)
{
    QJsonArray bodyObjects;
    for (const std::string& objectId : mechanism.bodyObjectIds) bodyObjects.append(fromUtf8(objectId));
    QJsonArray bodies;
    for (const DatumMechanismBody& body : mechanism.bodies) bodies.append(encodeMechanismBody(body));
    QJsonArray axes;
    for (const DatumAxis& axis : mechanism.axes) {
        axes.append(QJsonObject{
            {QStringLiteral("id"), fromUtf8(axis.id)},
            {QStringLiteral("name"), fromUtf8(axis.name)},
            {QStringLiteral("type"), fromUtf8(datumAxisTypeName(axis.type))},
            {QStringLiteral("baseObjectId"), fromUtf8(axis.baseObjectId)},
            {QStringLiteral("movingObjectId"), fromUtf8(axis.movingObjectId)},
            {QStringLiteral("originInBaseFrameM"), encodeVector(axis.originInBaseFrameM)},
            {QStringLiteral("directionInBaseFrame"), encodeVector(axis.directionInBaseFrame)},
            {QStringLiteral("actuatorMotorId"), axis.actuatorMotorId.has_value()
                 ? QJsonValue(fromUtf8(*axis.actuatorMotorId))
                 : QJsonValue(QJsonValue::Null)},
        });
    }
    QJsonArray joints;
    for (const DatumJoint& joint : mechanism.joints) {
        joints.append(QJsonObject{
            {QStringLiteral("id"), fromUtf8(joint.id)},
            {QStringLiteral("name"), fromUtf8(joint.name)},
            {QStringLiteral("type"), fromUtf8(datumJointTypeName(joint.type))},
            {QStringLiteral("bodyAId"), fromUtf8(joint.bodyAId)},
            {QStringLiteral("bodyBId"), fromUtf8(joint.bodyBId)},
            {QStringLiteral("frameInBodyA"), encodeCoordinateFrame(joint.frameInBodyA)},
            {QStringLiteral("frameInBodyB"), encodeCoordinateFrame(joint.frameInBodyB)},
            {QStringLiteral("lowerLimitSi"), encodeOptionalNumber(joint.lowerLimitSi)},
            {QStringLiteral("upperLimitSi"), encodeOptionalNumber(joint.upperLimitSi)},
        });
    }
    QJsonArray actuators;
    for (const DatumActuator& actuator : mechanism.actuators) {
        QJsonArray trajectory;
        for (const DatumActuatorPoint& point : actuator.trajectory) {
            trajectory.append(QJsonObject{
                {QStringLiteral("timeS"), point.timeS},
                {QStringLiteral("valueSi"), point.valueSi},
            });
        }
        actuators.append(QJsonObject{
            {QStringLiteral("id"), fromUtf8(actuator.id)},
            {QStringLiteral("name"), fromUtf8(actuator.name)},
            {QStringLiteral("jointId"), fromUtf8(actuator.jointId)},
            {QStringLiteral("mode"), fromUtf8(datumActuatorControlModeName(actuator.mode))},
            {QStringLiteral("motorId"), encodeOptionalString(actuator.motorId)},
            {QStringLiteral("trajectory"), trajectory},
            {QStringLiteral("maximumEffortSi"), encodeOptionalNumber(actuator.maximumEffortSi)},
            {QStringLiteral("maximumRateSi"), encodeOptionalNumber(actuator.maximumRateSi)},
        });
    }
    QJsonArray contacts;
    for (const DatumContactPair& contact : mechanism.contacts) {
        contacts.append(QJsonObject{
            {QStringLiteral("id"), fromUtf8(contact.id)},
            {QStringLiteral("name"), fromUtf8(contact.name)},
            {QStringLiteral("bodyAId"), fromUtf8(contact.bodyAId)},
            {QStringLiteral("bodyBId"), fromUtf8(contact.bodyBId)},
            {QStringLiteral("surfaceAReferenceId"), encodeOptionalString(contact.surfaceAReferenceId)},
            {QStringLiteral("surfaceBReferenceId"), encodeOptionalString(contact.surfaceBReferenceId)},
            {QStringLiteral("law"), fromUtf8(datumContactLawName(contact.law))},
            {QStringLiteral("frictionCoefficient"), encodeOptionalNumber(contact.frictionCoefficient)},
            {QStringLiteral("normalStiffnessNPerM"), encodeOptionalNumber(contact.normalStiffnessNPerM)},
            {QStringLiteral("normalDampingNsPerM"), encodeOptionalNumber(contact.normalDampingNsPerM)},
            {QStringLiteral("restitution"), encodeOptionalNumber(contact.restitution)},
        });
    }
    QJsonArray requirements;
    for (const DatumRequirement& requirement : mechanism.requirements) {
        requirements.append(QJsonObject{
            {QStringLiteral("id"), fromUtf8(requirement.id)},
            {QStringLiteral("name"), fromUtf8(requirement.name)},
            {QStringLiteral("metricId"), fromUtf8(requirement.metricId)},
            {QStringLiteral("unit"), fromUtf8(requirement.unit)},
            {QStringLiteral("relation"), fromUtf8(datumRequirementRelationName(requirement.relation))},
            {QStringLiteral("minimumSi"), encodeOptionalNumber(requirement.minimumSi)},
            {QStringLiteral("maximumSi"), encodeOptionalNumber(requirement.maximumSi)},
            {QStringLiteral("targetSi"), encodeOptionalNumber(requirement.targetSi)},
            {QStringLiteral("toleranceSi"), encodeOptionalNumber(requirement.toleranceSi)},
            {QStringLiteral("hardConstraint"), requirement.hardConstraint},
        });
    }
    return {
        {QStringLiteral("specVersion"), static_cast<int>(mechanism.specVersion)},
        {QStringLiteral("id"), fromUtf8(mechanism.id)},
        {QStringLiteral("name"), fromUtf8(mechanism.name)},
        {QStringLiteral("bodyObjectIds"), bodyObjects},
        {QStringLiteral("bodies"), bodies},
        {QStringLiteral("axes"), axes},
        {QStringLiteral("joints"), joints},
        {QStringLiteral("actuators"), actuators},
        {QStringLiteral("contacts"), contacts},
        {QStringLiteral("requirements"), requirements},
    };
}

MechanismSpec decodeMechanism(const QJsonObject& object, int projectSchemaVersion)
{
    MechanismSpec mechanism;
    mechanism.id = toUtf8(requiredString(object, QStringLiteral("id")));
    mechanism.name = toUtf8(requiredString(object, QStringLiteral("name")));
    const QJsonValue bodyObjectsValue = object.value(QStringLiteral("bodyObjectIds"));
    if (!bodyObjectsValue.isArray()) fail(QStringLiteral("Mechanism bodyObjectIds must be an array."));
    for (const QJsonValue& bodyValue : bodyObjectsValue.toArray()) {
        if (!bodyValue.isString()) fail(QStringLiteral("Mechanism bodyObjectIds entries must be strings."));
        mechanism.bodyObjectIds.push_back(toUtf8(bodyValue.toString()));
    }
    const QJsonValue axesValue = object.value(QStringLiteral("axes"));
    if (!axesValue.isArray()) fail(QStringLiteral("Mechanism axes must be an array."));
    for (const QJsonValue& axisValue : axesValue.toArray()) {
        if (!axisValue.isObject()) fail(QStringLiteral("Mechanism axis entry must be an object."));
        const QJsonObject axisObject = axisValue.toObject();
        DatumAxis axis;
        axis.id = toUtf8(requiredString(axisObject, QStringLiteral("id")));
        axis.name = toUtf8(requiredString(axisObject, QStringLiteral("name")));
        const auto axisType = datumAxisTypeFromName(toUtf8(requiredString(axisObject, QStringLiteral("type"))));
        if (!axisType.has_value()) fail(QStringLiteral("Axis type is unsupported."));
        axis.type = *axisType;
        axis.baseObjectId = toUtf8(requiredString(axisObject, QStringLiteral("baseObjectId")));
        axis.movingObjectId = toUtf8(requiredString(axisObject, QStringLiteral("movingObjectId")));
        axis.originInBaseFrameM = decodeVector(axisObject.value(QStringLiteral("originInBaseFrameM")),
                                               QStringLiteral("axis.originInBaseFrameM"));
        axis.directionInBaseFrame = decodeVector(axisObject.value(QStringLiteral("directionInBaseFrame")),
                                                 QStringLiteral("axis.directionInBaseFrame"));
        if (!axisObject.contains(QStringLiteral("actuatorMotorId"))) {
            fail(QStringLiteral("Axis actuatorMotorId is missing."));
        }
        const QJsonValue motorValue = axisObject.value(QStringLiteral("actuatorMotorId"));
        if (motorValue.isString()) axis.actuatorMotorId = toUtf8(motorValue.toString());
        else if (!motorValue.isNull() && !motorValue.isUndefined()) {
            fail(QStringLiteral("Axis actuatorMotorId must be a motor UUID or null."));
        }
        mechanism.axes.push_back(std::move(axis));
    }

    if (projectSchemaVersion >= 6) {
        const QJsonValue specVersion = object.value(QStringLiteral("specVersion"));
        if (!specVersion.isDouble() || specVersion.toInt(-1) != 1) {
            fail(QStringLiteral("MechanismSpec specVersion must be 1."));
        }
        mechanism.specVersion = 1;
        const QJsonValue bodiesValue = object.value(QStringLiteral("bodies"));
        if (!bodiesValue.isArray()) fail(QStringLiteral("MechanismSpec bodies must be an array."));
        for (const QJsonValue& bodyValue : bodiesValue.toArray()) {
            if (!bodyValue.isObject()) fail(QStringLiteral("MechanismSpec body entries must be objects."));
            mechanism.bodies.push_back(decodeMechanismBody(bodyValue.toObject()));
        }
        const QJsonValue jointsValue = object.value(QStringLiteral("joints"));
        if (!jointsValue.isArray()) fail(QStringLiteral("MechanismSpec joints must be an array."));
        for (const QJsonValue& jointValue : jointsValue.toArray()) {
            if (!jointValue.isObject()) fail(QStringLiteral("MechanismSpec joint entries must be objects."));
            const QJsonObject item = jointValue.toObject();
            DatumJoint joint;
            joint.id = toUtf8(requiredString(item, QStringLiteral("id")));
            joint.name = toUtf8(requiredString(item, QStringLiteral("name")));
            const auto type = datumJointTypeFromName(toUtf8(requiredString(item, QStringLiteral("type"))));
            if (!type.has_value()) fail(QStringLiteral("MechanismSpec joint type is unsupported."));
            joint.type = *type;
            joint.bodyAId = toUtf8(requiredString(item, QStringLiteral("bodyAId")));
            joint.bodyBId = toUtf8(requiredString(item, QStringLiteral("bodyBId")));
            joint.frameInBodyA = decodeCoordinateFrame(item.value(QStringLiteral("frameInBodyA")), QStringLiteral("joint.frameInBodyA"));
            joint.frameInBodyB = decodeCoordinateFrame(item.value(QStringLiteral("frameInBodyB")), QStringLiteral("joint.frameInBodyB"));
            joint.lowerLimitSi = decodeOptionalNumber(item, QStringLiteral("lowerLimitSi"));
            joint.upperLimitSi = decodeOptionalNumber(item, QStringLiteral("upperLimitSi"));
            mechanism.joints.push_back(std::move(joint));
        }
        const QJsonValue actuatorsValue = object.value(QStringLiteral("actuators"));
        if (!actuatorsValue.isArray()) fail(QStringLiteral("MechanismSpec actuators must be an array."));
        for (const QJsonValue& actuatorValue : actuatorsValue.toArray()) {
            if (!actuatorValue.isObject()) fail(QStringLiteral("MechanismSpec actuator entries must be objects."));
            const QJsonObject item = actuatorValue.toObject();
            DatumActuator actuator;
            actuator.id = toUtf8(requiredString(item, QStringLiteral("id")));
            actuator.name = toUtf8(requiredString(item, QStringLiteral("name")));
            actuator.jointId = toUtf8(requiredString(item, QStringLiteral("jointId")));
            const auto mode = datumActuatorControlModeFromName(toUtf8(requiredString(item, QStringLiteral("mode"))));
            if (!mode.has_value()) fail(QStringLiteral("MechanismSpec actuator mode is unsupported."));
            actuator.mode = *mode;
            actuator.motorId = decodeOptionalString(item, QStringLiteral("motorId"));
            const QJsonValue trajectoryValue = item.value(QStringLiteral("trajectory"));
            if (!trajectoryValue.isArray()) fail(QStringLiteral("MechanismSpec actuator trajectory must be an array."));
            for (const QJsonValue& pointValue : trajectoryValue.toArray()) {
                if (!pointValue.isObject()) fail(QStringLiteral("MechanismSpec actuator trajectory points must be objects."));
                const QJsonObject point = pointValue.toObject();
                actuator.trajectory.push_back({
                    requiredFiniteNumber(point.value(QStringLiteral("timeS")), QStringLiteral("actuator.timeS")),
                    requiredFiniteNumber(point.value(QStringLiteral("valueSi")), QStringLiteral("actuator.valueSi")),
                });
            }
            actuator.maximumEffortSi = decodeOptionalNumber(item, QStringLiteral("maximumEffortSi"));
            actuator.maximumRateSi = decodeOptionalNumber(item, QStringLiteral("maximumRateSi"));
            mechanism.actuators.push_back(std::move(actuator));
        }
        const QJsonValue contactsValue = object.value(QStringLiteral("contacts"));
        if (!contactsValue.isArray()) fail(QStringLiteral("MechanismSpec contacts must be an array."));
        for (const QJsonValue& contactValue : contactsValue.toArray()) {
            if (!contactValue.isObject()) fail(QStringLiteral("MechanismSpec contact entries must be objects."));
            const QJsonObject item = contactValue.toObject();
            DatumContactPair contact;
            contact.id = toUtf8(requiredString(item, QStringLiteral("id")));
            contact.name = toUtf8(requiredString(item, QStringLiteral("name")));
            contact.bodyAId = toUtf8(requiredString(item, QStringLiteral("bodyAId")));
            contact.bodyBId = toUtf8(requiredString(item, QStringLiteral("bodyBId")));
            contact.surfaceAReferenceId = decodeOptionalString(item, QStringLiteral("surfaceAReferenceId"));
            contact.surfaceBReferenceId = decodeOptionalString(item, QStringLiteral("surfaceBReferenceId"));
            const auto law = datumContactLawFromName(toUtf8(requiredString(item, QStringLiteral("law"))));
            if (!law.has_value()) fail(QStringLiteral("MechanismSpec contact law is unsupported."));
            contact.law = *law;
            contact.frictionCoefficient = decodeOptionalNumber(item, QStringLiteral("frictionCoefficient"));
            contact.normalStiffnessNPerM = decodeOptionalNumber(item, QStringLiteral("normalStiffnessNPerM"));
            contact.normalDampingNsPerM = decodeOptionalNumber(item, QStringLiteral("normalDampingNsPerM"));
            contact.restitution = decodeOptionalNumber(item, QStringLiteral("restitution"));
            mechanism.contacts.push_back(std::move(contact));
        }
        const QJsonValue requirementsValue = object.value(QStringLiteral("requirements"));
        if (!requirementsValue.isArray()) fail(QStringLiteral("MechanismSpec requirements must be an array."));
        for (const QJsonValue& requirementValue : requirementsValue.toArray()) {
            if (!requirementValue.isObject()) fail(QStringLiteral("MechanismSpec requirement entries must be objects."));
            const QJsonObject item = requirementValue.toObject();
            DatumRequirement requirement;
            requirement.id = toUtf8(requiredString(item, QStringLiteral("id")));
            requirement.name = toUtf8(requiredString(item, QStringLiteral("name")));
            requirement.metricId = toUtf8(requiredString(item, QStringLiteral("metricId")));
            requirement.unit = toUtf8(requiredString(item, QStringLiteral("unit")));
            const auto relation = datumRequirementRelationFromName(toUtf8(requiredString(item, QStringLiteral("relation"))));
            if (!relation.has_value()) fail(QStringLiteral("MechanismSpec requirement relation is unsupported."));
            requirement.relation = *relation;
            requirement.minimumSi = decodeOptionalNumber(item, QStringLiteral("minimumSi"));
            requirement.maximumSi = decodeOptionalNumber(item, QStringLiteral("maximumSi"));
            requirement.targetSi = decodeOptionalNumber(item, QStringLiteral("targetSi"));
            requirement.toleranceSi = decodeOptionalNumber(item, QStringLiteral("toleranceSi"));
            const QJsonValue hardValue = item.value(QStringLiteral("hardConstraint"));
            if (!hardValue.isBool()) fail(QStringLiteral("MechanismSpec requirement hardConstraint must be boolean."));
            requirement.hardConstraint = hardValue.toBool();
            mechanism.requirements.push_back(std::move(requirement));
        }
    }
    return mechanism;
}

QJsonObject encodeMotor(const DatumMotor& motor)
{
    QJsonValue inertiaValue(QJsonValue::Null);
    if (motor.inertiaTensorKgM2.has_value()) {
        QJsonArray inertia;
        for (double component : *motor.inertiaTensorKgM2) {
            inertia.append(component);
        }
        inertiaValue = inertia;
    }
    QJsonArray torqueCurve;
    for (const DatumMotorTorquePoint& point : motor.torqueCurve) {
        torqueCurve.append(QJsonObject{
            {QStringLiteral("speedRadPerSec"), point.speedRadPerSec},
            {QStringLiteral("torqueNm"), point.torqueNm},
        });
    }
    QJsonArray mountingReferences;
    for (const DatumMountingReference& reference : motor.mountingReferences) {
        mountingReferences.append(QJsonObject{
            {QStringLiteral("role"), fromUtf8(reference.role)},
            {QStringLiteral("objectId"), fromUtf8(reference.objectId)},
            {QStringLiteral("topologyReferenceId"), reference.topologyReferenceId.has_value()
                 ? QJsonValue(fromUtf8(*reference.topologyReferenceId))
                 : QJsonValue(QJsonValue::Null)},
        });
    }
    QJsonValue encoderValue(QJsonValue::Null);
    if (motor.encoder.has_value()) {
        encoderValue = QJsonObject{
            {QStringLiteral("type"), fromUtf8(motor.encoder->type)},
            {QStringLiteral("countsPerRevolution"), QString::number(motor.encoder->countsPerRevolution)},
        };
    }
    QJsonValue supplierValue(QJsonValue::Null);
    if (motor.supplierData.has_value()) {
        supplierValue = QJsonObject{
            {QStringLiteral("manufacturer"), encodeOptionalString(motor.supplierData->manufacturer)},
            {QStringLiteral("partNumber"), encodeOptionalString(motor.supplierData->partNumber)},
            {QStringLiteral("datasheetUrl"), encodeOptionalString(motor.supplierData->datasheetUrl)},
        };
    }
    return {
        {QStringLiteral("id"), fromUtf8(motor.id)},
        {QStringLiteral("mechanismId"), fromUtf8(motor.mechanismId)},
        {QStringLiteral("name"), fromUtf8(motor.name)},
        {QStringLiteral("cadRepresentationObjectId"), motor.cadRepresentationObjectId.has_value()
             ? QJsonValue(fromUtf8(*motor.cadRepresentationObjectId))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("massKg"), encodeOptionalNumber(motor.massKg)},
        {QStringLiteral("inertiaTensorKgM2"), inertiaValue},
        {QStringLiteral("centerOfMassLocalM"), motor.centerOfMassLocalM.has_value()
             ? QJsonValue(encodeVector(*motor.centerOfMassLocalM))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("ratedSpeedRadPerSec"), encodeOptionalNumber(motor.ratedSpeedRadPerSec)},
        {QStringLiteral("maximumSpeedRadPerSec"), encodeOptionalNumber(motor.maximumSpeedRadPerSec)},
        {QStringLiteral("ratedTorqueNm"), encodeOptionalNumber(motor.ratedTorqueNm)},
        {QStringLiteral("peakTorqueNm"), encodeOptionalNumber(motor.peakTorqueNm)},
        {QStringLiteral("torqueCurve"), torqueCurve},
        {QStringLiteral("electrical"), QJsonObject{
            {QStringLiteral("ratedVoltageV"), encodeOptionalNumber(motor.electrical.ratedVoltageV)},
            {QStringLiteral("ratedCurrentA"), encodeOptionalNumber(motor.electrical.ratedCurrentA)},
            {QStringLiteral("peakCurrentA"), encodeOptionalNumber(motor.electrical.peakCurrentA)},
        }},
        {QStringLiteral("thermal"), QJsonObject{
            {QStringLiteral("thermalResistanceKPerW"), encodeOptionalNumber(motor.thermal.thermalResistanceKPerW)},
            {QStringLiteral("thermalCapacityJPerK"), encodeOptionalNumber(motor.thermal.thermalCapacityJPerK)},
            {QStringLiteral("maximumWindingTemperatureK"), encodeOptionalNumber(motor.thermal.maximumWindingTemperatureK)},
        }},
        {QStringLiteral("encoder"), encoderValue},
        {QStringLiteral("mountingReferences"), mountingReferences},
        {QStringLiteral("supplierData"), supplierValue},
    };
}

DatumMotor decodeMotor(const QJsonObject& object)
{
    DatumMotor motor;
    motor.id = toUtf8(requiredString(object, QStringLiteral("id")));
    motor.mechanismId = toUtf8(requiredString(object, QStringLiteral("mechanismId")));
    motor.name = toUtf8(requiredString(object, QStringLiteral("name")));
    motor.cadRepresentationObjectId = decodeOptionalString(object, QStringLiteral("cadRepresentationObjectId"));
    motor.massKg = decodeOptionalNumber(object, QStringLiteral("massKg"));
    const QJsonValue inertiaValue = object.value(QStringLiteral("inertiaTensorKgM2"));
    if (inertiaValue.isArray()) {
        const QJsonArray array = inertiaValue.toArray();
        if (array.size() != 6) {
            fail(QStringLiteral("Motor inertiaTensorKgM2 must contain six symmetric tensor values."));
        }
        DatumInertiaTensorKgM2 inertia{};
        for (qsizetype index = 0; index < array.size(); ++index) {
            inertia[static_cast<std::size_t>(index)] = requiredFiniteNumber(
                array.at(index), QStringLiteral("motor.inertiaTensorKgM2[%1]").arg(index));
        }
        motor.inertiaTensorKgM2 = inertia;
    } else if (!inertiaValue.isNull()) {
        fail(QStringLiteral("Motor inertiaTensorKgM2 must be a six-value array or null."));
    }
    const QJsonValue centerValue = object.value(QStringLiteral("centerOfMassLocalM"));
    if (centerValue.isArray()) {
        motor.centerOfMassLocalM = decodeVector(centerValue, QStringLiteral("motor.centerOfMassLocalM"));
    } else if (!centerValue.isNull()) {
        fail(QStringLiteral("Motor centerOfMassLocalM must be a three-vector or null."));
    }
    motor.ratedSpeedRadPerSec = decodeOptionalNumber(object, QStringLiteral("ratedSpeedRadPerSec"));
    motor.maximumSpeedRadPerSec = decodeOptionalNumber(object, QStringLiteral("maximumSpeedRadPerSec"));
    motor.ratedTorqueNm = decodeOptionalNumber(object, QStringLiteral("ratedTorqueNm"));
    motor.peakTorqueNm = decodeOptionalNumber(object, QStringLiteral("peakTorqueNm"));
    const QJsonValue curveValue = object.value(QStringLiteral("torqueCurve"));
    if (!curveValue.isArray()) {
        fail(QStringLiteral("Motor torqueCurve must be an array."));
    }
    for (const QJsonValue& pointValue : curveValue.toArray()) {
        if (!pointValue.isObject()) {
            fail(QStringLiteral("Motor torqueCurve entries must be objects."));
        }
        const QJsonObject pointObject = pointValue.toObject();
        motor.torqueCurve.push_back({
            requiredFiniteNumber(pointObject.value(QStringLiteral("speedRadPerSec")),
                                 QStringLiteral("motor.torqueCurve.speedRadPerSec")),
            requiredFiniteNumber(pointObject.value(QStringLiteral("torqueNm")),
                                 QStringLiteral("motor.torqueCurve.torqueNm")),
        });
    }
    const QJsonValue electricalValue = object.value(QStringLiteral("electrical"));
    if (!electricalValue.isObject()) {
        fail(QStringLiteral("Motor electrical model must be an object."));
    }
    const QJsonObject electrical = electricalValue.toObject();
    motor.electrical.ratedVoltageV = decodeOptionalNumber(electrical, QStringLiteral("ratedVoltageV"));
    motor.electrical.ratedCurrentA = decodeOptionalNumber(electrical, QStringLiteral("ratedCurrentA"));
    motor.electrical.peakCurrentA = decodeOptionalNumber(electrical, QStringLiteral("peakCurrentA"));

    const QJsonValue thermalValue = object.value(QStringLiteral("thermal"));
    if (!thermalValue.isObject()) {
        fail(QStringLiteral("Motor thermal model must be an object."));
    }
    const QJsonObject thermal = thermalValue.toObject();
    motor.thermal.thermalResistanceKPerW = decodeOptionalNumber(thermal, QStringLiteral("thermalResistanceKPerW"));
    motor.thermal.thermalCapacityJPerK = decodeOptionalNumber(thermal, QStringLiteral("thermalCapacityJPerK"));
    motor.thermal.maximumWindingTemperatureK = decodeOptionalNumber(thermal, QStringLiteral("maximumWindingTemperatureK"));

    const QJsonValue encoderValue = object.value(QStringLiteral("encoder"));
    if (encoderValue.isObject()) {
        const QJsonObject encoder = encoderValue.toObject();
        const QString counts = requiredString(encoder, QStringLiteral("countsPerRevolution"));
        const QByteArray encodedCounts = counts.toLatin1();
        std::uint64_t count = 0;
        const auto parsed = std::from_chars(encodedCounts.constData(),
                                            encodedCounts.constData() + encodedCounts.size(), count);
        if (parsed.ec != std::errc{} || parsed.ptr != encodedCounts.constData() + encodedCounts.size() || count == 0) {
            fail(QStringLiteral("Motor encoder countsPerRevolution must be a positive uint64 decimal string."));
        }
        motor.encoder = DatumMotorEncoder{toUtf8(requiredString(encoder, QStringLiteral("type"))), count};
    } else if (!encoderValue.isNull()) {
        fail(QStringLiteral("Motor encoder must be an object or null."));
    }

    const QJsonValue mountingsValue = object.value(QStringLiteral("mountingReferences"));
    if (!mountingsValue.isArray()) {
        fail(QStringLiteral("Motor mountingReferences must be an array."));
    }
    for (const QJsonValue& mountingValue : mountingsValue.toArray()) {
        if (!mountingValue.isObject()) {
            fail(QStringLiteral("Motor mounting reference entries must be objects."));
        }
        const QJsonObject mounting = mountingValue.toObject();
        DatumMountingReference reference;
        reference.role = toUtf8(requiredString(mounting, QStringLiteral("role")));
        reference.objectId = toUtf8(requiredString(mounting, QStringLiteral("objectId")));
        if (!mounting.contains(QStringLiteral("topologyReferenceId"))) {
            fail(QStringLiteral("Motor mounting topologyReferenceId is missing."));
        }
        const QJsonValue topologyValue = mounting.value(QStringLiteral("topologyReferenceId"));
        if (topologyValue.isString()) {
            reference.topologyReferenceId = toUtf8(topologyValue.toString());
        } else if (!topologyValue.isNull() && !topologyValue.isUndefined()) {
            fail(QStringLiteral("Motor mounting topologyReferenceId must be a UUID or null."));
        }
        motor.mountingReferences.push_back(std::move(reference));
    }

    const QJsonValue supplierValue = object.value(QStringLiteral("supplierData"));
    if (supplierValue.isObject()) {
        const QJsonObject supplier = supplierValue.toObject();
        motor.supplierData = DatumMotorSupplierData{
            decodeOptionalString(supplier, QStringLiteral("manufacturer")),
            decodeOptionalString(supplier, QStringLiteral("partNumber")),
            decodeOptionalString(supplier, QStringLiteral("datasheetUrl")),
        };
    } else if (!supplierValue.isNull()) {
        fail(QStringLiteral("Motor supplierData must be an object or null."));
    }
    return motor;
}

QJsonArray encodeStringArray(const std::vector<std::string>& values)
{
    QJsonArray result;
    for (const std::string& value : values) result.append(fromUtf8(value));
    return result;
}

std::vector<std::string> decodeStringArray(const QJsonValue& value, const QString& field)
{
    if (!value.isArray()) fail(QStringLiteral("Engineering field '%1' must be an array.").arg(field));
    std::vector<std::string> result;
    for (const QJsonValue& item : value.toArray()) {
        if (!item.isString() || item.toString().trimmed().isEmpty()) {
            fail(QStringLiteral("Engineering field '%1' entries must be non-empty strings.").arg(field));
        }
        result.push_back(toUtf8(item.toString()));
    }
    return result;
}

QJsonObject encodeBoundaryCondition(const BoundaryCondition& condition)
{
    QJsonObject result{
        {QStringLiteral("id"), fromUtf8(boundaryConditionId(condition))},
        {QStringLiteral("name"), fromUtf8(std::visit([](const auto& item) { return item.name; }, condition))},
        {QStringLiteral("cadObjectId"), fromUtf8(boundaryConditionCadObjectId(condition))},
    };
    std::visit([&result](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, FixedDisplacement>) {
            QJsonArray components;
            for (const bool constrained : item.constrainedComponents) components.append(constrained);
            result.insert(QStringLiteral("type"), QStringLiteral("fixed_displacement"));
            result.insert(QStringLiteral("topologyReferenceId"), fromUtf8(item.topologyReferenceId));
            result.insert(QStringLiteral("displacementM"), encodeVector(item.displacementM));
            result.insert(QStringLiteral("constrainedComponents"), components);
        } else if constexpr (std::is_same_v<T, AppliedForce>) {
            result.insert(QStringLiteral("type"), QStringLiteral("applied_force"));
            result.insert(QStringLiteral("topologyReferenceId"), fromUtf8(item.topologyReferenceId));
            result.insert(QStringLiteral("totalForceN"), encodeVector(item.totalForceN));
        } else if constexpr (std::is_same_v<T, AppliedPressure>) {
            result.insert(QStringLiteral("type"), QStringLiteral("applied_pressure"));
            result.insert(QStringLiteral("topologyReferenceId"), fromUtf8(item.topologyReferenceId));
            result.insert(QStringLiteral("pressurePa"), item.pressurePa);
        } else if constexpr (std::is_same_v<T, BodyForce>) {
            result.insert(QStringLiteral("type"), QStringLiteral("body_force"));
            result.insert(QStringLiteral("forceDensityNPerM3"), encodeVector(item.forceDensityNPerM3));
        } else if constexpr (std::is_same_v<T, InitialCondition>) {
            result.insert(QStringLiteral("type"), QStringLiteral("initial_condition"));
            result.insert(QStringLiteral("displacementM"), encodeVector(item.displacementM));
            result.insert(QStringLiteral("velocityMPerS"), encodeVector(item.velocityMPerS));
        }
    }, condition);
    return result;
}

BoundaryCondition decodeBoundaryCondition(const QJsonObject& object)
{
    const std::string type = toUtf8(requiredString(object, QStringLiteral("type")));
    const std::string id = toUtf8(requiredString(object, QStringLiteral("id")));
    const std::string name = toUtf8(requiredString(object, QStringLiteral("name")));
    const std::string cadObjectId = toUtf8(requiredString(object, QStringLiteral("cadObjectId")));
    if (type == "fixed_displacement") {
        FixedDisplacement result;
        result.id = id;
        result.name = name;
        result.cadObjectId = cadObjectId;
        result.topologyReferenceId = toUtf8(requiredString(object, QStringLiteral("topologyReferenceId")));
        result.displacementM = decodeVector(object.value(QStringLiteral("displacementM")),
                                            QStringLiteral("condition.displacementM"));
        const QJsonValue componentsValue = object.value(QStringLiteral("constrainedComponents"));
        if (!componentsValue.isArray() || componentsValue.toArray().size() != 3) {
            fail(QStringLiteral("Fixed displacement constrainedComponents must contain three booleans."));
        }
        for (qsizetype index = 0; index < 3; ++index) {
            const QJsonValue component = componentsValue.toArray().at(index);
            if (!component.isBool()) fail(QStringLiteral("Fixed displacement components must be booleans."));
            result.constrainedComponents[static_cast<std::size_t>(index)] = component.toBool();
        }
        return result;
    }
    if (type == "applied_force") {
        return AppliedForce{id, name, cadObjectId,
                            toUtf8(requiredString(object, QStringLiteral("topologyReferenceId"))),
                            decodeVector(object.value(QStringLiteral("totalForceN")),
                                         QStringLiteral("condition.totalForceN"))};
    }
    if (type == "applied_pressure") {
        return AppliedPressure{id, name, cadObjectId,
                               toUtf8(requiredString(object, QStringLiteral("topologyReferenceId"))),
                               requiredFiniteNumber(object.value(QStringLiteral("pressurePa")),
                                                    QStringLiteral("condition.pressurePa"))};
    }
    if (type == "body_force") {
        return BodyForce{id, name, cadObjectId,
                         decodeVector(object.value(QStringLiteral("forceDensityNPerM3")),
                                      QStringLiteral("condition.forceDensityNPerM3"))};
    }
    if (type == "initial_condition") {
        return InitialCondition{id, name, cadObjectId,
                                decodeVector(object.value(QStringLiteral("displacementM")),
                                             QStringLiteral("condition.displacementM")),
                                decodeVector(object.value(QStringLiteral("velocityMPerS")),
                                             QStringLiteral("condition.velocityMPerS"))};
    }
    fail(QStringLiteral("Unknown structural condition type '%1'.").arg(QString::fromUtf8(type)));
}

QJsonObject encodeAnalysisMeshProvenance(const AnalysisMeshProvenance& mesh)
{
    return {{QStringLiteral("revisionId"), fromUtf8(mesh.revisionId)},
            {QStringLiteral("generator"), fromUtf8(mesh.generator)},
            {QStringLiteral("generatorVersion"), fromUtf8(mesh.generatorVersion)},
            {QStringLiteral("elementType"), fromUtf8(mesh.elementType)},
            {QStringLiteral("requestedMaximumElementSizeM"), mesh.requestedMaximumElementSizeM},
            {QStringLiteral("nodeCount"), static_cast<double>(mesh.nodeCount)},
            {QStringLiteral("elementCount"), static_cast<double>(mesh.elementCount)},
            {QStringLiteral("minimumScaledJacobian"), mesh.minimumScaledJacobian},
            {QStringLiteral("maximumAspectRatio"), mesh.maximumAspectRatio}};
}

AnalysisMeshProvenance decodeAnalysisMeshProvenance(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("Analysis mesh provenance must be an object."));
    const QJsonObject object = value.toObject();
    AnalysisMeshProvenance mesh;
    mesh.revisionId = toUtf8(requiredString(object, QStringLiteral("revisionId")));
    mesh.generator = toUtf8(requiredString(object, QStringLiteral("generator")));
    mesh.generatorVersion = toUtf8(requiredString(object, QStringLiteral("generatorVersion")));
    mesh.elementType = toUtf8(requiredString(object, QStringLiteral("elementType")));
    mesh.requestedMaximumElementSizeM =
        requiredFiniteNumber(object.value(QStringLiteral("requestedMaximumElementSizeM")),
                             QStringLiteral("analysis.mesh.requestedMaximumElementSizeM"));
    const double nodes = requiredFiniteNumber(object.value(QStringLiteral("nodeCount")),
                                              QStringLiteral("analysis.mesh.nodeCount"));
    const double elements = requiredFiniteNumber(object.value(QStringLiteral("elementCount")),
                                                  QStringLiteral("analysis.mesh.elementCount"));
    if (nodes < 0.0 || std::floor(nodes) != nodes || elements < 0.0 || std::floor(elements) != elements) {
        fail(QStringLiteral("Analysis mesh nodeCount and elementCount must be non-negative integers."));
    }
    mesh.nodeCount = static_cast<std::uint64_t>(nodes);
    mesh.elementCount = static_cast<std::uint64_t>(elements);
    mesh.minimumScaledJacobian =
        requiredFiniteNumber(object.value(QStringLiteral("minimumScaledJacobian")),
                             QStringLiteral("analysis.mesh.minimumScaledJacobian"));
    mesh.maximumAspectRatio =
        requiredFiniteNumber(object.value(QStringLiteral("maximumAspectRatio")),
                             QStringLiteral("analysis.mesh.maximumAspectRatio"));
    return mesh;
}

QJsonObject encodeFieldDataset(const FieldDataset& field)
{
    QJsonArray values;
    for (const double value : field.values) values.append(value);
    return {{QStringLiteral("id"), fromUtf8(field.id)},
            {QStringLiteral("fieldType"), fromUtf8(fieldTypeName(field.fieldType))},
            {QStringLiteral("location"), fromUtf8(fieldLocationName(field.location))},
            {QStringLiteral("components"), encodeStringArray(field.components)},
            {QStringLiteral("units"), fromUtf8(field.units)},
            {QStringLiteral("meshRevisionId"), fromUtf8(field.meshRevisionId)},
            {QStringLiteral("solverRunId"), fromUtf8(field.solverRunId)},
            {QStringLiteral("values"), values}};
}

QJsonObject encodeAnalysisMeshSnapshot(const AnalysisMeshSnapshot& snapshot)
{
    QJsonArray nodes;
    for (const Vector3& position : snapshot.nodePositionsM) nodes.append(encodeVector(position));
    QJsonArray hexahedra;
    for (const auto& element : snapshot.hexahedra) {
        QJsonArray indices;
        for (const std::uint32_t index : element) indices.append(static_cast<double>(index));
        hexahedra.append(indices);
    }
    return {{QStringLiteral("meshRevisionId"), fromUtf8(snapshot.meshRevisionId)},
            {QStringLiteral("nodePositionsM"), nodes},
            {QStringLiteral("hexahedra"), hexahedra}};
}

FieldDataset decodeFieldDataset(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("Structural field dataset entries must be objects."));
    const QJsonObject object = value.toObject();
    FieldDataset field;
    field.id = toUtf8(requiredString(object, QStringLiteral("id")));
    const std::string typeName = toUtf8(requiredString(object, QStringLiteral("fieldType")));
    const auto type = fieldTypeFromName(typeName);
    if (!type.has_value()) fail(QStringLiteral("Unknown structural field type '%1'.").arg(fromUtf8(typeName)));
    field.fieldType = *type;
    const std::string locationName = toUtf8(requiredString(object, QStringLiteral("location")));
    const auto location = fieldLocationFromName(locationName);
    if (!location.has_value()) fail(QStringLiteral("Unknown structural field location '%1'.").arg(fromUtf8(locationName)));
    field.location = *location;
    field.components = decodeStringArray(object.value(QStringLiteral("components")), QStringLiteral("field.components"));
    field.units = toUtf8(requiredString(object, QStringLiteral("units")));
    field.meshRevisionId = toUtf8(requiredString(object, QStringLiteral("meshRevisionId")));
    field.solverRunId = toUtf8(requiredString(object, QStringLiteral("solverRunId")));
    const QJsonValue values = object.value(QStringLiteral("values"));
    if (!values.isArray()) fail(QStringLiteral("Structural field values must be an array."));
    field.values.reserve(static_cast<std::size_t>(values.toArray().size()));
    for (const QJsonValue& valueItem : values.toArray()) {
        field.values.push_back(requiredFiniteNumber(valueItem, QStringLiteral("field.values[]")));
    }
    return field;
}

QJsonObject encodeAnalysisRunProvenance(const AnalysisRunProvenance& provenance)
{
    return {{QStringLiteral("solverRunId"), fromUtf8(provenance.solverRunId)},
            {QStringLiteral("timestampUtc"), fromUtf8(provenance.timestampUtc)},
            {QStringLiteral("sourceProjectId"), fromUtf8(provenance.sourceProjectId)},
            {QStringLiteral("sourceDocumentRevision"), static_cast<double>(provenance.sourceDocumentRevision)},
            {QStringLiteral("cadObjectId"), fromUtf8(provenance.cadObjectId)},
            {QStringLiteral("geometryFeatureId"), encodeOptionalString(provenance.geometryFeatureId)},
            {QStringLiteral("materialDefinitionId"), fromUtf8(provenance.materialDefinitionId)},
            {QStringLiteral("materialRevision"), fromUtf8(provenance.materialRevision)},
            {QStringLiteral("materialStateId"), encodeOptionalString(provenance.materialStateId)},
            {QStringLiteral("analysisTemperatureK"), encodeOptionalNumber(provenance.analysisTemperatureK)},
            {QStringLiteral("temperatureRangeVerified"), provenance.temperatureRangeVerified},
            {QStringLiteral("equationDefinitionId"), fromUtf8(provenance.equationDefinitionId)},
            {QStringLiteral("equationMaturity"), fromUtf8(modelMaturityName(provenance.equationMaturity))},
            {QStringLiteral("solverId"), fromUtf8(provenance.solverId)},
            {QStringLiteral("solverVersion"), fromUtf8(provenance.solverVersion)},
            {QStringLiteral("mesh"), encodeAnalysisMeshProvenance(provenance.mesh)},
            {QStringLiteral("topologyReferenceIds"), encodeStringArray(provenance.topologyReferenceIds)},
            {QStringLiteral("boundaryConditionIds"), encodeStringArray(provenance.boundaryConditionIds)},
            {QStringLiteral("absoluteTolerance"), provenance.absoluteTolerance},
            {QStringLiteral("relativeTolerance"), provenance.relativeTolerance},
            {QStringLiteral("maximumIterations"), static_cast<double>(provenance.maximumIterations)},
            {QStringLiteral("agentProposalId"), encodeOptionalString(provenance.agentProposalId)}};
}

ModelMaturity decodeModelMaturity(const QJsonValue& value)
{
    const QString name = requiredString(QJsonObject{{QStringLiteral("maturity"), value}},
                                        QStringLiteral("maturity"));
    const std::string text = toUtf8(name);
    if (text == "E0") return ModelMaturity::E0;
    if (text == "E1") return ModelMaturity::E1;
    if (text == "E2") return ModelMaturity::E2;
    if (text == "E3") return ModelMaturity::E3;
    if (text == "E4") return ModelMaturity::E4;
    if (text == "E5") return ModelMaturity::E5;
    if (text == "E6") return ModelMaturity::E6;
    fail(QStringLiteral("Unknown engineering equation maturity '%1'.").arg(name));
}

AnalysisRunProvenance decodeAnalysisRunProvenance(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("Structural run provenance must be an object."));
    const QJsonObject object = value.toObject();
    AnalysisRunProvenance provenance;
    provenance.solverRunId = toUtf8(requiredString(object, QStringLiteral("solverRunId")));
    provenance.timestampUtc = toUtf8(requiredString(object, QStringLiteral("timestampUtc")));
    provenance.sourceProjectId = toUtf8(requiredString(object, QStringLiteral("sourceProjectId")));
    const double documentRevision = requiredFiniteNumber(
        object.value(QStringLiteral("sourceDocumentRevision")), QStringLiteral("provenance.sourceDocumentRevision"));
    if (documentRevision < 0.0 || std::floor(documentRevision) != documentRevision) {
        fail(QStringLiteral("Source document revision must be a non-negative integer."));
    }
    provenance.sourceDocumentRevision = static_cast<std::uint64_t>(documentRevision);
    provenance.cadObjectId = toUtf8(requiredString(object, QStringLiteral("cadObjectId")));
    provenance.geometryFeatureId = decodeOptionalString(object, QStringLiteral("geometryFeatureId"));
    provenance.materialDefinitionId = toUtf8(requiredString(object, QStringLiteral("materialDefinitionId")));
    provenance.materialRevision = toUtf8(requiredString(object, QStringLiteral("materialRevision")));
    provenance.materialStateId = decodeOptionalString(object, QStringLiteral("materialStateId"));
    provenance.analysisTemperatureK = decodeOptionalNumber(object, QStringLiteral("analysisTemperatureK"));
    const QJsonValue temperatureVerified = object.value(QStringLiteral("temperatureRangeVerified"));
    if (!temperatureVerified.isBool()) fail(QStringLiteral("temperatureRangeVerified must be boolean."));
    provenance.temperatureRangeVerified = temperatureVerified.toBool();
    provenance.equationDefinitionId = toUtf8(requiredString(object, QStringLiteral("equationDefinitionId")));
    provenance.equationMaturity = decodeModelMaturity(object.value(QStringLiteral("equationMaturity")));
    provenance.solverId = toUtf8(requiredString(object, QStringLiteral("solverId")));
    provenance.solverVersion = toUtf8(requiredString(object, QStringLiteral("solverVersion")));
    provenance.mesh = decodeAnalysisMeshProvenance(object.value(QStringLiteral("mesh")));
    provenance.topologyReferenceIds = decodeStringArray(object.value(QStringLiteral("topologyReferenceIds")),
                                                          QStringLiteral("provenance.topologyReferenceIds"));
    provenance.boundaryConditionIds = decodeStringArray(object.value(QStringLiteral("boundaryConditionIds")),
                                                         QStringLiteral("provenance.boundaryConditionIds"));
    provenance.absoluteTolerance =
        requiredFiniteNumber(object.value(QStringLiteral("absoluteTolerance")), QStringLiteral("provenance.absoluteTolerance"));
    provenance.relativeTolerance =
        requiredFiniteNumber(object.value(QStringLiteral("relativeTolerance")), QStringLiteral("provenance.relativeTolerance"));
    const double maximumIterations = requiredFiniteNumber(
        object.value(QStringLiteral("maximumIterations")), QStringLiteral("provenance.maximumIterations"));
    if (maximumIterations < 0.0 || std::floor(maximumIterations) != maximumIterations ||
        maximumIterations > std::numeric_limits<std::uint32_t>::max()) {
        fail(QStringLiteral("Maximum solver iterations must be a non-negative integer."));
    }
    provenance.maximumIterations = static_cast<std::uint32_t>(maximumIterations);
    provenance.agentProposalId = decodeOptionalString(object, QStringLiteral("agentProposalId"));
    return provenance;
}

QJsonObject encodeStructuralRun(const StructuralAnalysisRun& run)
{
    QJsonArray fields;
    for (const FieldDataset& field : run.fields) fields.append(encodeFieldDataset(field));
    QJsonValue meshSnapshot(QJsonValue::Null);
    if (run.meshSnapshot.has_value()) {
        meshSnapshot = encodeAnalysisMeshSnapshot(*run.meshSnapshot);
    }
    return {{QStringLiteral("provenance"), encodeAnalysisRunProvenance(run.provenance)},
            {QStringLiteral("meshSnapshot"), meshSnapshot},
            {QStringLiteral("converged"), run.converged},
            {QStringLiteral("iterations"), static_cast<double>(run.iterations)},
            {QStringLiteral("relativeResidual"), run.relativeResidual},
            {QStringLiteral("appliedForceN"), encodeVector(run.appliedForceN)},
            {QStringLiteral("reactionForceN"), encodeVector(run.reactionForceN)},
            {QStringLiteral("relativeForceBalanceError"), run.relativeForceBalanceError},
            {QStringLiteral("tipDisplacementM"), run.tipDisplacementM},
            {QStringLiteral("maximumDisplacementM"), run.maximumDisplacementM},
            {QStringLiteral("sectionAxialStressPa"), run.sectionAxialStressPa},
            {QStringLiteral("sectionStressSamplePositionM"), encodeVector(run.sectionStressSamplePositionM)},
            {QStringLiteral("maximumVonMisesStressPa"), run.maximumVonMisesStressPa},
            {QStringLiteral("strainEnergyJ"), run.strainEnergyJ},
            {QStringLiteral("fields"), fields}};
}

StructuralAnalysisRun decodeStructuralRun(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("Structural analysis run entries must be objects."));
    const QJsonObject object = value.toObject();
    StructuralAnalysisRun run;
    run.provenance = decodeAnalysisRunProvenance(object.value(QStringLiteral("provenance")));
    const QJsonValue snapshotValue = object.value(QStringLiteral("meshSnapshot"));
    if (snapshotValue.isObject()) {
        const QJsonObject snapshotObject = snapshotValue.toObject();
        AnalysisMeshSnapshot snapshot;
        snapshot.meshRevisionId = toUtf8(requiredString(snapshotObject, QStringLiteral("meshRevisionId")));
        const QJsonValue nodesValue = snapshotObject.value(QStringLiteral("nodePositionsM"));
        const QJsonValue hexahedraValue = snapshotObject.value(QStringLiteral("hexahedra"));
        if (!nodesValue.isArray() || !hexahedraValue.isArray()) {
            fail(QStringLiteral("Structural mesh snapshot nodes and hexahedra must be arrays."));
        }
        const QJsonArray nodes = nodesValue.toArray();
        const QJsonArray hexahedra = hexahedraValue.toArray();
        if (static_cast<std::uint64_t>(nodes.size()) != run.provenance.mesh.nodeCount ||
            static_cast<std::uint64_t>(hexahedra.size()) != run.provenance.mesh.elementCount) {
            fail(QStringLiteral("Structural mesh snapshot counts do not match their run provenance."));
        }
        snapshot.nodePositionsM.reserve(static_cast<std::size_t>(nodes.size()));
        for (const QJsonValue& nodeValue : nodes) {
            snapshot.nodePositionsM.push_back(decodeVector(nodeValue, QStringLiteral("meshSnapshot.nodePositionsM[]")));
        }
        snapshot.hexahedra.reserve(static_cast<std::size_t>(hexahedra.size()));
        for (const QJsonValue& elementValue : hexahedra) {
            if (!elementValue.isArray() || elementValue.toArray().size() != 8) {
                fail(QStringLiteral("Structural mesh snapshot elements must contain eight node indices."));
            }
            const QJsonArray element = elementValue.toArray();
            std::array<std::uint32_t, 8> indices{};
            for (qsizetype index = 0; index < element.size(); ++index) {
                const double nodeIndex = requiredFiniteNumber(
                    element.at(index), QStringLiteral("meshSnapshot.hexahedra[][]"));
                if (nodeIndex < 0.0 || std::floor(nodeIndex) != nodeIndex ||
                    nodeIndex > std::numeric_limits<std::uint32_t>::max()) {
                    fail(QStringLiteral("Structural mesh snapshot node indices must be non-negative 32-bit integers."));
                }
                indices[static_cast<std::size_t>(index)] = static_cast<std::uint32_t>(nodeIndex);
            }
            snapshot.hexahedra.push_back(indices);
        }
        try {
            snapshot.validate(run.provenance.mesh);
        } catch (const std::invalid_argument& error) {
            fail(QStringLiteral("Structural mesh snapshot is invalid: %1").arg(QString::fromUtf8(error.what())));
        }
        run.meshSnapshot = std::move(snapshot);
    } else if (!snapshotValue.isNull() && !snapshotValue.isUndefined()) {
        fail(QStringLiteral("Structural run meshSnapshot must be an object or null."));
    }
    const QJsonValue converged = object.value(QStringLiteral("converged"));
    if (!converged.isBool()) fail(QStringLiteral("Structural run converged must be boolean."));
    run.converged = converged.toBool();
    const double iterations = requiredFiniteNumber(object.value(QStringLiteral("iterations")),
                                                   QStringLiteral("run.iterations"));
    if (iterations < 0.0 || std::floor(iterations) != iterations ||
        iterations > std::numeric_limits<std::uint32_t>::max()) {
        fail(QStringLiteral("Structural run iterations must be a non-negative integer."));
    }
    run.iterations = static_cast<std::uint32_t>(iterations);
    run.relativeResidual =
        requiredFiniteNumber(object.value(QStringLiteral("relativeResidual")), QStringLiteral("run.relativeResidual"));
    run.appliedForceN = decodeVector(object.value(QStringLiteral("appliedForceN")), QStringLiteral("run.appliedForceN"));
    run.reactionForceN = decodeVector(object.value(QStringLiteral("reactionForceN")), QStringLiteral("run.reactionForceN"));
    run.relativeForceBalanceError = requiredFiniteNumber(object.value(QStringLiteral("relativeForceBalanceError")),
                                                         QStringLiteral("run.relativeForceBalanceError"));
    run.tipDisplacementM = requiredFiniteNumber(object.value(QStringLiteral("tipDisplacementM")),
                                                 QStringLiteral("run.tipDisplacementM"));
    run.maximumDisplacementM = requiredFiniteNumber(object.value(QStringLiteral("maximumDisplacementM")),
                                                     QStringLiteral("run.maximumDisplacementM"));
    run.sectionAxialStressPa = requiredFiniteNumber(object.value(QStringLiteral("sectionAxialStressPa")),
                                                     QStringLiteral("run.sectionAxialStressPa"));
    run.sectionStressSamplePositionM = decodeVector(object.value(QStringLiteral("sectionStressSamplePositionM")),
                                                     QStringLiteral("run.sectionStressSamplePositionM"));
    run.maximumVonMisesStressPa = requiredFiniteNumber(object.value(QStringLiteral("maximumVonMisesStressPa")),
                                                        QStringLiteral("run.maximumVonMisesStressPa"));
    run.strainEnergyJ = requiredFiniteNumber(object.value(QStringLiteral("strainEnergyJ")),
                                              QStringLiteral("run.strainEnergyJ"));
    const QJsonValue fields = object.value(QStringLiteral("fields"));
    if (!fields.isArray()) fail(QStringLiteral("Structural run fields must be an array."));
    for (const QJsonValue& field : fields.toArray()) run.fields.push_back(decodeFieldDataset(field));
    return run;
}

QJsonObject encodeStructuralStudy(const StructuralAnalysisStudy& study)
{
    QJsonArray conditions;
    for (const BoundaryCondition& condition : study.boundaryConditions) {
        conditions.append(encodeBoundaryCondition(condition));
    }
    QJsonArray runs;
    for (const StructuralAnalysisRun& run : study.runs) runs.append(encodeStructuralRun(run));
    QJsonValue convergence(QJsonValue::Null);
    if (study.meshConvergence.has_value()) {
        const StructuralMeshConvergence& item = *study.meshConvergence;
        convergence = QJsonObject{{QStringLiteral("solverRunIds"), encodeStringArray(item.solverRunIds)},
                                  {QStringLiteral("relativeTipDisplacementChange"), item.relativeTipDisplacementChange},
                                  {QStringLiteral("relativeStrainEnergyChange"), item.relativeStrainEnergyChange},
                                  {QStringLiteral("relativeSectionStressChange"), item.relativeSectionStressChange},
                                  {QStringLiteral("tolerance"), item.tolerance},
                                  {QStringLiteral("converged"), item.converged}};
    }
    return {{QStringLiteral("id"), fromUtf8(study.id)},
            {QStringLiteral("name"), fromUtf8(study.name)},
            {QStringLiteral("cadObjectId"), fromUtf8(study.cadObjectId)},
            {QStringLiteral("materialDefinitionId"), fromUtf8(study.materialDefinitionId)},
            {QStringLiteral("materialStateId"), encodeOptionalString(study.materialStateId)},
            {QStringLiteral("equationDefinitionId"), fromUtf8(study.equationDefinitionId)},
            {QStringLiteral("boundaryConditions"), conditions},
            {QStringLiteral("runs"), runs},
            {QStringLiteral("meshConvergence"), convergence}};
}

StructuralAnalysisStudy decodeStructuralStudy(const QJsonValue& value)
{
    if (!value.isObject()) fail(QStringLiteral("Structural study entries must be objects."));
    const QJsonObject object = value.toObject();
    StructuralAnalysisStudy study;
    study.id = toUtf8(requiredString(object, QStringLiteral("id")));
    study.name = toUtf8(requiredString(object, QStringLiteral("name")));
    study.cadObjectId = toUtf8(requiredString(object, QStringLiteral("cadObjectId")));
    study.materialDefinitionId = toUtf8(requiredString(object, QStringLiteral("materialDefinitionId")));
    study.materialStateId = decodeOptionalString(object, QStringLiteral("materialStateId"));
    study.equationDefinitionId = toUtf8(requiredString(object, QStringLiteral("equationDefinitionId")));
    const QJsonValue conditions = object.value(QStringLiteral("boundaryConditions"));
    if (!conditions.isArray()) fail(QStringLiteral("Structural study boundaryConditions must be an array."));
    for (const QJsonValue& condition : conditions.toArray()) {
        if (!condition.isObject()) fail(QStringLiteral("Structural study condition entries must be objects."));
        study.boundaryConditions.push_back(decodeBoundaryCondition(condition.toObject()));
    }
    const QJsonValue runs = object.value(QStringLiteral("runs"));
    if (!runs.isArray()) fail(QStringLiteral("Structural study runs must be an array."));
    for (const QJsonValue& run : runs.toArray()) study.runs.push_back(decodeStructuralRun(run));
    const QJsonValue convergence = object.value(QStringLiteral("meshConvergence"));
    if (convergence.isObject()) {
        const QJsonObject item = convergence.toObject();
        StructuralMeshConvergence record;
        record.solverRunIds = decodeStringArray(item.value(QStringLiteral("solverRunIds")),
                                                QStringLiteral("convergence.solverRunIds"));
        record.relativeTipDisplacementChange = requiredFiniteNumber(
            item.value(QStringLiteral("relativeTipDisplacementChange")),
            QStringLiteral("convergence.relativeTipDisplacementChange"));
        record.relativeStrainEnergyChange = requiredFiniteNumber(
            item.value(QStringLiteral("relativeStrainEnergyChange")),
            QStringLiteral("convergence.relativeStrainEnergyChange"));
        record.relativeSectionStressChange = requiredFiniteNumber(
            item.value(QStringLiteral("relativeSectionStressChange")),
            QStringLiteral("convergence.relativeSectionStressChange"));
        record.tolerance = requiredFiniteNumber(item.value(QStringLiteral("tolerance")),
                                                QStringLiteral("convergence.tolerance"));
        const QJsonValue converged = item.value(QStringLiteral("converged"));
        if (!converged.isBool()) fail(QStringLiteral("Mesh convergence status must be boolean."));
        record.converged = converged.toBool();
        study.meshConvergence = std::move(record);
    } else if (!convergence.isNull()) {
        fail(QStringLiteral("Structural study meshConvergence must be an object or null."));
    }
    return study;
}

QJsonObject encodeEngineeringModel(const DatumEngineeringModel& model)
{
    QJsonArray parts;
    for (const auto& part : model.partRequirements) parts.append(encodePartRequirements(part));
    QJsonArray mechanisms;
    for (const DatumMechanism& mechanism : model.mechanisms) {
        mechanisms.append(encodeMechanism(mechanism));
    }
    QJsonArray motors;
    for (const DatumMotor& motor : model.motors) {
        motors.append(encodeMotor(motor));
    }
    QJsonArray studies;
    for (const StructuralAnalysisStudy& study : model.structuralStudies) {
        studies.append(encodeStructuralStudy(study));
    }
    return {
        {QStringLiteral("mechanisms"), mechanisms},
        {QStringLiteral("motors"), motors},
        {QStringLiteral("structuralStudies"), studies},
        {QStringLiteral("partRequirements"), parts},
    };
}

DatumEngineeringModel decodeEngineeringModel(const QJsonObject& object, int projectSchemaVersion)
{
    DatumEngineeringModel model;
    const auto parts = object.value(QStringLiteral("partRequirements"));
    if (projectSchemaVersion >= 9) {
        if (!parts.isArray()) fail(QStringLiteral("Schema-nine partRequirements must be an array."));
        for (const auto& part : parts.toArray()) {
            if (!part.isObject()) fail(QStringLiteral("Part requirements entry must be an object."));
            model.partRequirements.push_back(decodePartRequirements(part.toObject()));
        }
    } else if (!parts.isUndefined() && (!parts.isArray() || !parts.toArray().isEmpty())) {
        fail(QStringLiteral("Part requirements require schema version nine or newer."));
    }
    const QJsonValue mechanismsValue = object.value(QStringLiteral("mechanisms"));
    const QJsonValue motorsValue = object.value(QStringLiteral("motors"));
    if (!mechanismsValue.isArray() || !motorsValue.isArray()) {
        fail(QStringLiteral("Engineering model mechanisms and motors must be arrays."));
    }
    for (const QJsonValue& mechanismValue : mechanismsValue.toArray()) {
        if (!mechanismValue.isObject()) {
            fail(QStringLiteral("Engineering mechanism entries must be objects."));
        }
        model.mechanisms.push_back(decodeMechanism(mechanismValue.toObject(), projectSchemaVersion));
    }
    for (const QJsonValue& motorValue : motorsValue.toArray()) {
        if (!motorValue.isObject()) {
            fail(QStringLiteral("Engineering motor entries must be objects."));
        }
        model.motors.push_back(decodeMotor(motorValue.toObject()));
    }
    const QJsonValue studiesValue = object.value(QStringLiteral("structuralStudies"));
    if (projectSchemaVersion >= 7 && !studiesValue.isArray()) {
        fail(QStringLiteral("Schema-seven engineering model structuralStudies must be an array."));
    }
    if (studiesValue.isArray()) {
        if (projectSchemaVersion < 7 && !studiesValue.toArray().isEmpty()) {
            fail(QStringLiteral("Structural analyses require schema version seven or newer."));
        }
        if (projectSchemaVersion < 7) {
            // Older projects may carry the empty array emitted by a newer
            // writer during migration tests; it contains no unsupported data.
        } else {
            for (const QJsonValue& studyValue : studiesValue.toArray()) {
                model.structuralStudies.push_back(decodeStructuralStudy(studyValue));
            }
        }
    } else if (!studiesValue.isUndefined()) {
        fail(QStringLiteral("Engineering structuralStudies must be an array."));
    }
    try {
        model.validate();
    } catch (const std::invalid_argument& error) {
        fail(QStringLiteral("Engineering model is invalid: %1").arg(QString::fromUtf8(error.what())));
    }
    return model;
}

QJsonObject encodeTopologyReference(const DatumTopologyReference& reference)
{
    QJsonArray adjacent;
    for (const DatumTopologyNeighborSignature& neighbor : reference.geometrySignature.adjacent) {
        adjacent.append(QJsonObject{
            {QStringLiteral("subshapeType"), fromUtf8(datumSubshapeTypeName(neighbor.subshapeType))},
            {QStringLiteral("geometryType"), fromUtf8(neighbor.geometryType)},
            {QStringLiteral("measure"), neighbor.measure},
        });
    }
    QJsonObject signature{
        {QStringLiteral("geometryType"), fromUtf8(reference.geometrySignature.geometryType)},
        {QStringLiteral("measure"), reference.geometrySignature.measure},
        {QStringLiteral("centroidMm"), encodeVector(reference.geometrySignature.centroidMm)},
        {QStringLiteral("direction"), reference.geometrySignature.direction.has_value()
             ? QJsonValue(encodeVector(*reference.geometrySignature.direction))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("adjacent"), adjacent},
    };
    QJsonArray parents;
    for (const std::string& parentId : reference.parentReferenceIds) {
        parents.append(fromUtf8(parentId));
    }
    return {
        {QStringLiteral("id"), fromUtf8(reference.id)},
        {QStringLiteral("objectId"), fromUtf8(reference.objectId)},
        {QStringLiteral("featureId"), reference.featureId.has_value()
             ? QJsonValue(fromUtf8(*reference.featureId))
             : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("subshapeType"), fromUtf8(datumSubshapeTypeName(reference.subshapeType))},
        {QStringLiteral("topologySignature"), fromUtf8(reference.topologySignature)},
        {QStringLiteral("geometrySignature"), signature},
        {QStringLiteral("parentReferenceIds"), parents},
        {QStringLiteral("transientIndexHint"), reference.transientIndexHint.has_value()
             ? QJsonValue(static_cast<double>(*reference.transientIndexHint))
             : QJsonValue(QJsonValue::Null)},
    };
}

DatumTopologyReference decodeTopologyReference(const QJsonObject& object)
{
    DatumTopologyReference reference;
    reference.id = toUtf8(requiredString(object, QStringLiteral("id")));
    reference.objectId = toUtf8(requiredString(object, QStringLiteral("objectId")));
    const QJsonValue featureValue = object.value(QStringLiteral("featureId"));
    if (featureValue.isString()) {
        reference.featureId = toUtf8(featureValue.toString());
    } else if (!featureValue.isNull() && !featureValue.isUndefined()) {
        fail(QStringLiteral("Topology reference featureId must be a UUID or null."));
    }
    const auto subshapeType = datumSubshapeTypeFromName(
        toUtf8(requiredString(object, QStringLiteral("subshapeType"))));
    if (!subshapeType.has_value()) {
        fail(QStringLiteral("Topology reference has an unsupported subshape type."));
    }
    reference.subshapeType = *subshapeType;
    reference.topologySignature = toUtf8(requiredString(object, QStringLiteral("topologySignature")));

    const QJsonValue signatureValue = object.value(QStringLiteral("geometrySignature"));
    if (!signatureValue.isObject()) {
        fail(QStringLiteral("Topology reference geometrySignature is missing or invalid."));
    }
    const QJsonObject signature = signatureValue.toObject();
    reference.geometrySignature.geometryType =
        toUtf8(requiredString(signature, QStringLiteral("geometryType")));
    reference.geometrySignature.measure =
        requiredFiniteNumber(signature.value(QStringLiteral("measure")), QStringLiteral("geometrySignature.measure"));
    reference.geometrySignature.centroidMm =
        decodeVector(signature.value(QStringLiteral("centroidMm")), QStringLiteral("geometrySignature.centroidMm"));
    const QJsonValue directionValue = signature.value(QStringLiteral("direction"));
    if (directionValue.isArray()) {
        reference.geometrySignature.direction =
            decodeVector(directionValue, QStringLiteral("geometrySignature.direction"));
    } else if (!directionValue.isNull() && !directionValue.isUndefined()) {
        fail(QStringLiteral("Topology reference direction must be a three-vector or null."));
    }
    const QJsonValue adjacentValue = signature.value(QStringLiteral("adjacent"));
    if (!adjacentValue.isArray()) {
        fail(QStringLiteral("Topology reference adjacency signature must be an array."));
    }
    for (const QJsonValue& neighborValue : adjacentValue.toArray()) {
        if (!neighborValue.isObject()) {
            fail(QStringLiteral("Topology reference adjacency entry is invalid."));
        }
        const QJsonObject neighborJson = neighborValue.toObject();
        const auto neighborType = datumSubshapeTypeFromName(
            toUtf8(requiredString(neighborJson, QStringLiteral("subshapeType"))));
        if (!neighborType.has_value()) {
            fail(QStringLiteral("Topology reference adjacency entry has an unsupported subshape type."));
        }
        reference.geometrySignature.adjacent.push_back({
            *neighborType,
            toUtf8(requiredString(neighborJson, QStringLiteral("geometryType"))),
            requiredFiniteNumber(neighborJson.value(QStringLiteral("measure")),
                                 QStringLiteral("geometrySignature.adjacent.measure")),
        });
    }
    const QJsonValue parentsValue = object.value(QStringLiteral("parentReferenceIds"));
    if (!parentsValue.isArray()) {
        fail(QStringLiteral("Topology reference parentReferenceIds must be an array."));
    }
    for (const QJsonValue& parentValue : parentsValue.toArray()) {
        if (!parentValue.isString()) {
            fail(QStringLiteral("Topology reference parent id must be a string."));
        }
        reference.parentReferenceIds.push_back(toUtf8(parentValue.toString()));
    }
    const QJsonValue hintValue = object.value(QStringLiteral("transientIndexHint"));
    if (hintValue.isDouble()) {
        const double hint = hintValue.toDouble();
        if (hint < 0.0 || hint > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
            std::floor(hint) != hint) {
            fail(QStringLiteral("Topology reference transientIndexHint is invalid."));
        }
        reference.transientIndexHint = static_cast<std::uint32_t>(hint);
    } else if (!hintValue.isNull() && !hintValue.isUndefined()) {
        fail(QStringLiteral("Topology reference transientIndexHint must be an integer or null."));
    }
    return reference;
}

TopoDS_Shape decodeShape(const QJsonObject& geometry, const QString& objectName)
{
    if (requiredString(geometry, QStringLiteral("format")) != QString::fromLatin1(GeometryFormat)) {
        fail(QStringLiteral("Object '%1' uses an unsupported geometry format.").arg(objectName));
    }
    const QJsonValue occtVersionValue = geometry.value(QStringLiteral("occtFormatVersion"));
    const int occtVersion = occtVersionValue.isDouble() ? occtVersionValue.toInt(-1) : -1;
    if (occtVersion < TopTools_FormatVersion_LOWER || occtVersion > TopTools_FormatVersion_CURRENT) {
        fail(QStringLiteral("Object '%1' uses an unsupported OCCT B-Rep format version.").arg(objectName));
    }
    if (requiredString(geometry, QStringLiteral("encoding")) != QStringLiteral("base64")) {
        fail(QStringLiteral("Object '%1' uses an unsupported geometry encoding.").arg(objectName));
    }
    const QString encodedText = requiredString(geometry, QStringLiteral("data"));
    const QByteArray encoded = encodedText.toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty()) {
        fail(QStringLiteral("Object '%1' has malformed or empty B-Rep data.").arg(objectName));
    }

    TopoDS_Shape shape;
    BRep_Builder builder;
    const std::string brep(decoded.decoded.constData(), static_cast<std::size_t>(decoded.decoded.size()));
    std::istringstream stream(brep, std::ios::in | std::ios::binary);
    try {
        BRepTools::Read(shape, stream, builder);
    } catch (const Standard_Failure& error) {
        const char* message = error.GetMessageString();
        fail(QStringLiteral("OCCT could not read B-Rep for object '%1': %2")
                 .arg(objectName, QString::fromUtf8(message == nullptr ? "unknown OCCT failure" : message)));
    }
    return shape;
}

DatumDocument decodeProject(const QJsonObject& root)
{
    if (requiredString(root, QStringLiteral("format")) != QString::fromLatin1(FormatName)) {
        fail(QStringLiteral("The file is not a DatumLoop project."));
    }
    const QJsonValue schemaValue = root.value(QStringLiteral("schemaVersion"));
    const double schemaNumber = schemaValue.isDouble() ? schemaValue.toDouble() : -1.0;
    if (schemaNumber != 1.0 && schemaNumber != 2.0 && schemaNumber != 3.0 && schemaNumber != 4.0 &&
        schemaNumber != 5.0 && schemaNumber != 6.0 && schemaNumber != 7.0 && schemaNumber != 8.0 &&
        schemaNumber != static_cast<double>(DldProjectStore::SchemaVersion)) {
        fail(QStringLiteral("Unsupported DatumLoop project schema version."));
    }
    const QJsonValue projectValue = root.value(QStringLiteral("project"));
    if (!projectValue.isObject()) {
        fail(QStringLiteral("Project metadata is missing or invalid."));
    }
    const QJsonObject project = projectValue.toObject();
    DatumDocument document(toUtf8(requiredString(project, QStringLiteral("name"))),
                           toUtf8(requiredString(project, QStringLiteral("id"))));

    const QJsonValue objectsValue = root.value(QStringLiteral("objects"));
    if (!objectsValue.isArray()) {
        fail(QStringLiteral("Project objects must be an array."));
    }
    const QJsonArray objects = objectsValue.toArray();
    for (qsizetype index = 0; index < objects.size(); ++index) {
        if (!objects.at(index).isObject()) {
            fail(QStringLiteral("Object entry %1 is not an object.").arg(index));
        }
        const QJsonObject objectJson = objects.at(index).toObject();
        const QString id = requiredString(objectJson, QStringLiteral("id"));
        const QString name = requiredString(objectJson, QStringLiteral("name"));
        for (const QString& reservedArray : {QStringLiteral("featureDefinitions"),
                                             QStringLiteral("references")}) {
            const QJsonValue reservedValue = objectJson.value(reservedArray);
            if (!reservedValue.isArray()) {
                fail(QStringLiteral("Object '%1' field '%2' must be an array.").arg(name, reservedArray));
            }
            if (reservedArray == QStringLiteral("references") && !reservedValue.toArray().isEmpty()) {
                fail(QStringLiteral("Object '%1' contains unsupported '%2' entries; this schema cannot preserve them yet.")
                         .arg(name, reservedArray));
            }
            if (reservedArray == QStringLiteral("featureDefinitions") && schemaNumber < 3.0 &&
                !reservedValue.toArray().isEmpty()) {
                fail(QStringLiteral("Object '%1' contains feature definitions unsupported by this schema version.")
                         .arg(name));
            }
        }

        std::optional<std::string> parentId;
        const QJsonValue parentValue = objectJson.value(QStringLiteral("parentId"));
        if (parentValue.isString()) {
            parentId = toUtf8(parentValue.toString());
        } else if (!parentValue.isNull() && !parentValue.isUndefined()) {
            fail(QStringLiteral("Object '%1' has an invalid parentId.").arg(name));
        }

        const QJsonValue propertiesValue = objectJson.value(QStringLiteral("properties"));
        if (!propertiesValue.isObject()) {
            fail(QStringLiteral("Object '%1' properties must be an object.").arg(name));
        }
        DatumPropertyMap properties;
        const QJsonObject propertiesJson = propertiesValue.toObject();
        for (auto iterator = propertiesJson.begin(); iterator != propertiesJson.end(); ++iterator) {
            if (!iterator.value().isObject()) {
                fail(QStringLiteral("Object '%1' property '%2' must include a type and value.").arg(name, iterator.key()));
            }
            const std::string key = toUtf8(iterator.key());
            if (key.find_first_not_of(" \t\n\r") == std::string::npos) {
                fail(QStringLiteral("Object '%1' has an empty property name.").arg(name));
            }
            properties.emplace(key, decodeProperty(iterator.value().toObject(), iterator.key()));
        }

        const QJsonValue transformValue = objectJson.value(QStringLiteral("transform"));
        if (!transformValue.isObject()) {
            fail(QStringLiteral("Object '%1' transform is missing or invalid.").arg(name));
        }
        const QJsonObject transform = transformValue.toObject();
        const QJsonValue translationValue = transform.value(QStringLiteral("translationMm"));
        if (!translationValue.isArray() || translationValue.toArray().size() != 3) {
            fail(QStringLiteral("Object '%1' translationMm must contain three coordinates.").arg(name));
        }
        const QJsonArray translation = translationValue.toArray();
        const Vector3 position{
            requiredFiniteNumber(translation.at(0), QStringLiteral("transform.translationMm[0]")),
            requiredFiniteNumber(translation.at(1), QStringLiteral("transform.translationMm[1]")),
            requiredFiniteNumber(translation.at(2), QStringLiteral("transform.translationMm[2]")),
        };

        const QJsonValue geometryValue = objectJson.value(QStringLiteral("geometry"));
        if (!geometryValue.isObject()) {
            fail(QStringLiteral("Object '%1' geometry is missing or invalid.").arg(name));
        }
        TopoDS_Shape shape = decodeShape(geometryValue.toObject(), name);
        const ShapeMetrics storedMetrics = OcctKernel::measure(shape);
        const DatumObject& loaded = document.addObject(toUtf8(id), toUtf8(name), std::move(shape), position,
                                                       std::move(parentId), false);
        for (auto& [key, value] : properties) {
            document.setProperty(loaded.id, key, std::move(value));
        }
        if (schemaNumber >= 3.0) {
            std::vector<DatumFeature> features;
            const QJsonArray featureDefinitions = objectJson.value(QStringLiteral("featureDefinitions")).toArray();
            features.reserve(static_cast<std::size_t>(featureDefinitions.size()));
            for (const QJsonValue& featureValue : featureDefinitions) {
                if (!featureValue.isObject()) {
                    fail(QStringLiteral("Object '%1' contains an invalid feature definition.").arg(name));
                }
                features.push_back(decodeFeature(featureValue.toObject(), toUtf8(id), static_cast<int>(schemaNumber)));
            }
            const QJsonValue outputValue = objectJson.value(QStringLiteral("outputFeatureId"));
            std::optional<std::string> outputFeatureId;
            if (outputValue.isString()) {
                outputFeatureId = toUtf8(outputValue.toString());
            } else if (!outputValue.isNull()) {
                fail(QStringLiteral("Object '%1' outputFeatureId must be a feature UUID or null.").arg(name));
            }
            if (!features.empty() || outputFeatureId.has_value()) {
                DatumFeatureGraph graph(toUtf8(id), std::move(features), outputFeatureId);
                if (graph.outputFeatureId().has_value() && graph.outputSucceeded() &&
                    !sameShapeMetrics(storedMetrics, OcctKernel::measure(graph.outputShape()))) {
                    fail(QStringLiteral("Object '%1' cached B-Rep does not agree with its feature graph output.")
                             .arg(name));
                }
                document.setFeatureGraph(toUtf8(id), std::move(graph), false);
            }
        } else if (objectJson.contains(QStringLiteral("outputFeatureId")) &&
                   !objectJson.value(QStringLiteral("outputFeatureId")).isNull()) {
            fail(QStringLiteral("Object '%1' has an outputFeatureId unsupported by this schema version.")
                     .arg(name));
        }
    }

    const QJsonValue referencesValue = root.value(QStringLiteral("topologyReferences"));
    if (schemaNumber == 1.0 && referencesValue.isUndefined()) {
        // Schema 1 had no stored topology references. Generate initial
        // object-level references below and write them on the next v2 save.
    } else {
        if (!referencesValue.isArray()) {
            fail(QStringLiteral("Project topologyReferences must be an array."));
        }
        for (const QJsonValue& referenceValue : referencesValue.toArray()) {
            if (!referenceValue.isObject()) {
                fail(QStringLiteral("Project topology reference entry is invalid."));
            }
            (void)document.addTopologyReference(decodeTopologyReference(referenceValue.toObject()));
        }
    }
    for (const DatumObject& object : document.objects()) {
        const bool hasObjectTopologyReferences = std::any_of(
            document.topologyReferences().begin(), document.topologyReferences().end(),
            [&object](const DatumTopologyReference& reference) {
                return reference.objectId == object.id;
            });
        if (!hasObjectTopologyReferences) {
            for (DatumTopologyReference& reference : DatumTopologyResolver::captureAll(
                     object.id, object.shape, object.featureGraph.outputFeatureId())) {
                (void)document.addTopologyReference(std::move(reference));
            }
        }
    }
    const QJsonValue electronicsValue = root.value(QStringLiteral("electronicsModel"));
    if (schemaNumber >= 5.0) {
        if (!electronicsValue.isObject()) {
            fail(QStringLiteral("Project electronicsModel is missing or invalid."));
        }
        document.setElectronicsModel(decodeElectronicsModel(electronicsValue.toObject()));
    } else if (!electronicsValue.isUndefined()) {
        fail(QStringLiteral("Project contains electronicsModel data unsupported by its schema version."));
    }
    const QJsonValue engineeringValue = root.value(QStringLiteral("engineeringModel"));
    if (schemaNumber >= 4.0) {
        if (!engineeringValue.isObject()) {
            fail(QStringLiteral("Project engineeringModel is missing or invalid."));
        }
        document.setEngineeringModel(decodeEngineeringModel(engineeringValue.toObject(), static_cast<int>(schemaNumber)));
    } else if (!engineeringValue.isUndefined()) {
        fail(QStringLiteral("Project contains engineeringModel data unsupported by its schema version."));
    }
    document.markSaved();
    return document;
}

}  // namespace

DatumDocument DldProjectStore::load(const QString& filePath)
{
    if (filePath.trimmed().isEmpty()) {
        fail(QStringLiteral("A project file path is required."));
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("Could not open project file '%1': %2").arg(filePath, file.errorString()));
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        fail(QStringLiteral("Could not parse project file '%1': %2").arg(filePath, parseError.errorString()));
    }
    try {
        return decodeProject(json.object());
    } catch (const Standard_Failure& error) {
        const char* message = error.GetMessageString();
        fail(QStringLiteral("OCCT rejected project file '%1': %2")
                 .arg(filePath, QString::fromUtf8(message == nullptr ? "unknown OCCT failure" : message)));
    }
}

void DldProjectStore::save(DatumDocument& document, const QString& filePath)
{
    if (filePath.trimmed().isEmpty()) {
        fail(QStringLiteral("A project file path is required."));
    }
    QJsonArray objects;
    for (const DatumObject& object : document.objects()) {
        objects.append(encodeObject(object));
    }
    QJsonArray topologyReferences;
    for (const DatumTopologyReference& reference : document.topologyReferences()) {
        topologyReferences.append(encodeTopologyReference(reference));
    }
    const QJsonObject root{
        {QStringLiteral("format"), QString::fromLatin1(FormatName)},
        {QStringLiteral("schemaVersion"), SchemaVersion},
        {QStringLiteral("project"), QJsonObject{
            {QStringLiteral("id"), fromUtf8(document.projectId())},
            {QStringLiteral("name"), fromUtf8(document.name())},
        }},
        {QStringLiteral("objects"), objects},
        {QStringLiteral("topologyReferences"), topologyReferences},
        {QStringLiteral("engineeringModel"), encodeEngineeringModel(document.engineeringModel())},
        {QStringLiteral("electronicsModel"), encodeElectronicsModel(document.electronicsModel())},
    };
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("Could not create project file '%1': %2").arg(filePath, file.errorString()));
    }
    if (file.write(bytes) != bytes.size()) {
        const QString error = file.errorString();
        file.cancelWriting();
        fail(QStringLiteral("Could not write project file '%1': %2").arg(filePath, error));
    }
    if (!file.commit()) {
        fail(QStringLiteral("Could not commit project file '%1': %2").arg(filePath, file.errorString()));
    }
    document.markSaved();
}

}  // namespace datumloop
