#include "datumloop/ui/DocumentViewModel.hpp"

#include <QCoreApplication>

#include <cmath>
#include <iostream>
#include <stdexcept>

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

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    try {
        datumloop::DatumDocument document;
        const datumloop::DatumObject& part = document.createBox("Test Block", {20.0, 16.0, 12.0});
        const std::string objectId = part.id;
        datumloop::DocumentViewModel model(&document);

        require(model.rowCount() == 1, "outliner model did not expose its document object");
        require(model.selectedRow() == 0, "the first document object was not selected");
        require(model.data(model.index(0), datumloop::DocumentViewModel::ObjectIdRole).toString()
                    == QString::fromStdString(objectId),
                "outliner model did not expose the stable object id");

        model.beginMove();
        model.setMoveAxis(QStringLiteral("Z"));
        model.setMoveValue(QStringLiteral("5.5"));
        require(model.moveActive(), "move preview did not enter the active state");
        require(close(model.displayPosition().z(), 5.5F), "move preview did not constrain the Z axis");
        require(close(static_cast<float>(document.object(objectId)->position[2]), 0.0F),
                "move preview mutated the document before confirmation");
        model.cancelMove();
        require(!model.moveActive(), "cancel did not exit the move tool");
        require(close(model.displayPosition().z(), 0.0F), "cancel did not remove the preview transform");
        require(close(static_cast<float>(document.object(objectId)->position[2]), 0.0F),
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
        require(close(static_cast<float>(document.object(objectId)->position[1]), -3.25F),
                "confirm did not commit the move to the document");
        require(model.canUndo() && !model.canRedo(), "committed move history state is incorrect");

        model.undo();
        require(close(static_cast<float>(document.object(objectId)->position[1]), 0.0F),
                "view-model undo did not restore the original position");
        model.redo();
        require(close(static_cast<float>(document.object(objectId)->position[1]), -3.25F),
                "view-model redo did not restore the committed position");

        std::cout << "Native DatumLoop UI model checks passed: selection identity, constrained preview, cancel, validation, commit, undo, and redo.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native DatumLoop UI model check failed: " << error.what() << '\n';
        return 1;
    }
}
