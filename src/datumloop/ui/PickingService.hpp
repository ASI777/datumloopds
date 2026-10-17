#pragma once

#include <QObject>
#include <QString>

namespace datumloop {

class DocumentViewModel;

// Resolves a renderer-level object hit to a persistent DatumLoop object ID
// before changing document selection. Subshape resolution is intentionally
// not claimed until persistent topology references are implemented.
class PickingService final : public QObject {
    Q_OBJECT

public:
    explicit PickingService(DocumentViewModel* documentModel, QObject* parent = nullptr);

    Q_INVOKABLE bool resolveObjectHit(const QString& objectId);

signals:
    void unresolvedHit(const QString& objectId);

private:
    DocumentViewModel* documentModel_ = nullptr;
};

}  // namespace datumloop
