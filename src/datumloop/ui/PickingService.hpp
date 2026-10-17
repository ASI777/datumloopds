#pragma once

#include <QObject>
#include <QString>
#include <QVector3D>

namespace datumloop {

class DocumentViewModel;
class DatumDocument;

// Resolves renderer hits to persistent DatumLoop object or topology IDs before
// changing selection. Ambiguous or unsupported topology hits remain unresolved.
class PickingService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString selectionMode READ selectionMode NOTIFY selectionModeChanged)

public:
    explicit PickingService(DocumentViewModel* documentModel,
                            DatumDocument* document,
                            QObject* parent = nullptr);

    [[nodiscard]] QString selectionMode() const { return selectionMode_; }
    Q_INVOKABLE void setSelectionMode(const QString& mode);
    Q_INVOKABLE bool resolveObjectHit(const QString& objectId);
    Q_INVOKABLE bool resolveSubshapeHit(const QString& objectId,
                                        const QVector3D& localPointMm,
                                        const QVector3D& localNormal,
                                        double toleranceMm);

signals:
    void selectionModeChanged();
    void unresolvedHit(const QString& objectId, const QString& reason);

private:
    DocumentViewModel* documentModel_ = nullptr;
    DatumDocument* document_ = nullptr;
    QString selectionMode_ = QStringLiteral("Object");
};

}  // namespace datumloop
