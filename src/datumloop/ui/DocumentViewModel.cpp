#include "datumloop/ui/DocumentViewModel.hpp"

#include <QLocale>

#include <algorithm>
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

}  // namespace

DocumentViewModel::DocumentViewModel(DatumDocument* document, QObject* parent)
    : QAbstractListModel(parent), document_(document)
{
    if (document_ != nullptr && !document_->objects().empty()) {
        selectedRow_ = 0;
    }
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
    return selectedObject() == nullptr ? QStringLiteral("—") : QStringLiteral("OCCT B-Rep Solid");
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
    if (selectedRow_ == row) {
        return;
    }
    const int previous = selectedRow_;
    selectedRow_ = row;
    if (previous >= 0) {
        emit dataChanged(index(previous), index(previous), {IsSelectedRole});
    }
    if (selectedRow_ >= 0) {
        emit dataChanged(index(selectedRow_), index(selectedRow_), {IsSelectedRole});
    }
    emit selectionChanged();
    emit displayPositionChanged();
    if (moveActive_) {
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

const DatumObject* DocumentViewModel::selectedObject() const noexcept
{
    if (document_ == nullptr || selectedRow_ < 0 ||
        selectedRow_ >= static_cast<int>(document_->objects().size())) {
        return nullptr;
    }
    return &document_->objects()[static_cast<std::size_t>(selectedRow_)];
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

}  // namespace datumloop
