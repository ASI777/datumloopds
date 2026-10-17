#include "datumloop/core/DatumCommand.hpp"

#include <stdexcept>
#include <utility>

namespace datumloop {

MoveObjectCommand::MoveObjectCommand(std::string objectId, Vector3 before, Vector3 after)
    : objectId_(std::move(objectId)), before_(before), after_(after)
{
}

void MoveObjectCommand::execute(DatumDocument& document)
{
    if (!document.setPosition(objectId_, after_)) {
        throw std::invalid_argument("move command references an unknown object or invalid position");
    }
}

void MoveObjectCommand::undo(DatumDocument& document)
{
    if (!document.setPosition(objectId_, before_)) {
        throw std::invalid_argument("move command cannot restore its object position");
    }
}

SetFeatureGraphCommand::SetFeatureGraphCommand(std::string objectId, DatumFeatureGraph graph)
    : objectId_(std::move(objectId)), graph_(std::move(graph))
{
}

void SetFeatureGraphCommand::execute(DatumDocument& document)
{
    if (after_.has_value()) {
        document.restoreObjectSnapshot(*after_);
        return;
    }
    DatumObjectSnapshot before = document.snapshotObject(objectId_);
    document.setFeatureGraph(objectId_, graph_);
    before_ = std::move(before);
    after_ = document.snapshotObject(objectId_);
}

void SetFeatureGraphCommand::undo(DatumDocument& document)
{
    if (!before_.has_value()) {
        throw std::logic_error("feature graph command was not executed");
    }
    document.restoreObjectSnapshot(*before_);
}

void CommandHistory::execute(DatumDocument& document, std::unique_ptr<DatumCommand> command)
{
    if (!command) {
        throw std::invalid_argument("cannot execute an empty command");
    }
    command->execute(document);
    commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(cursor_), commands_.end());
    commands_.push_back(std::move(command));
    cursor_ = commands_.size();
}

bool CommandHistory::undo(DatumDocument& document)
{
    if (!canUndo()) {
        return false;
    }
    commands_[cursor_ - 1]->undo(document);
    --cursor_;
    return true;
}

bool CommandHistory::redo(DatumDocument& document)
{
    if (!canRedo()) {
        return false;
    }
    commands_[cursor_]->execute(document);
    ++cursor_;
    return true;
}

void CommandHistory::clear() noexcept
{
    commands_.clear();
    cursor_ = 0;
}

}  // namespace datumloop
