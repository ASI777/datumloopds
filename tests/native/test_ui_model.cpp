#include "datumloop/ui/DocumentViewModel.hpp"
#include "datumloop/ui/AnalysisResultDisplay.hpp"
#include "datumloop/ui/PickingService.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/numerics/AnalysisMesh.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

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

datumloop::StructuralAnalysisRun makeVisualizationRun()
{
    using namespace datumloop;
    StructuralAnalysisRun run;
    run.converged = true;
    run.provenance.solverRunId = createDatumUuidV4();
    run.provenance.mesh.revisionId = createDatumUuidV4();
    run.provenance.mesh.nodeCount = 8;
    run.provenance.mesh.elementCount = 1;

    AnalysisMeshSnapshot snapshot;
    snapshot.meshRevisionId = run.provenance.mesh.revisionId;
    snapshot.nodePositionsM = {
        {0.0, 0.0, 0.0}, {0.1, 0.0, 0.0}, {0.1, 0.1, 0.0}, {0.0, 0.1, 0.0},
        {0.0, 0.0, 0.1}, {0.1, 0.0, 0.1}, {0.1, 0.1, 0.1}, {0.0, 0.1, 0.1},
    };
    snapshot.hexahedra = {{{0, 1, 2, 3, 4, 5, 6, 7}}};
    run.meshSnapshot = snapshot;

    FieldDataset displacement;
    displacement.id = createDatumUuidV4();
    displacement.fieldType = FieldType::Displacement;
    displacement.location = FieldLocation::Nodes;
    displacement.components = {"x", "y", "z"};
    displacement.units = "m";
    displacement.meshRevisionId = run.provenance.mesh.revisionId;
    displacement.solverRunId = run.provenance.solverRunId;
    for (int node = 0; node < 8; ++node) {
        displacement.values.insert(displacement.values.end(), {0.001, 0.0, 0.0});
    }
    run.fields.push_back(displacement);

    FieldDataset strain;
    strain.id = createDatumUuidV4();
    strain.fieldType = FieldType::StrainTensor;
    strain.location = FieldLocation::Elements;
    strain.components = {"xx", "yy", "zz", "xy", "yz", "zx"};
    strain.units = "1";
    strain.meshRevisionId = run.provenance.mesh.revisionId;
    strain.solverRunId = run.provenance.solverRunId;
    strain.values = {0.001, -0.0005, -0.0005, 0.0, 0.0, 0.0};
    run.fields.push_back(strain);

    FieldDataset vonMises;
    vonMises.id = createDatumUuidV4();
    vonMises.fieldType = FieldType::VonMisesStress;
    vonMises.location = FieldLocation::Elements;
    vonMises.components = {"von_mises"};
    vonMises.units = "Pa";
    vonMises.meshRevisionId = run.provenance.mesh.revisionId;
    vonMises.solverRunId = run.provenance.solverRunId;
    vonMises.values = {200e6};
    run.fields.push_back(vonMises);
    return run;
}

void addDisplayableStudy(datumloop::DatumDocument& document,
                         const datumloop::AnalysisMesh& mesh,
                         const std::string& cadObjectId)
{
    using namespace datumloop;
    MaterialDefinition material;
    material.id = createDatumUuidV4();
    material.name = "Viewport test material";
    DatumElectronicsModel electronics = document.electronicsModel();
    electronics.materials.push_back(material);
    document.setElectronicsModel(std::move(electronics));

    FixedDisplacement fixed;
    fixed.id = createDatumUuidV4();
    fixed.name = "Test support";
    fixed.cadObjectId = cadObjectId;
    fixed.topologyReferenceId = mesh.topologyReferenceIds.front();

    StructuralAnalysisStudy study;
    study.id = createDatumUuidV4();
    study.name = "Viewport field test";
    study.cadObjectId = cadObjectId;
    study.materialDefinitionId = material.id;
    study.equationDefinitionId = "test.linear-elasticity";
    study.boundaryConditions.push_back(fixed);

    StructuralAnalysisRun run;
    run.converged = true;
    run.iterations = 1;
    run.provenance.solverRunId = createDatumUuidV4();
    run.provenance.timestampUtc = "2026-10-17T00:00:00Z";
    run.provenance.sourceProjectId = document.projectId();
    run.provenance.sourceDocumentRevision = document.revision();
    run.provenance.cadObjectId = cadObjectId;
    run.provenance.materialDefinitionId = material.id;
    run.provenance.materialRevision = "test-material-fingerprint";
    run.provenance.equationDefinitionId = study.equationDefinitionId;
    run.provenance.solverId = "test.solver";
    run.provenance.solverVersion = "1.0";
    run.provenance.mesh = mesh.provenance;
    run.provenance.topologyReferenceIds = mesh.topologyReferenceIds;
    run.provenance.boundaryConditionIds = {fixed.id};
    run.provenance.absoluteTolerance = 1e-10;
    run.provenance.relativeTolerance = 1e-8;
    run.provenance.maximumIterations = 20;
    run.meshSnapshot.emplace();
    run.meshSnapshot->meshRevisionId = mesh.provenance.revisionId;
    for (const AnalysisNode& node : mesh.nodes) {
        run.meshSnapshot->nodePositionsM.push_back(node.positionM);
    }
    for (const AnalysisHexElement& element : mesh.elements) {
        run.meshSnapshot->hexahedra.push_back(element.nodeIndices);
    }

    FieldDataset displacement;
    displacement.id = createDatumUuidV4();
    displacement.fieldType = FieldType::Displacement;
    displacement.location = FieldLocation::Nodes;
    displacement.components = {"x", "y", "z"};
    displacement.units = "m";
    displacement.meshRevisionId = mesh.provenance.revisionId;
    displacement.solverRunId = run.provenance.solverRunId;
    for (std::size_t node = 0; node < mesh.nodes.size(); ++node) {
        displacement.values.insert(displacement.values.end(), {0.001, 0.0, 0.0});
    }
    run.fields.push_back(displacement);

    const auto elementField = [&](FieldType type, std::vector<std::string> components,
                                  std::string units, std::size_t componentCount, double value) {
        FieldDataset field;
        field.id = createDatumUuidV4();
        field.fieldType = type;
        field.location = FieldLocation::Elements;
        field.components = std::move(components);
        field.units = std::move(units);
        field.meshRevisionId = mesh.provenance.revisionId;
        field.solverRunId = run.provenance.solverRunId;
        field.values.resize(mesh.elements.size() * componentCount, value);
        return field;
    };
    run.fields.push_back(elementField(FieldType::StrainTensor,
        {"xx", "yy", "zz", "xy", "yz", "zx"}, "1", 6, 0.0));
    run.fields.push_back(elementField(FieldType::StressTensor,
        {"xx", "yy", "zz", "xy", "yz", "zx"}, "Pa", 6, 0.0));
    run.fields.push_back(elementField(FieldType::VonMisesStress,
        {"von_mises"}, "Pa", 1, 2e6));
    run.maximumVonMisesStressPa = 2e6;
    study.runs.push_back(std::move(run));

    DatumEngineeringModel engineering = document.engineeringModel();
    engineering.structuralStudies.push_back(std::move(study));
    document.setEngineeringModel(std::move(engineering));
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    try {
        const datumloop::StructuralAnalysisRun displayRun = makeVisualizationRun();
        const auto displacementDisplay = datumloop::buildAnalysisResultDisplay(
            displayRun, datumloop::AnalysisResultField::DisplacementMagnitude, 0.0);
        require(displacementDisplay.mesh.triangleCount() == 12 &&
                    displacementDisplay.mesh.hasVertexColors && displacementDisplay.units == "mm" &&
                    close(static_cast<float>(displacementDisplay.minimum), 1.0F),
                "displacement result display did not produce a colored exterior with mm legend values");
        const auto stressDisplay = datumloop::buildAnalysisResultDisplay(
            displayRun, datumloop::AnalysisResultField::VonMisesStress, 0.0);
        require(stressDisplay.units == "MPa" && close(static_cast<float>(stressDisplay.minimum), 200.0F) &&
                    close(static_cast<float>(stressDisplay.maximum), 200.0F),
                "von Mises result display did not convert Pa to MPa");
        const auto strainDisplay = datumloop::buildAnalysisResultDisplay(
            displayRun, datumloop::AnalysisResultField::EquivalentStrain, 0.0);
        require(strainDisplay.units == "µε" && close(static_cast<float>(strainDisplay.minimum), 1000.0F),
                "strain result display did not derive equivalent microstrain");
        const auto deformedDisplay = datumloop::buildAnalysisResultDisplay(
            displayRun, datumloop::AnalysisResultField::VonMisesStress, 10.0);
        require(close(static_cast<float>(deformedDisplay.mesh.minimum[0]), 10.0F),
                "deformation display did not apply the explicit scale to the persisted displacement field");
        bool invalidDisplayRejected = false;
        try {
            (void)datumloop::buildAnalysisResultDisplay(
                displayRun, datumloop::AnalysisResultField::VonMisesStress, -1.0);
        } catch (const std::invalid_argument&) {
            invalidDisplayRejected = true;
        }
        require(invalidDisplayRejected, "analysis result display accepted a negative deformation scale");

        datumloop::DatumDocument resultDocument;
        const std::string resultObjectId =
            resultDocument.createBox("Result View Block", {20.0, 16.0, 12.0}).id;
        datumloop::AnalysisMeshSettings resultMeshSettings;
        resultMeshSettings.maximumElementSizeM = 0.02;
        const datumloop::AnalysisMesh resultMesh = datumloop::OcctPrismAnalysisMesher::generate(
            resultDocument, resultObjectId, resultMeshSettings);
        addDisplayableStudy(resultDocument, resultMesh, resultObjectId);
        datumloop::DocumentViewModel resultModel(&resultDocument);
        require(resultModel.analysisResultAvailable() &&
                    resultModel.analysisDisplayMode() == QStringLiteral("Geometry"),
                "view model did not find a current CAD-linked field result");
        resultModel.setAnalysisDisplayMode(QStringLiteral("von Mises Stress"));
        require(resultModel.analysisDisplayMesh().triangleCount() == 48 &&
                    resultModel.analysisLegendTitle() == QStringLiteral("von Mises stress") &&
                    resultModel.analysisLegendMaximum().contains(QStringLiteral("MPa")) &&
                    resultModel.analysisResultProvenance().contains(QStringLiteral("source revision")),
                "view model did not expose the colored CAD-derived stress field, legend, and provenance");
        resultModel.setAnalysisDeformationScale(10.0);
        require(close(static_cast<float>(resultModel.analysisDisplayMesh().minimum[0]), 0.0F),
                "view model did not apply and retain the displayed deformation scale");

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
