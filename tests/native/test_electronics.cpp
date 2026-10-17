#include "datumloop/electronics/ElectronicsModel.hpp"
#include "datumloop/io/ElectronicsAdapter.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/geometry/PcbShapeBuilder.hpp"
#include "datumloop/solver/NgspiceProcessCircuitSolver.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool close(double left, double right, double tolerance = 1e-10)
{
    return std::abs(left - right) <= tolerance;
}

std::string uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    try {
        QFile circuitJsonFile(QStringLiteral(DATUMLOOP_FIXTURE_DIR "/minimal-board.circuit.json"));
        require(circuitJsonFile.open(QIODevice::ReadOnly), "could not read minimal Circuit JSON fixture");
        const datumloop::ElectronicsImportResult imported =
            datumloop::CircuitJsonAdapter::importBytes(circuitJsonFile.readAll());
        imported.model.validate();
        require(imported.model.materials.size() == 2, "Circuit JSON import did not register shared board/copper materials");
        require(imported.model.pcbs.size() == 1 && imported.model.circuits.size() == 1,
                "Circuit JSON import did not produce native PCB and circuit models");
        const auto& pcb = imported.model.pcbs.front();
        const auto& circuit = imported.model.circuits.front();
        require(pcb.components.size() == 2 && pcb.pads.size() == 2 && pcb.traces.size() == 1 && pcb.vias.size() == 1,
                "Circuit JSON import changed component, pad, trace, or via counts");
        require(circuit.nets.size() == 1 && circuit.nets.front().pinIds.size() == 2,
                "Circuit JSON source-net membership did not become circuit pin connectivity");
        require(close(pcb.widthM, 0.02) && close(pcb.thicknessM, 0.0016) &&
                    close(pcb.components[1].centerM[0], 0.015) &&
                    close(pcb.pads[0].centerM[0], 0.004) && close(pcb.traces[0].routeM[1][0], 0.009) &&
                    pcb.traces[0].netId == circuit.nets.front().id && pcb.vias[0].netId == circuit.nets.front().id,
                "Circuit JSON import lost SI coordinates, net links, or geometry values");
        require(imported.model.materials.front().propertyModels.empty() &&
                    imported.model.materials.front().composition.empty(),
                "Circuit JSON import inferred unsupported board material properties");
        const datumloop::PcbViewportGeometry circuitJsonView = datumloop::PcbShapeBuilder::build(pcb);
        require(datumloop::OcctKernel::measure(circuitJsonView.shape).valid &&
                    datumloop::OcctKernel::tessellate(circuitJsonView.shape).triangleCount() > 0 &&
                    !circuitJsonView.notices.empty(),
                "Circuit JSON PCB did not produce a displayable exact 3D preview with an unknown-thickness notice");
        datumloop::PcbDocument pcbWithPackageHeight = pcb;
        pcbWithPackageHeight.components.front().packageHeightM = 0.004;
        const datumloop::PcbViewportGeometry packageView = datumloop::PcbShapeBuilder::build(pcbWithPackageHeight);
        require(datumloop::OcctKernel::measure(packageView.shape).valid &&
                    std::any_of(packageView.notices.begin(), packageView.notices.end(), [](const std::string& notice) {
                        return notice.find("rectangular envelopes") != std::string::npos;
                    }) &&
                    std::any_of(packageView.notices.begin(), packageView.notices.end(), [](const std::string& notice) {
                        return notice.find("omitted because package height is unknown") != std::string::npos;
                    }),
                "PCB preview did not show the known package envelope or disclose an unknown component height");

        const QByteArray exported = datumloop::CircuitJsonAdapter::exportPcb(imported.model, pcb.id);
        const datumloop::ElectronicsImportResult reimported = datumloop::CircuitJsonAdapter::importBytes(exported);
        const auto& roundTripPcb = reimported.model.pcbs.front();
        const auto& roundTripCircuit = reimported.model.circuits.front();
        require(roundTripPcb.components.size() == 2 && roundTripPcb.pads.size() == 2 &&
                    roundTripPcb.traces.size() == 1 && roundTripPcb.vias.size() == 1 &&
                    roundTripCircuit.nets.size() == 1 && roundTripCircuit.nets.front().pinIds.size() == 2,
                "Circuit JSON export/import round-trip lost board elements or circuit connectivity");
        require(close(roundTripPcb.pads[1].widthM, 0.001) &&
                    close(roundTripPcb.components[1].rotationRad, 1.5707963267948966),
                "Circuit JSON export/import round-trip changed pad size or component rotation");

        QFile kicadFile(QStringLiteral(DATUMLOOP_FIXTURE_DIR "/minimal-board.kicad_pcb"));
        require(kicadFile.open(QIODevice::ReadOnly), "could not read minimal KiCad PCB fixture");
        const datumloop::ElectronicsImportResult kicad =
            datumloop::KiCadPcbAdapter::importBytes(kicadFile.readAll(), "KiCad Fixture Board");
        kicad.model.validate();
        const auto& kicadPcb = kicad.model.pcbs.front();
        const auto& kicadCircuit = kicad.model.circuits.front();
        require(kicadPcb.widthM == 0.03 && kicadPcb.heightM == 0.02 &&
                    kicadPcb.components.size() == 1 && kicadPcb.pads.size() == 2 &&
                    kicadPcb.traces.size() == 2 && kicadPcb.vias.size() == 1 &&
                    kicadCircuit.nets.size() == 2,
                "KiCad import changed board, component, pad, trace, via, or net counts");
        require(close(kicadPcb.pads[0].centerM[0], 0.01) && close(kicadPcb.pads[0].centerM[1], 0.009) &&
                    close(kicadPcb.pads[1].centerM[0], 0.01) && close(kicadPcb.pads[1].centerM[1], 0.011) &&
                    kicadPcb.pads[0].netId != kicadPcb.pads[1].netId &&
                    kicadCircuit.nets[0].pinIds.size() == 1 && kicadCircuit.nets[1].pinIds.size() == 1,
                "KiCad import lost rotated pad positions or net-to-pin links");
        require(kicad.model.materials.front().propertyModels.empty() &&
                    kicad.model.materials.front().composition.empty(),
                "KiCad import inferred a laminate grade or unsupported material properties");
        const datumloop::PcbViewportGeometry kicadView = datumloop::PcbShapeBuilder::build(kicadPcb);
        require(datumloop::OcctKernel::measure(kicadView.shape).valid &&
                    datumloop::OcctKernel::tessellate(kicadView.shape).triangleCount() > 0 &&
                    close(kicadPcb.centerM[0], 0.015) && close(kicadPcb.centerM[1], 0.01),
                "KiCad PCB center or derived exact viewport geometry was not preserved");

#ifdef DATUMLOOP_HAS_NGSPICE
        datumloop::CircuitDocument dcCircuit;
        dcCircuit.id = uuid();
        dcCircuit.name = "5 V resistor load";
        datumloop::CircuitComponent source;
        source.id = uuid();
        source.sourceId = "source-5v";
        source.reference = "V1";
        source.displayName = "5 V source";
        source.kind = datumloop::CircuitComponentKind::VoltageSource;
        source.electricalValue = 5.0;
        source.electricalValueUnit = "V";
        datumloop::CircuitComponent load;
        load.id = uuid();
        load.sourceId = "load-1k";
        load.reference = "R1";
        load.displayName = "1 kOhm load";
        load.kind = datumloop::CircuitComponentKind::Resistor;
        load.electricalValue = 1000.0;
        load.electricalValueUnit = "ohm";
        datumloop::CircuitPin sourcePlus{uuid(), source.id, "1", "positive"};
        datumloop::CircuitPin sourceMinus{uuid(), source.id, "2", "negative"};
        datumloop::CircuitPin loadPlus{uuid(), load.id, "1", "A"};
        datumloop::CircuitPin loadMinus{uuid(), load.id, "2", "B"};
        source.pinIds = {sourcePlus.id, sourceMinus.id};
        load.pinIds = {loadPlus.id, loadMinus.id};
        const std::string vccId = uuid();
        const std::string groundId = uuid();
        dcCircuit.nets = {{vccId, "net-vcc", "VCC", false, {sourcePlus.id, loadPlus.id}},
                          {groundId, "net-ground", "GND", true, {sourceMinus.id, loadMinus.id}}};
        dcCircuit.components = {source, load};
        dcCircuit.pins = {sourcePlus, sourceMinus, loadPlus, loadMinus};
        const QString workerPath = QCoreApplication::applicationDirPath() + QStringLiteral("/datumloop-ngspice-worker");
        const datumloop::CircuitSimulationResult simulation =
            datumloop::NgspiceProcessCircuitSolver(workerPath).solveDcOperatingPoint(dcCircuit);
        require(close(simulation.netVoltagesV.at(vccId), 5.0, 1e-6) &&
                    close(simulation.netVoltagesV.at(groundId), 0.0, 1e-9) &&
                    simulation.components.size() == 2 && simulation.thermalSourcePlaceholders.size() == 1,
                "ngspice did not return the expected net voltages and resistor thermal-source placeholder");
        const auto resistorResult = std::find_if(simulation.components.begin(), simulation.components.end(),
                                                 [&](const auto& item) { return item.componentId == load.id; });
        const auto sourceResult = std::find_if(simulation.components.begin(), simulation.components.end(),
                                               [&](const auto& item) { return item.componentId == source.id; });
        require(resistorResult != simulation.components.end() && sourceResult != simulation.components.end() &&
                    close(resistorResult->currentA, 0.005, 1e-6) && close(resistorResult->powerW, 0.025, 1e-6) &&
                    close(sourceResult->currentA, -0.005, 1e-6) &&
                    close(simulation.thermalSourcePlaceholders.front().powerW, 0.025, 1e-6) &&
                    !simulation.thermalSourcePlaceholders.front().coupledToThermalSolver,
                "ngspice current/power or explicitly uncoupled thermal placeholder is incorrect");
#endif

        std::cout << "Electronics checks passed: SI conversion, material unknowns, Circuit JSON/KiCad import, PCB viewport, and available solver checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Electronics test failure: " << error.what() << '\n';
        return 1;
    }
}
