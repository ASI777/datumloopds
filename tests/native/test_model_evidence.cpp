#include "datumloop/digitaltwin/ModelEvidence.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireThrows(Function&& function, const char* message)
{
    bool threw = false;
    try {
        function();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

}  // namespace

int main()
{
    try {
        datumloop::ParameterEstimate inertia;
        inertia.id = "motor.rotor.inertia";
        inertia.dimension = datumloop::UnitDimension::kilogram().multiply(datumloop::UnitDimension::meter().pow(2));
        inertia.nominalValue = 0.001;
        inertia.estimatedValue = 0.00112;
        inertia.standardUncertainty = 0.00003;
        inertia.provenance = {datumloop::ParameterSourceKind::SystemIdentification, "calibration-2026-01",
                              "least-squares fit against encoder/current telemetry", "bench-run-004",
                              "Estimate retains the original nominal value for comparison."};
        inertia.validate();
        require(inertia.nominalValue == 0.001 && inertia.valueForSimulation() == 0.00112 &&
                    inertia.valueForSimulation(false) == 0.001,
                "parameter estimate must preserve nominal and calibrated values separately");
        auto missingDataset = inertia;
        missingDataset.provenance.datasetId.clear();
        requireThrows([&]() { missingDataset.validate(); },
                      "system-identification provenance accepted a missing dataset");

        datumloop::ObservableValue predicted;
        predicted.dimension = datumloop::UnitDimension::meter();
        predicted.components = {0.010};
        predicted.provenanceReference = "twin:revision-17";
        datumloop::ObservableValue measured = predicted;
        measured.components = {0.011};
        measured.provenanceReference = "measurement:encoder-004";
        datumloop::ObservableResidual positionResidual{"encoder.position", 0.25, predicted, measured};
        datumloop::ModelDiscrepancy discrepancy{"one-axis-twin", "revision-17", {}};
        discrepancy.add(positionResidual);
        discrepancy.validate();
        require(std::abs(discrepancy.observables.front().residual().front() - 0.001) < 1e-14,
                "model discrepancy must record measured minus predicted state");

        auto frameMismatch = positionResidual;
        frameMismatch.predicted.quantityType = datumloop::EquationQuantityType::Vector;
        frameMismatch.predicted.referenceFrame = "world";
        frameMismatch.predicted.coordinateSystem = "Cartesian";
        frameMismatch.predicted.components = {1.0, 2.0, 3.0};
        frameMismatch.measured = frameMismatch.predicted;
        frameMismatch.measured.referenceFrame = "body";
        requireThrows([&]() { frameMismatch.validate(); },
                      "discrepancy silently mixed world and body frame values");

        std::cout << "Parameter provenance and model discrepancy evidence checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Model evidence test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
