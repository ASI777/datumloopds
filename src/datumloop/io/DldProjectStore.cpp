#include "datumloop/io/DldProjectStore.hpp"
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
    };
}

DatumFeature decodeFeature(const QJsonObject& object, const std::string& expectedOwnerId)
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

QJsonObject encodeMechanism(const DatumMechanism& mechanism)
{
    QJsonArray bodyObjects;
    for (const std::string& objectId : mechanism.bodyObjectIds) {
        bodyObjects.append(fromUtf8(objectId));
    }
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
    return {
        {QStringLiteral("id"), fromUtf8(mechanism.id)},
        {QStringLiteral("name"), fromUtf8(mechanism.name)},
        {QStringLiteral("bodyObjectIds"), bodyObjects},
        {QStringLiteral("axes"), axes},
    };
}

DatumMechanism decodeMechanism(const QJsonObject& object)
{
    DatumMechanism mechanism;
    mechanism.id = toUtf8(requiredString(object, QStringLiteral("id")));
    mechanism.name = toUtf8(requiredString(object, QStringLiteral("name")));
    const QJsonValue bodyObjectsValue = object.value(QStringLiteral("bodyObjectIds"));
    if (!bodyObjectsValue.isArray()) {
        fail(QStringLiteral("Mechanism bodyObjectIds must be an array."));
    }
    for (const QJsonValue& bodyValue : bodyObjectsValue.toArray()) {
        if (!bodyValue.isString()) {
            fail(QStringLiteral("Mechanism bodyObjectIds entries must be strings."));
        }
        mechanism.bodyObjectIds.push_back(toUtf8(bodyValue.toString()));
    }
    const QJsonValue axesValue = object.value(QStringLiteral("axes"));
    if (!axesValue.isArray()) {
        fail(QStringLiteral("Mechanism axes must be an array."));
    }
    for (const QJsonValue& axisValue : axesValue.toArray()) {
        if (!axisValue.isObject()) {
            fail(QStringLiteral("Mechanism axis entry must be an object."));
        }
        const QJsonObject axisObject = axisValue.toObject();
        DatumAxis axis;
        axis.id = toUtf8(requiredString(axisObject, QStringLiteral("id")));
        axis.name = toUtf8(requiredString(axisObject, QStringLiteral("name")));
        const auto axisType = datumAxisTypeFromName(toUtf8(requiredString(axisObject, QStringLiteral("type"))));
        if (!axisType.has_value()) {
            fail(QStringLiteral("Axis type is unsupported."));
        }
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
        if (motorValue.isString()) {
            axis.actuatorMotorId = toUtf8(motorValue.toString());
        } else if (!motorValue.isNull() && !motorValue.isUndefined()) {
            fail(QStringLiteral("Axis actuatorMotorId must be a motor UUID or null."));
        }
        mechanism.axes.push_back(std::move(axis));
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

QJsonObject encodeEngineeringModel(const DatumEngineeringModel& model)
{
    QJsonArray mechanisms;
    for (const DatumMechanism& mechanism : model.mechanisms) {
        mechanisms.append(encodeMechanism(mechanism));
    }
    QJsonArray motors;
    for (const DatumMotor& motor : model.motors) {
        motors.append(encodeMotor(motor));
    }
    return {
        {QStringLiteral("mechanisms"), mechanisms},
        {QStringLiteral("motors"), motors},
    };
}

DatumEngineeringModel decodeEngineeringModel(const QJsonObject& object)
{
    DatumEngineeringModel model;
    const QJsonValue mechanismsValue = object.value(QStringLiteral("mechanisms"));
    const QJsonValue motorsValue = object.value(QStringLiteral("motors"));
    if (!mechanismsValue.isArray() || !motorsValue.isArray()) {
        fail(QStringLiteral("Engineering model mechanisms and motors must be arrays."));
    }
    for (const QJsonValue& mechanismValue : mechanismsValue.toArray()) {
        if (!mechanismValue.isObject()) {
            fail(QStringLiteral("Engineering mechanism entries must be objects."));
        }
        model.mechanisms.push_back(decodeMechanism(mechanismValue.toObject()));
    }
    for (const QJsonValue& motorValue : motorsValue.toArray()) {
        if (!motorValue.isObject()) {
            fail(QStringLiteral("Engineering motor entries must be objects."));
        }
        model.motors.push_back(decodeMotor(motorValue.toObject()));
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
    if (schemaNumber != 1.0 && schemaNumber != 2.0 && schemaNumber != 3.0 &&
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
                features.push_back(decodeFeature(featureValue.toObject(), toUtf8(id)));
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
    const QJsonValue engineeringValue = root.value(QStringLiteral("engineeringModel"));
    if (schemaNumber == static_cast<double>(DldProjectStore::SchemaVersion)) {
        if (!engineeringValue.isObject()) {
            fail(QStringLiteral("Project engineeringModel is missing or invalid."));
        }
        document.setEngineeringModel(decodeEngineeringModel(engineeringValue.toObject()));
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
