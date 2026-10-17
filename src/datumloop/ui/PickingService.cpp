#include "datumloop/ui/PickingService.hpp"

#include "datumloop/ui/DocumentViewModel.hpp"

namespace datumloop {

PickingService::PickingService(DocumentViewModel* documentModel, QObject* parent)
    : QObject(parent), documentModel_(documentModel)
{
}

bool PickingService::resolveObjectHit(const QString& objectId)
{
    if (documentModel_ != nullptr && documentModel_->selectObjectId(objectId)) {
        return true;
    }
    emit unresolvedHit(objectId);
    return false;
}

}  // namespace datumloop
