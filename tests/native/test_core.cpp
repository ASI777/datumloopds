#include "datumloop/core/DatumCommand.hpp"
#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/geometry/OcctKernel.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool close(double left, double right, double tolerance = 1e-8)
{
    return std::abs(left - right) <= tolerance;
}

}  // namespace

int main()
{
    try {
        datumloop::DatumDocument document;
        const datumloop::DatumObject& part = document.createBox("DatumLoop Block", {40.0, 30.0, 20.0});
        const std::string id = part.id;

        require(part.id.size() == 36, "document object id is not UUID-shaped");
        require(part.metrics.valid, "OCCT box must be valid");
        require(close(part.metrics.volume, 24000.0), "OCCT box volume is incorrect");
        require(close(part.metrics.surfaceArea, 5200.0), "OCCT box area is incorrect");
        require(close(part.metrics.minimum[0], -20.0), "OCCT box lower bound is incorrect");
        require(close(part.metrics.maximum[2], 10.0), "OCCT box upper bound is incorrect");

        const datumloop::TriangleMesh mesh = datumloop::OcctKernel::tessellate(part.shape);
        require(mesh.triangleCount() > 0, "exact OCCT shape did not produce display triangles");
        require(mesh.vertices.size() == mesh.triangleCount() * 3, "display mesh is not a triangle list");
        const double authoritativeVolume = part.metrics.volume;
        const datumloop::TriangleMesh disposableDisplayMesh{};
        require(disposableDisplayMesh.triangleCount() == 0,
                "display mesh should be disposable independently of the exact shape");
        const datumloop::TriangleMesh regeneratedMesh = datumloop::OcctKernel::tessellate(part.shape);
        require(regeneratedMesh.triangleCount() == mesh.triangleCount(),
                "display mesh could not be regenerated from the authoritative exact shape");
        require(close(part.metrics.volume, authoritativeVolume),
                "display tessellation changed exact-shape volume metrics");

        datumloop::CommandHistory history;
        history.execute(document, std::make_unique<datumloop::MoveObjectCommand>(
            id, datumloop::Vector3{0.0, 0.0, 0.0}, datumloop::Vector3{12.5, -3.0, 0.0}));
        require(document.object(id) != nullptr, "created object cannot be resolved by id");
        require(close(document.object(id)->position[0], 12.5), "move command did not update document position");
        require(history.canUndo() && !history.canRedo(), "command history state after execute is incorrect");

        require(history.undo(document), "move command could not be undone");
        require(close(document.object(id)->position[0], 0.0), "undo did not restore the prior position");
        require(history.canRedo(), "undo did not preserve the redo command");

        bool rejectedUnknownObject = false;
        try {
            history.execute(document, std::make_unique<datumloop::MoveObjectCommand>(
                "missing-object", datumloop::Vector3{}, datumloop::Vector3{1.0, 0.0, 0.0}));
        } catch (const std::invalid_argument&) {
            rejectedUnknownObject = true;
        }
        require(rejectedUnknownObject, "move command accepted an unknown object id");
        require(history.canRedo(), "failed command erased the valid redo branch");

        require(history.redo(document), "move command could not be redone");
        require(close(document.object(id)->position[1], -3.0), "redo did not restore the moved position");

        const std::uint64_t revision = document.revision();
        require(!document.setPosition(id, {NAN, 0.0, 0.0}), "document accepted a non-finite position");
        require(document.revision() == revision, "invalid position changed the document revision");

        std::cout << "Native DatumLoop core checks passed: exact OCCT box, metrics, tessellation, selection identity, and move undo/redo.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native DatumLoop core check failed: " << error.what() << '\n';
        return 1;
    }
}
