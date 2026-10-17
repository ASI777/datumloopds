#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/io/DldProjectStore.hpp"

#include <TopTools_FormatVersion.hxx>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(double left, double right, double tolerance = 1e-8)
{
    return std::abs(left - right) <= tolerance;
}

QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject readObject(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "could not open test project JSON");
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    require(document.isObject(), "test project JSON root is not an object");
    return document.object();
}

void writeObject(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "could not write test project JSON");
    require(file.write(QJsonDocument(object).toJson()) > 0, "test project JSON write failed");
}

bool loadFails(const QString& path)
{
    try {
        (void)datumloop::DldProjectStore::load(path);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir temporaryDirectory;
        require(temporaryDirectory.isValid(), "could not create temporary directory");
        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("assembly.dld"));

        datumloop::DatumDocument document("Round-trip assembly");
        const std::string projectId = document.projectId();
        const std::string parentId = document.createBox("Base", {40.0, 30.0, 20.0}).id;
        document.setProperty(parentId, "reference", true);
        document.setProperty(parentId, "serial_number", std::int64_t{9007199254740993LL});
        document.setProperty(parentId, "roughness_um", 1.6);
        document.setProperty(parentId, "material", std::string("6061-T6"));
        const std::string childId = uuid().toStdString();
        (void)document.addObject(childId, "Carriage", datumloop::OcctKernel::box({12.0, 8.0, 4.0}),
                                 {12.5, -3.25, 9.0}, parentId);

        require(document.isDirty(), "new or edited document should be dirty before saving");
        datumloop::DldProjectStore::save(document, projectPath);
        require(!document.isDirty(), "successful save should mark document clean");

        QFile manifest(projectPath);
        require(manifest.open(QIODevice::ReadOnly), "saved project could not be opened");
        const QByteArray manifestBytes = manifest.readAll();
        const QJsonObject root = QJsonDocument::fromJson(manifestBytes).object();
        require(root.value(QStringLiteral("schemaVersion")).toInt() == 1,
                "saved project did not include the version-one schema");
        const QJsonArray savedObjects = root.value(QStringLiteral("objects")).toArray();
        require(savedObjects.at(0).toObject().value(QStringLiteral("featureDefinitions")).toArray().isEmpty() &&
                    savedObjects.at(0).toObject().value(QStringLiteral("references")).toArray().isEmpty(),
                "version-one reserved feature/reference arrays should be empty");
        require(savedObjects.at(0).toObject().value(QStringLiteral("geometry")).toObject()
                    .value(QStringLiteral("occtFormatVersion")).toInt() == TopTools_FormatVersion_CURRENT,
                "saved B-Rep omitted its OCCT stream version");
        require(!manifestBytes.contains("displayMesh") && !manifestBytes.contains("triangles"),
                "project persistence should not store renderer tessellation");

        datumloop::DatumDocument loaded = datumloop::DldProjectStore::load(projectPath);
        require(!loaded.isDirty(), "loaded project should start clean");
        require(loaded.projectId() == projectId, "project UUID did not survive round-trip");
        require(loaded.name() == "Round-trip assembly", "project name did not survive round-trip");
        require(loaded.objects().size() == 2, "project object count did not survive round-trip");
        const datumloop::DatumObject* loadedParent = loaded.object(parentId);
        const datumloop::DatumObject* loadedChild = loaded.object(childId);
        require(loadedParent != nullptr && loadedChild != nullptr,
                "stable object UUIDs did not survive round-trip");
        require(!loadedParent->parentId.has_value(), "root object acquired a parent during round-trip");
        require(loadedChild->parentId == parentId, "object hierarchy did not survive round-trip");
        require(close(loadedChild->position[0], 12.5) && close(loadedChild->position[1], -3.25) &&
                    close(loadedChild->position[2], 9.0),
                "object translation did not survive round-trip");
        require(close(loadedChild->metrics.volume, 384.0) && loadedChild->metrics.valid,
                "exact OCCT B-Rep geometry or its recomputed metrics did not survive round-trip");
        require(datumloop::OcctKernel::tessellate(loadedChild->shape).triangleCount() > 0,
                "display tessellation could not be regenerated after load");
        require(std::get<bool>(loadedParent->properties.at("reference")),
                "boolean property did not survive round-trip");
        require(std::get<std::int64_t>(loadedParent->properties.at("serial_number")) == 9007199254740993LL,
                "64-bit integer property lost precision during round-trip");
        require(close(std::get<double>(loadedParent->properties.at("roughness_um")), 1.6),
                "floating-point property did not survive round-trip");
        require(std::get<std::string>(loadedParent->properties.at("material")) == "6061-T6",
                "string property did not survive round-trip");

        loaded.setName("Edited assembly");
        require(loaded.isDirty(), "document metadata edits should mark the document dirty");
        loaded.markSaved();
        require(!loaded.isDirty(), "markSaved should clear dirty state");
        require(loaded.setPosition(childId, {13.0, -3.25, 9.0}) && loaded.isDirty(),
                "transform edits should mark the document dirty");

        QJsonObject broken = root;
        broken.insert(QStringLiteral("schemaVersion"), 99);
        const QString unsupportedPath = temporaryDirectory.filePath(QStringLiteral("unsupported.dld"));
        writeObject(unsupportedPath, broken);
        require(loadFails(unsupportedPath), "unsupported project schema was accepted");

        QJsonArray objects = root.value(QStringLiteral("objects")).toArray();
        QJsonObject firstObject = objects.at(0).toObject();
        QJsonObject geometry = firstObject.value(QStringLiteral("geometry")).toObject();
        const QString unsupportedBrepPath = temporaryDirectory.filePath(QStringLiteral("unsupported-brep.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        geometry = firstObject.value(QStringLiteral("geometry")).toObject();
        geometry.insert(QStringLiteral("occtFormatVersion"), 99);
        firstObject.insert(QStringLiteral("geometry"), geometry);
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(unsupportedBrepPath, broken);
        require(loadFails(unsupportedBrepPath), "unsupported OCCT B-Rep format version was accepted");

        const QString unsupportedFeaturePath = temporaryDirectory.filePath(QStringLiteral("unsupported-feature.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        firstObject.insert(QStringLiteral("featureDefinitions"), QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("primitive.box")},
        }});
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(unsupportedFeaturePath, broken);
        require(loadFails(unsupportedFeaturePath), "unsupported feature definitions were silently discarded");

        const QString unsupportedReferencePath = temporaryDirectory.filePath(QStringLiteral("unsupported-reference.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        firstObject.insert(QStringLiteral("references"), QJsonArray{QStringLiteral("Face1")});
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(unsupportedReferencePath, broken);
        require(loadFails(unsupportedReferencePath), "unsupported topology references were silently discarded");

        const QString badBase64Path = temporaryDirectory.filePath(QStringLiteral("bad-base64.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        geometry = firstObject.value(QStringLiteral("geometry")).toObject();
        geometry.insert(QStringLiteral("data"), QStringLiteral("not valid base64!"));
        firstObject.insert(QStringLiteral("geometry"), geometry);
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(badBase64Path, broken);
        require(loadFails(badBase64Path), "malformed base64 B-Rep data was accepted");

        const QString duplicateIdPath = temporaryDirectory.filePath(QStringLiteral("duplicate-id.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        QJsonObject secondObject = objects.at(1).toObject();
        secondObject.insert(QStringLiteral("id"), firstObject.value(QStringLiteral("id")));
        objects.replace(1, secondObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(duplicateIdPath, broken);
        require(loadFails(duplicateIdPath), "duplicate object UUID was accepted");

        const QString projectIdCollisionPath = temporaryDirectory.filePath(QStringLiteral("project-id-collision.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        firstObject = objects.at(0).toObject();
        firstObject.insert(QStringLiteral("id"), broken.value(QStringLiteral("project")).toObject()
                                                     .value(QStringLiteral("id")));
        objects.replace(0, firstObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(projectIdCollisionPath, broken);
        require(loadFails(projectIdCollisionPath), "object UUID collision with the project UUID was accepted");

        const QString missingParentPath = temporaryDirectory.filePath(QStringLiteral("missing-parent.dld"));
        broken = root;
        objects = broken.value(QStringLiteral("objects")).toArray();
        secondObject = objects.at(1).toObject();
        secondObject.insert(QStringLiteral("parentId"), uuid());
        objects.replace(1, secondObject);
        broken.insert(QStringLiteral("objects"), objects);
        writeObject(missingParentPath, broken);
        require(loadFails(missingParentPath), "object with a missing parent was accepted");

        loaded.setName("Save failure should stay dirty");
        bool failedSaveWasRejected = false;
        try {
            datumloop::DldProjectStore::save(loaded,
                temporaryDirectory.filePath(QStringLiteral("missing-folder/failure.dld")));
        } catch (const std::exception&) {
            failedSaveWasRejected = true;
        }
        require(failedSaveWasRejected && loaded.isDirty(),
                "failed save should be rejected without clearing document dirty state");

        std::cout << "Native .dld checks passed: exact B-Rep round-trip, identity, hierarchy, transforms, typed properties, dirty tracking, regenerated display mesh, and malformed-file rejection.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native .dld check failed: " << error.what() << '\n';
        return 1;
    }
}
