#pragma once

#include "datumloop/core/DatumDocument.hpp"

#include <memory>
#include <string>
#include <vector>

namespace datumloop {

class DatumCommand {
public:
    virtual ~DatumCommand() = default;
    virtual void execute(DatumDocument& document) = 0;
    virtual void undo(DatumDocument& document) = 0;
    [[nodiscard]] virtual std::string label() const = 0;
};

class MoveObjectCommand final : public DatumCommand {
public:
    MoveObjectCommand(std::string objectId, Vector3 before, Vector3 after);
    void execute(DatumDocument& document) override;
    void undo(DatumDocument& document) override;
    [[nodiscard]] std::string label() const override { return "Move Object"; }

private:
    std::string objectId_;
    Vector3 before_;
    Vector3 after_;
};

class CommandHistory final {
public:
    void execute(DatumDocument& document, std::unique_ptr<DatumCommand> command);
    [[nodiscard]] bool undo(DatumDocument& document);
    [[nodiscard]] bool redo(DatumDocument& document);
    [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return cursor_ < commands_.size(); }

private:
    std::vector<std::unique_ptr<DatumCommand>> commands_;
    std::size_t cursor_ = 0;
};

}  // namespace datumloop
