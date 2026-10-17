#pragma once

#include "datumloop/core/DatumCommand.hpp"
#include "datumloop/core/DatumDocument.hpp"

#include <QAbstractListModel>
#include <QUrl>
#include <QVector3D>

namespace datumloop {

class DocumentViewModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedObjectId READ selectedObjectId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedTopologyReferenceId READ selectedTopologyReferenceId NOTIFY selectionChanged)
    Q_PROPERTY(QString displayObjectId READ displayObjectId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedType READ selectedType NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPositionText READ selectedPositionText NOTIFY displayPositionChanged)
    Q_PROPERTY(QString selectedBoundsText READ selectedBoundsText NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedVolumeText READ selectedVolumeText NOTIFY selectionChanged)
    Q_PROPERTY(QVector3D displayPosition READ displayPosition NOTIFY displayPositionChanged)
    Q_PROPERTY(bool moveActive READ moveActive NOTIFY moveStateChanged)
    Q_PROPERTY(QString moveAxis READ moveAxis NOTIFY moveStateChanged)
    Q_PROPERTY(QString moveValue READ moveValue NOTIFY moveStateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)

public:
    enum Role {
        ObjectIdRole = Qt::UserRole + 1,
        ObjectNameRole,
        ObjectTypeRole,
        IsSelectedRole,
        ObjectPositionRole,
        ObjectVolumeRole
    };
    Q_ENUM(Role)

    explicit DocumentViewModel(DatumDocument* document, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int selectedRow() const noexcept { return selectedRow_; }
    [[nodiscard]] QString selectedObjectId() const;
    [[nodiscard]] QString selectedTopologyReferenceId() const { return selectedTopologyReferenceId_; }
    [[nodiscard]] QString displayObjectId() const;
    [[nodiscard]] QString selectedName() const;
    [[nodiscard]] QString selectedType() const;
    [[nodiscard]] QString selectedPositionText() const;
    [[nodiscard]] QString selectedBoundsText() const;
    [[nodiscard]] QString selectedVolumeText() const;
    [[nodiscard]] QVector3D displayPosition() const;
    [[nodiscard]] bool moveActive() const noexcept { return moveActive_; }
    [[nodiscard]] QString moveAxis() const { return moveAxis_; }
    [[nodiscard]] QString moveValue() const { return moveValue_; }
    [[nodiscard]] QString statusText() const { return statusText_; }
    [[nodiscard]] bool canUndo() const noexcept { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return history_.canRedo(); }
    [[nodiscard]] QString projectName() const;
    [[nodiscard]] bool dirty() const noexcept { return document_ != nullptr && document_->isDirty(); }

    Q_INVOKABLE void selectRow(int row);
    Q_INVOKABLE bool selectObjectId(const QString& objectId);
    Q_INVOKABLE bool selectTopologyReference(const QString& objectId, const QString& referenceId);
    Q_INVOKABLE void beginMove();
    Q_INVOKABLE void setMoveAxis(const QString& axis);
    Q_INVOKABLE void setMoveValue(const QString& value);
    Q_INVOKABLE void confirmMove();
    Q_INVOKABLE void cancelMove();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void frameSelected();
    Q_INVOKABLE void saveProject();
    Q_INVOKABLE void saveProjectAs(const QUrl& fileUrl);
    Q_INVOKABLE void openProject(const QUrl& fileUrl);
    Q_INVOKABLE void reportInteractionStatus(const QString& message);

signals:
    void selectionChanged();
    void displayPositionChanged();
    void moveStateChanged();
    void statusTextChanged();
    void historyChanged();
    void frameSelectedRequested(const QVector3D& position);
    void projectChanged();
    void saveAsRequested();
    void documentContentChanged();

private:
    [[nodiscard]] const DatumObject* selectedObject() const noexcept;
    [[nodiscard]] const DatumTopologyReference* selectedTopologyReference() const noexcept;
    [[nodiscard]] Vector3 previewDelta() const noexcept;
    void updateStatus();
    void notifyObjectRows();

    DatumDocument* document_ = nullptr;
    int selectedRow_ = -1;
    QString selectedTopologyReferenceId_;
    bool moveActive_ = false;
    QString moveAxis_ = QStringLiteral("X");
    QString moveValue_ = QStringLiteral("0");
    QString statusText_ = QStringLiteral("Ready  |  Design workspace  |  mm");
    Vector3 previewDelta_{0.0, 0.0, 0.0};
    CommandHistory history_;
    QString projectPath_;
};

}  // namespace datumloop
