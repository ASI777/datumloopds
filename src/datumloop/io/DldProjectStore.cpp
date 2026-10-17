#include "datumloop/io/DldProjectStore.hpp"

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
        {QStringLiteral("featureDefinitions"), QJsonArray{}},
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
    if (schemaNumber != 1.0 && schemaNumber != static_cast<double>(DldProjectStore::SchemaVersion)) {
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
            if (!reservedValue.toArray().isEmpty()) {
                fail(QStringLiteral("Object '%1' contains unsupported '%2' entries; this schema cannot preserve them yet.")
                         .arg(name, reservedArray));
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
        const DatumObject& loaded = document.addObject(toUtf8(id), toUtf8(name), std::move(shape), position,
                                                       std::move(parentId));
        for (auto& [key, value] : properties) {
            document.setProperty(loaded.id, key, std::move(value));
        }
    }

    const QJsonValue referencesValue = root.value(QStringLiteral("topologyReferences"));
    if (schemaNumber == 1.0 && referencesValue.isUndefined()) {
        // Schema 1 had no persistent topology references. Treat this as an
        // empty collection and upgrade in memory; the next save writes v2.
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
