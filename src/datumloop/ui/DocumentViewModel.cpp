#include "datumloop/ui/DocumentViewModel.hpp"

#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/numerics/AnalysisMesh.hpp"

#include <QLocale>

#include <algorithm>
#include <cmath>
#include <memory>

namespace datumloop {
namespace {

QString number(double value)
{
    return QString::number(value, 'f', 3);
}

QString vectorText(const Vector3& value)
{
    return QStringLiteral("%1, %2, %3 mm")
        .arg(number(value[0]), number(value[1]), number(value[2]));
}

QString exceptionText(const std::exception& error)
{
    return QString::fromUtf8(error.what());
}

}  // namespace

DocumentViewModel::DocumentViewModel(DatumDocument* document, QObject* parent)
    : QAbstractListModel(parent), document_(document)
{
    if (document_ != nullptr && !document_->objects().empty()) {
        selectedRow_ = 0;
    }
    rebuildAnalysisDisplay();
}

int DocumentViewModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || document_ == nullptr) {
        return 0;
    }
    return static_cast<int>(document_->objects().size());
}

QString DocumentViewModel::selectedObjectId() const
{
    const DatumObject* object = selectedObject();
    return object == nullptr ? QString{} : QString::fromStdString(object->id);
}

QString DocumentViewModel::displayObjectId() const
{
    const DatumObject* object = selectedObject();
    if (object == nullptr && document_ != nullptr && !document_->objects().empty()) {
        object = &document_->objects().front();
    }
    return object == nullptr ? QString{} : QString::fromStdString(object->id);
}

QString DocumentViewModel::projectName() const
{
    return document_ == nullptr ? QStringLiteral("Untitled Mechanism")
                                : QString::fromUtf8(document_->name().data(),
                                                    static_cast<qsizetype>(document_->name().size()));
}

QVariant DocumentViewModel::data(const QModelIndex& modelIndex, int role) const
{
    if (document_ == nullptr || !modelIndex.isValid() || modelIndex.row() < 0 ||
        modelIndex.row() >= static_cast<int>(document_->objects().size())) {
        return {};
    }
    const DatumObject& object = document_->objects()[static_cast<std::size_t>(modelIndex.row())];
    switch (role) {
    case Qt::DisplayRole:
    case ObjectNameRole:
        return QString::fromStdString(object.name);
    case ObjectIdRole:
        return QString::fromStdString(object.id);
    case ObjectTypeRole:
        return QStringLiteral("OCCT B-Rep Solid");
    case IsSelectedRole:
        return modelIndex.row() == selectedRow_;
    case ObjectPositionRole:
        return modelIndex.row() == selectedRow_
            ? QVariant::fromValue(displayPosition())
            : QVariant::fromValue(QVector3D(static_cast<float>(object.position[0]),
                                            static_cast<float>(object.position[1]),
                                            static_cast<float>(object.position[2])));
    case ObjectVolumeRole:
        return object.metrics.volume;
    default:
        return {};
    }
}

QHash<int, QByteArray> DocumentViewModel::roleNames() const
{
    return {
        {ObjectIdRole, "objectId"},
        {ObjectNameRole, "name"},
        {ObjectTypeRole, "objectType"},
        {IsSelectedRole, "isSelected"},
        {ObjectPositionRole, "objectPosition"},
        {ObjectVolumeRole, "objectVolume"},
    };
}

QString DocumentViewModel::selectedName() const
{
    const DatumObject* object = selectedObject();
    return object == nullptr ? QStringLiteral("No selection") : QString::fromStdString(object->name);
}

QString DocumentViewModel::selectedType() const
{
    if (selectedObject() == nullptr) {
        return QStringLiteral("—");
    }
    const DatumTopologyReference* reference = selectedTopologyReference();
    if (reference == nullptr) {
        return QStringLiteral("OCCT B-Rep Solid");
    }
    const QString type = QString::fromStdString(datumSubshapeTypeName(reference->subshapeType));
    return QStringLiteral("OCCT B-Rep %1")
        .arg(type.left(1).toUpper() + type.mid(1));
}

QString DocumentViewModel::selectedPositionText() const
{
    const QVector3D position = displayPosition();
    return vectorText({position.x(), position.y(), position.z()});
}

QString DocumentViewModel::selectedBoundsText() const
{
    const DatumObject* object = selectedObject();
    if (object == nullptr) {
        return QStringLiteral("—");
    }
    const ShapeMetrics& metrics = object->metrics;
    return vectorText({metrics.maximum[0] - metrics.minimum[0],
                       metrics.maximum[1] - metrics.minimum[1],
                       metrics.maximum[2] - metrics.minimum[2]});
}

QString DocumentViewModel::selectedVolumeText() const
{
    const DatumObject* object = selectedObject();
    return object == nullptr ? QStringLiteral("—")
                             : QStringLiteral("%1 mm³").arg(number(object->metrics.volume));
}

QVector3D DocumentViewModel::displayPosition() const
{
    const DatumObject* object = selectedObject();
    if (object == nullptr && document_ != nullptr && !document_->objects().empty()) {
        object = &document_->objects().front();
    }
    if (object == nullptr) {
        return {};
    }
    const Vector3 delta = moveActive_ ? previewDelta_ : Vector3{0.0, 0.0, 0.0};
    return QVector3D(static_cast<float>(object->position[0] + delta[0]),
                     static_cast<float>(object->position[1] + delta[1]),
                     static_cast<float>(object->position[2] + delta[2]));
}

void DocumentViewModel::selectRow(int row)
{
    if (document_ == nullptr) {
        return;
    }
    if (row < 0 || row >= static_cast<int>(document_->objects().size())) {
        row = -1;
    }
    const bool rowChanged = selectedRow_ != row;
    const bool topologySelectionChanged = !selectedTopologyReferenceId_.isEmpty();
    if (!rowChanged && !topologySelectionChanged) {
        return;
    }
    const int previous = selectedRow_;
    selectedRow_ = row;
    selectedTopologyReferenceId_.clear();
    if (rowChanged && previous >= 0) {
        emit dataChanged(index(previous), index(previous), {IsSelectedRole});
    }
    if (rowChanged && selectedRow_ >= 0) {
        emit dataChanged(index(selectedRow_), index(selectedRow_), {IsSelectedRole});
    }
    emit selectionChanged();
    rebuildAnalysisDisplay();
    if (rowChanged) {
        emit displayPositionChanged();
    }
    if (rowChanged && moveActive_) {
        cancelMove();
    }
}

bool DocumentViewModel::selectObjectId(const QString& objectId)
{
    if (document_ == nullptr || objectId.isEmpty()) {
        return false;
    }
    const std::string stableId = objectId.toStdString();
    const auto& objects = document_->objects();
    const auto found = std::find_if(objects.begin(), objects.end(), [&](const DatumObject& object) {
        return object.id == stableId;
    });
    if (found == objects.end()) {
        return false;
    }
    selectRow(static_cast<int>(std::distance(objects.begin(), found)));
    return true;
}

bool DocumentViewModel::selectTopologyReference(const QString& objectId, const QString& referenceId)
{
    if (document_ == nullptr || objectId.isEmpty() || referenceId.isEmpty()) {
        return false;
    }
    const std::string stableObjectId = objectId.toStdString();
    const std::string stableReferenceId = referenceId.toStdString();
    const auto objectIterator = std::find_if(
        document_->objects().begin(), document_->objects().end(), [&stableObjectId](const DatumObject& object) {
            return object.id == stableObjectId;
        });
    const auto referenceIterator = std::find_if(
        document_->topologyReferences().begin(), document_->topologyReferences().end(),
        [&stableObjectId, &stableReferenceId](const DatumTopologyReference& reference) {
            return reference.id == stableReferenceId && reference.objectId == stableObjectId;
        });
    if (objectIterator == document_->objects().end() ||
        referenceIterator == document_->topologyReferences().end()) {
        return false;
    }

    const int row = static_cast<int>(std::distance(document_->objects().begin(), objectIterator));
    if (selectedRow_ != row) {
        selectRow(row);
    }
    if (selectedTopologyReferenceId_ != referenceId) {
        selectedTopologyReferenceId_ = referenceId;
        emit selectionChanged();
        rebuildAnalysisDisplay();
    }
    const QString type = QString::fromStdString(datumSubshapeTypeName(referenceIterator->subshapeType));
    statusText_ = QStringLiteral("Selected %1  |  %2")
        .arg(type.left(1).toUpper() + type.mid(1), QString::fromStdString(objectIterator->name));
    emit statusTextChanged();
    return true;
}

void DocumentViewModel::beginMove()
{
    if (selectedObject() == nullptr) {
        statusText_ = QStringLiteral("Select a component to move.");
        emit statusTextChanged();
        return;
    }
    moveActive_ = true;
    moveAxis_ = QStringLiteral("X");
    moveValue_ = QStringLiteral("0");
    previewDelta_ = {0.0, 0.0, 0.0};
    emit moveStateChanged();
    emit displayPositionChanged();
    updateStatus();
}

void DocumentViewModel::setMoveAxis(const QString& axis)
{
    if (!moveActive_) {
        return;
    }
    const QString normalized = axis.trimmed().toUpper();
    if (normalized != QStringLiteral("X") && normalized != QStringLiteral("Y") &&
        normalized != QStringLiteral("Z")) {
        return;
    }
    moveAxis_ = normalized;
    setMoveValue(moveValue_);
    emit moveStateChanged();
    updateStatus();
}

void DocumentViewModel::setMoveValue(const QString& value)
{
    if (!moveActive_) {
        return;
    }
    moveValue_ = value;
    previewDelta_ = {0.0, 0.0, 0.0};
    bool parsed = false;
    const double distance = QLocale::c().toDouble(value.trimmed(), &parsed);
    if (parsed && std::isfinite(distance)) {
        const int coordinate = moveAxis_ == QStringLiteral("X") ? 0
                             : moveAxis_ == QStringLiteral("Y") ? 1 : 2;
        previewDelta_[static_cast<std::size_t>(coordinate)] = distance;
    }
    emit moveStateChanged();
    emit displayPositionChanged();
    if (selectedRow_ >= 0) {
        emit dataChanged(index(selectedRow_), index(selectedRow_), {ObjectPositionRole});
    }
    updateStatus();
}

void DocumentViewModel::confirmMove()
{
    if (!moveActive_ || document_ == nullptr) {
        return;
    }
    bool parsed = false;
    const double distance = QLocale::c().toDouble(moveValue_.trimmed(), &parsed);
    if (!parsed || !std::isfinite(distance)) {
        statusText_ = QStringLiteral("Move distance must be a finite number in millimetres.");
        emit statusTextChanged();
        return;
    }
    const DatumObject* object = selectedObject();
    if (object == nullptr) {
        cancelMove();
        return;
    }
    const std::string id = object->id;
    const Vector3 before = object->position;
    const Vector3 after = {before[0] + previewDelta_[0],
                           before[1] + previewDelta_[1],
                           before[2] + previewDelta_[2]};
    history_.execute(*document_, std::make_unique<MoveObjectCommand>(id, before, after));
    moveActive_ = false;
    previewDelta_ = {0.0, 0.0, 0.0};
    emit moveStateChanged();
    notifyObjectRows();
    emit selectionChanged();
    emit displayPositionChanged();
    emit historyChanged();
    emit projectChanged();
    statusText_ = QStringLiteral("Move committed  |  %1 %2 mm").arg(moveAxis_, number(distance));
    emit statusTextChanged();
}

void DocumentViewModel::cancelMove()
{
    if (!moveActive_) {
        return;
    }
    moveActive_ = false;
    previewDelta_ = {0.0, 0.0, 0.0};
    emit moveStateChanged();
    emit displayPositionChanged();
    if (selectedRow_ >= 0) {
        emit dataChanged(index(selectedRow_), index(selectedRow_), {ObjectPositionRole});
    }
    statusText_ = QStringLiteral("Move cancelled  |  document unchanged");
    emit statusTextChanged();
}

void DocumentViewModel::undo()
{
    if (moveActive_) {
        cancelMove();
    }
    if (document_ != nullptr && history_.undo(*document_)) {
        notifyObjectRows();
        emit selectionChanged();
        emit displayPositionChanged();
        emit historyChanged();
        emit projectChanged();
        statusText_ = QStringLiteral("Undo  |  Move Object");
        emit statusTextChanged();
    }
}

void DocumentViewModel::redo()
{
    if (moveActive_) {
        cancelMove();
    }
    if (document_ != nullptr && history_.redo(*document_)) {
        notifyObjectRows();
        emit selectionChanged();
        emit displayPositionChanged();
        emit historyChanged();
        emit projectChanged();
        statusText_ = QStringLiteral("Redo  |  Move Object");
        emit statusTextChanged();
    }
}

void DocumentViewModel::frameSelected()
{
    const DatumObject* object = selectedObject();
    if (object == nullptr) {
        return;
    }
    emit frameSelectedRequested(displayPosition());
    statusText_ = QStringLiteral("Framed  |  %1").arg(QString::fromStdString(object->name));
    emit statusTextChanged();
}

void DocumentViewModel::saveProject()
{
    if (document_ == nullptr) {
        return;
    }
    if (projectPath_.isEmpty()) {
        emit saveAsRequested();
        return;
    }
    saveProjectAs(QUrl::fromLocalFile(projectPath_));
}

void DocumentViewModel::saveProjectAs(const QUrl& fileUrl)
{
    if (document_ == nullptr) {
        return;
    }
    if (!fileUrl.isLocalFile() || fileUrl.toLocalFile().trimmed().isEmpty()) {
        statusText_ = QStringLiteral("Choose a local .dld project path.");
        emit statusTextChanged();
        return;
    }
    const QString path = fileUrl.toLocalFile();
    try {
        DldProjectStore::save(*document_, path);
        projectPath_ = path;
        emit projectChanged();
        statusText_ = QStringLiteral("Saved  |  %1").arg(path);
    } catch (const std::exception& error) {
        statusText_ = QStringLiteral("Save failed  |  %1").arg(exceptionText(error));
    }
    emit statusTextChanged();
}

void DocumentViewModel::openProject(const QUrl& fileUrl)
{
    if (document_ == nullptr) {
        return;
    }
    if (!fileUrl.isLocalFile() || fileUrl.toLocalFile().trimmed().isEmpty()) {
        statusText_ = QStringLiteral("Choose a local .dld project file.");
        emit statusTextChanged();
        return;
    }
    const QString path = fileUrl.toLocalFile();
    try {
        DatumDocument replacement = DldProjectStore::load(path);
        if (moveActive_) {
            cancelMove();
        }
        beginResetModel();
        document_->replaceWith(std::move(replacement));
        selectedRow_ = document_->objects().empty() ? -1 : 0;
        selectedTopologyReferenceId_.clear();
        history_.clear();
        endResetModel();
        projectPath_ = path;
        emit projectChanged();
        emit selectionChanged();
        emit displayPositionChanged();
        emit historyChanged();
        emit documentContentChanged();
        rebuildAnalysisDisplay();
        statusText_ = QStringLiteral("Opened  |  %1").arg(path);
    } catch (const std::exception& error) {
        statusText_ = QStringLiteral("Open failed  |  %1").arg(exceptionText(error));
    }
    emit statusTextChanged();
}

void DocumentViewModel::reportInteractionStatus(const QString& message)
{
    if (!message.isEmpty() && statusText_ != message) {
        statusText_ = message;
        emit statusTextChanged();
    }
}

void DocumentViewModel::setAnalysisDisplayMode(const QString& mode)
{
    const QString normalized = mode.trimmed();
    if (normalized != QStringLiteral("Geometry") && normalized != QStringLiteral("Displacement") &&
        normalized != QStringLiteral("von Mises Stress") && normalized != QStringLiteral("Strain")) {
        return;
    }
    if (normalized != QStringLiteral("Geometry") && !analysisResultAvailable_) {
        statusText_ = analysisResultStatus_;
        emit statusTextChanged();
        return;
    }
    if (analysisDisplayMode_ == normalized) {
        return;
    }
    analysisDisplayMode_ = normalized;
    rebuildAnalysisDisplay();
}

void DocumentViewModel::setAnalysisDeformationScale(double scale)
{
    if (!std::isfinite(scale) || scale < 0.0 || scale > 1000.0) {
        statusText_ = QStringLiteral("Deformation display scale must be between 0× and 1000×.");
        emit statusTextChanged();
        return;
    }
    if (analysisDeformationScale_ == scale) {
        return;
    }
    analysisDeformationScale_ = scale;
    rebuildAnalysisDisplay();
}

const DatumObject* DocumentViewModel::selectedObject() const noexcept
{
    if (document_ == nullptr || selectedRow_ < 0 ||
        selectedRow_ >= static_cast<int>(document_->objects().size())) {
        return nullptr;
    }
    return &document_->objects()[static_cast<std::size_t>(selectedRow_)];
}

const DatumTopologyReference* DocumentViewModel::selectedTopologyReference() const noexcept
{
    if (document_ == nullptr || selectedTopologyReferenceId_.isEmpty()) {
        return nullptr;
    }
    const std::string referenceId = selectedTopologyReferenceId_.toStdString();
    const auto found = std::find_if(document_->topologyReferences().begin(),
                                    document_->topologyReferences().end(),
                                    [&referenceId](const DatumTopologyReference& reference) {
                                        return reference.id == referenceId;
                                    });
    return found == document_->topologyReferences().end() ? nullptr : &*found;
}

Vector3 DocumentViewModel::previewDelta() const noexcept
{
    return moveActive_ ? previewDelta_ : Vector3{0.0, 0.0, 0.0};
}

void DocumentViewModel::updateStatus()
{
    bool parsed = false;
    const double distance = QLocale::c().toDouble(moveValue_.trimmed(), &parsed);
    const QString displayedValue = parsed && std::isfinite(distance)
        ? number(distance)
        : moveValue_.trimmed();
    statusText_ = QStringLiteral("MOVE  |  %1 axis  |  %2 mm  |  Enter: confirm  |  Esc: cancel")
        .arg(moveAxis_, displayedValue);
    emit statusTextChanged();
}

void DocumentViewModel::notifyObjectRows()
{
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1),
                         {ObjectPositionRole, ObjectVolumeRole, IsSelectedRole});
    }
}

void DocumentViewModel::rebuildAnalysisDisplay()
{
    analysisResultDisplay_ = {};
    analysisResultAvailable_ = false;
    analysisLegendTitle_.clear();
    analysisLegendMinimum_.clear();
    analysisLegendMaximum_.clear();
    analysisResultProvenance_.clear();

    const std::string objectId = displayObjectId().toStdString();
    const StructuralAnalysisRun* latestRun = nullptr;
    if (document_ != nullptr && !objectId.empty()) {
        for (const StructuralAnalysisStudy& study : document_->engineeringModel().structuralStudies) {
            if (study.cadObjectId != objectId) {
                continue;
            }
            for (auto iterator = study.runs.rbegin(); iterator != study.runs.rend(); ++iterator) {
                if (iterator->provenance.cadObjectId == objectId && iterator->converged) {
                    latestRun = &*iterator;
                    break;
                }
            }
        }
    }

    if (latestRun == nullptr) {
        analysisResultStatus_ = QStringLiteral("No structural result for this part.");
        analysisDisplayMode_ = QStringLiteral("Geometry");
        emit analysisDisplayChanged();
        return;
    }
    if (!latestRun->meshSnapshot.has_value()) {
        analysisResultStatus_ = QStringLiteral(
            "This stored result has no persisted analysis mesh. Re-run the analysis to display its fields.");
        analysisDisplayMode_ = QStringLiteral("Geometry");
        emit analysisDisplayChanged();
        return;
    }

    try {
        latestRun->validate(latestRun->provenance.boundaryConditionIds);
        std::string validationReason;
        if (!validateAnalysisRunForDisplay(*latestRun, validationReason)) {
            throw std::invalid_argument(validationReason);
        }
        analysisResultAvailable_ = true;
        analysisResultStatus_ = QStringLiteral("Structural field result ready.");
        const QString runId = QString::fromStdString(latestRun->provenance.solverRunId);
        analysisResultProvenance_ = QStringLiteral("Run %1 · %2 %3 · source revision %4 · material %5")
            .arg(runId.left(8), QString::fromStdString(latestRun->provenance.solverId),
                 QString::fromStdString(latestRun->provenance.solverVersion))
            .arg(static_cast<qulonglong>(latestRun->provenance.sourceDocumentRevision))
            .arg(QString::fromStdString(latestRun->provenance.materialRevision));
    } catch (const std::exception& error) {
        analysisResultStatus_ = QStringLiteral("Structural result is not displayable: %1")
                                    .arg(exceptionText(error));
        analysisDisplayMode_ = QStringLiteral("Geometry");
        emit analysisDisplayChanged();
        return;
    }

    if (analysisDisplayMode_ != QStringLiteral("Geometry")) {
        AnalysisResultField field;
        if (analysisDisplayMode_ == QStringLiteral("Displacement")) {
            field = AnalysisResultField::DisplacementMagnitude;
        } else if (analysisDisplayMode_ == QStringLiteral("von Mises Stress")) {
            field = AnalysisResultField::VonMisesStress;
        } else {
            field = AnalysisResultField::EquivalentStrain;
        }
        try {
            analysisResultDisplay_ = buildAnalysisResultDisplay(
                *latestRun, field, analysisDeformationScale_);
            analysisLegendTitle_ = QString::fromStdString(analysisResultDisplay_.fieldName);
            const QString units = QString::fromStdString(analysisResultDisplay_.units);
            analysisLegendMinimum_ = QStringLiteral("%1 %2")
                .arg(QString::number(analysisResultDisplay_.minimum, 'g', 4), units);
            analysisLegendMaximum_ = QStringLiteral("%1 %2")
                .arg(QString::number(analysisResultDisplay_.maximum, 'g', 4), units);
        } catch (const std::exception& error) {
            analysisResultAvailable_ = false;
            analysisResultStatus_ = QStringLiteral("Structural result is not displayable: %1")
                                        .arg(exceptionText(error));
            analysisDisplayMode_ = QStringLiteral("Geometry");
        }
    }
    emit analysisDisplayChanged();
}

bool DocumentViewModel::validateAnalysisRunForDisplay(const StructuralAnalysisRun& run,
                                                      std::string& reason) const
{
    if (document_ == nullptr || !run.meshSnapshot.has_value()) {
        reason = "the source analysis mesh snapshot is unavailable";
        return false;
    }
    const DatumObject* object = document_->object(run.provenance.cadObjectId);
    if (object == nullptr) {
        reason = "the source CAD object no longer exists";
        return false;
    }
    const MaterialDefinition* material =
        document_->electronicsModel().material(run.provenance.materialDefinitionId);
    if (material == nullptr) {
        reason = "the source material definition no longer exists in the current document";
        return false;
    }

    AnalysisMeshSettings settings;
    settings.maximumElementSizeM = run.provenance.mesh.requestedMaximumElementSizeM;
    settings.maximumElementAspectRatio = std::max(1.0, run.provenance.mesh.maximumAspectRatio + 1e-9);
    settings.maximumElementCount = std::max<std::uint64_t>(
        settings.maximumElementCount, run.provenance.mesh.elementCount);
    try {
        if (run.provenance.mesh.elementType == "tetrahedron4") {
            run.meshSnapshot->validate(run.provenance.mesh);
            if (run.provenance.mesh.geometrySha256.empty() ||
                analysisGeometrySha256(*object) != run.provenance.mesh.geometrySha256) {
                reason = "the exact CAD geometry differs from the tetrahedral result source";
                return false;
            }
            for (const auto& id : run.provenance.topologyReferenceIds) {
                const auto& references = document_->topologyReferences();
                if (std::none_of(references.begin(), references.end(), [&](const auto& ref) {
                    return ref.id == id && ref.objectId == object->id;
                })) {
                    reason = "a tetrahedral result source face no longer exists";
                    return false;
                }
            }
            return true;
        }
        const AnalysisMesh currentMesh = OcctPrismAnalysisMesher::generate(
            *document_, run.provenance.cadObjectId, settings);
        if (currentMesh.provenance.generator != run.provenance.mesh.generator ||
            currentMesh.provenance.generatorVersion != run.provenance.mesh.generatorVersion ||
            currentMesh.provenance.elementType != run.provenance.mesh.elementType ||
            currentMesh.topologyReferenceIds != run.provenance.topologyReferenceIds ||
            currentMesh.nodes.size() != run.meshSnapshot->nodePositionsM.size() ||
            currentMesh.elements.size() != run.meshSnapshot->hexahedra.size()) {
            reason = "the current CAD-derived mesh no longer matches the result's topology or mesh settings";
            return false;
        }
        for (std::size_t node = 0; node < currentMesh.nodes.size(); ++node) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double expected = run.meshSnapshot->nodePositionsM[node][axis];
                const double actual = currentMesh.nodes[node].positionM[axis];
                const double tolerance = 1e-12 * std::max({1.0, std::abs(expected), std::abs(actual)});
                if (std::abs(expected - actual) > tolerance) {
                    reason = "the current CAD geometry differs from the saved analysis mesh";
                    return false;
                }
            }
        }
        for (std::size_t element = 0; element < currentMesh.elements.size(); ++element) {
            if (currentMesh.elements[element].nodeIndices != run.meshSnapshot->hexahedra[element]) {
                reason = "the current CAD-derived element connectivity differs from the saved analysis mesh";
                return false;
            }
        }
    } catch (const std::exception& error) {
        reason = std::string("the current CAD mesh could not be verified: ") + error.what();
        return false;
    }
    return true;
}

}  // namespace datumloop
