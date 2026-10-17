#include "datumloop/ui/DocumentViewModel.hpp"
#include "datumloop/ui/PickingService.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(float left, float right, float tolerance = 1e-5F)
{
    return std::abs(left - right) <= tolerance;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    try {
        datumloop::DatumDocument document;
        const datumloop::DatumObject& part = document.createBox("Test Block", {20.0, 16.0, 12.0});
        const std::string objectId = part.id;
        const std::string secondObjectId = document.createBox("Second Block", {10.0, 8.0, 6.0}).id;
        datumloop::DocumentViewModel model(&document);
        datumloop::PickingService picking(&model, &document);

        require(model.rowCount() == 2, "outliner model did not expose its document objects");
        require(model.selectedRow() == 0, "the first document object was not selected");
        require(model.data(model.index(0), datumloop::DocumentViewModel::ObjectIdRole).toString()
                    == QString::fromStdString(objectId),
                "outliner model did not expose the stable object id");
        require(model.selectedObjectId() == QString::fromStdString(objectId),
                "view model did not expose selection identity");
        require(picking.resolveObjectHit(QString::fromStdString(secondObjectId)),
                "picking service did not resolve a hit by stable object id");
        require(model.selectedObjectId() == QString::fromStdString(secondObjectId) &&
                    model.selectedRow() == 1,
                "resolved pick did not update the corresponding outliner selection");
        require(!picking.resolveObjectHit(QStringLiteral("missing-object")),
                "picking service accepted an unknown object id");
        require(model.selectedObjectId() == QString::fromStdString(secondObjectId),
                "an unresolved hit changed document selection");

        for (const auto& [type, label] : {
                 std::pair{datumloop::DatumSubshapeType::Face, QStringLiteral("Face")},
                 std::pair{datumloop::DatumSubshapeType::Edge, QStringLiteral("Edge")},
                 std::pair{datumloop::DatumSubshapeType::Vertex, QStringLiteral("Vertex")}}) {
            const auto reference = std::find_if(
                document.topologyReferences().begin(), document.topologyReferences().end(),
                [&objectId, type](const auto& candidate) {
                    return candidate.objectId == objectId && candidate.subshapeType == type;
                });
            require(reference != document.topologyReferences().end(),
                    "new object did not receive persistent subshape identities");
            picking.setSelectionMode(label);
            const auto& point = reference->geometrySignature.centroidMm;
            const auto normal = reference->geometrySignature.direction.value_or(
                datumloop::Vector3{0.0, 0.0, 1.0});
            require(picking.resolveSubshapeHit(
                        QString::fromStdString(objectId),
                        QVector3D(static_cast<float>(point[0]), static_cast<float>(point[1]), static_cast<float>(point[2])),
                        QVector3D(static_cast<float>(normal[0]), static_cast<float>(normal[1]), static_cast<float>(normal[2])),
                        0.5),
                    "picking service did not resolve a face, edge, or vertex hit");
            require(model.selectedTopologyReferenceId() == QString::fromStdString(reference->id) &&
                        model.selectedType() == QStringLiteral("OCCT B-Rep %1").arg(label),
                    "subshape picking did not expose its persistent identity and type");
        }
        picking.setSelectionMode(QStringLiteral("Object"));
        require(picking.resolveObjectHit(QString::fromStdString(objectId)) &&
                    model.selectedTopologyReferenceId().isEmpty() &&
                    model.selectedType() == QStringLiteral("OCCT B-Rep Solid"),
                "object selection did not clear the subshape selection");
        require(picking.resolveObjectHit(QString::fromStdString(secondObjectId)),
                "could not restore the second object selection for command tests");

        model.beginMove();
        model.setMoveAxis(QStringLiteral("Z"));
        model.setMoveValue(QStringLiteral("5.5"));
        require(model.moveActive(), "move preview did not enter the active state");
        require(close(model.displayPosition().z(), 5.5F), "move preview did not constrain the Z axis");
        require(close(static_cast<float>(document.object(secondObjectId)->position[2]), 0.0F),
                "move preview mutated the document before confirmation");
        model.cancelMove();
        require(!model.moveActive(), "cancel did not exit the move tool");
        require(close(model.displayPosition().z(), 0.0F), "cancel did not remove the preview transform");
        require(close(static_cast<float>(document.object(secondObjectId)->position[2]), 0.0F),
                "cancel changed the document position");

        model.beginMove();
        model.setMoveAxis(QStringLiteral("Y"));
        model.setMoveValue(QStringLiteral("invalid"));
        model.confirmMove();
        require(model.moveActive(), "invalid move input unexpectedly committed");
        require(model.statusText().contains(QStringLiteral("finite number")),
                "invalid move input did not explain the validation error");
        model.setMoveValue(QStringLiteral("-3.25"));
        require(close(model.displayPosition().y(), -3.25F), "move preview did not constrain the Y axis");
        model.confirmMove();
        require(!model.moveActive(), "confirm did not exit the move tool");
        require(close(static_cast<float>(document.object(secondObjectId)->position[1]), -3.25F),
                "confirm did not commit the move to the document");
        require(close(static_cast<float>(document.object(objectId)->position[1]), 0.0F),
                "stable-id selection moved a different document object");
        require(model.canUndo() && !model.canRedo(), "committed move history state is incorrect");

        model.undo();
        require(close(static_cast<float>(document.object(secondObjectId)->position[1]), 0.0F),
                "view-model undo did not restore the original position");
        model.redo();
        require(close(static_cast<float>(document.object(secondObjectId)->position[1]), -3.25F),
                "view-model redo did not restore the committed position");

        QTemporaryDir temporaryDirectory;
        require(temporaryDirectory.isValid(), "could not create a temporary project directory");
        const QUrl projectUrl = QUrl::fromLocalFile(temporaryDirectory.filePath(QStringLiteral("ui-project.dld")));
        model.saveProjectAs(projectUrl);
        require(!model.dirty(), "successful view-model save did not update dirty state");
        document.setName("Unsaved edit");
        require(model.dirty(), "document edits were not reflected by the view model");
        model.openProject(projectUrl);
        require(model.projectName() == QStringLiteral("Untitled Mechanism"),
                "opening a project did not restore the saved project name");
        require(!model.dirty(), "opening a saved project should reset dirty state");
        require(model.rowCount() == 2 && model.selectedRow() == 0,
                "opening a project did not reset the outliner model and selection");
        require(model.selectedTopologyReferenceId().isEmpty(),
                "opening a project retained the previous subshape selection");
        require(!model.canUndo() && !model.canRedo(),
                "opening a project retained commands from the prior document");
        require(close(static_cast<float>(document.object(secondObjectId)->position[1]), -3.25F),
                "view-model open did not restore saved exact-document placement");

        std::cout << "Native DatumLoop UI model checks passed: object/face/edge/vertex identity selection, constrained preview, cancel, validation, commit, undo/redo, and project save/open.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native DatumLoop UI model check failed: " << error.what() << '\n';
        return 1;
    }
}
