#include "datumloop/ui/PickingService.hpp"

#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/topology/DatumTopologyResolver.hpp"
#include "datumloop/ui/DocumentViewModel.hpp"

#include <Standard_Failure.hxx>

namespace datumloop {

PickingService::PickingService(DocumentViewModel* documentModel,
                               DatumDocument* document,
                               QObject* parent)
    : QObject(parent), documentModel_(documentModel), document_(document)
{
}

void PickingService::setSelectionMode(const QString& mode)
{
    const QString normalized = mode.trimmed().toLower();
    const bool valid = normalized == QStringLiteral("object") ||
                       normalized == QStringLiteral("face") ||
                       normalized == QStringLiteral("edge") ||
                       normalized == QStringLiteral("vertex");
    if (!valid) {
        return;
    }
    const QString canonical = normalized.left(1).toUpper() + normalized.mid(1);
    if (selectionMode_ != canonical) {
        selectionMode_ = canonical;
        emit selectionModeChanged();
    }
}

bool PickingService::resolveObjectHit(const QString& objectId)
{
    if (documentModel_ != nullptr && documentModel_->selectObjectId(objectId)) {
        return true;
    }
    emit unresolvedHit(objectId, QStringLiteral("Object is no longer in the document."));
    return false;
}

bool PickingService::resolveSubshapeHit(const QString& objectId,
                                        const QVector3D& localPointMm,
                                        const QVector3D& localNormal,
                                        double toleranceMm)
{
    if (documentModel_ == nullptr || document_ == nullptr || objectId.isEmpty()) {
        emit unresolvedHit(objectId, QStringLiteral("The document is unavailable."));
        return false;
    }
    const auto subshapeType = datumSubshapeTypeFromName(selectionMode_.toLower().toStdString());
    if (!subshapeType.has_value()) {
        emit unresolvedHit(objectId, QStringLiteral("Choose Object, Face, Edge, or Vertex selection."));
        return false;
    }
    const DatumObject* object = document_->object(objectId.toStdString());
    if (object == nullptr) {
        emit unresolvedHit(objectId, QStringLiteral("Object is no longer in the document."));
        return false;
    }
    try {
        const DatumTopologyHitResolution hit = DatumTopologyResolver::resolveHitAtPoint(
            document_->topologyReferences(), object->id, object->shape, *subshapeType,
            {localPointMm.x(), localPointMm.y(), localPointMm.z()},
            Vector3{localNormal.x(), localNormal.y(), localNormal.z()}, toleranceMm);
        if (hit.status == DatumTopologyResolutionStatus::Resolved &&
            documentModel_->selectTopologyReference(objectId, QString::fromStdString(hit.referenceId))) {
            return true;
        }
        const QString reason = hit.status == DatumTopologyResolutionStatus::Ambiguous
            ? QStringLiteral("Topology hit is ambiguous; zoom in or choose another selection mode.")
            : hit.status == DatumTopologyResolutionStatus::UnsupportedFeature
                ? QStringLiteral("Feature-based topology references cannot be resolved yet.")
                : QStringLiteral("No persistent %1 reference matched this exact-geometry hit.")
                    .arg(selectionMode_.toLower());
        emit unresolvedHit(objectId, reason);
    } catch (const Standard_Failure& error) {
        const char* message = error.GetMessageString();
        emit unresolvedHit(objectId, QString::fromUtf8(message == nullptr
                                                           ? "OCCT could not resolve the topology hit."
                                                           : message));
    } catch (const std::exception& error) {
        emit unresolvedHit(objectId, QString::fromUtf8(error.what()));
    }
    return false;
}

}  // namespace datumloop
