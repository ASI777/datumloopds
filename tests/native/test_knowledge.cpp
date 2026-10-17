#include "datumloop/knowledge/EquationRegistry.hpp"
#include "datumloop/numerics/CantileverBeamFEM.hpp"
#include "datumloop/solver/PhysicsSolver.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool close(double left, double right, double tolerance)
{
    return std::abs(left - right) <= tolerance;
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

datumloop::UnitDimension accelerationUnit()
{
    return {{0, 1, -2, 0, 0, 0, 0}, "m/s^2"};
}

}  // namespace

int main()
{
    try {
        const datumloop::EquationRegistry registry = datumloop::makeInitialEquationRegistry();
        require(registry.equations().size() == 3, "initial equation registry must contain the three reviewed equations");
        const auto* newton = registry.find("mechanics.newton.second-law.translation.v1");
        const auto* heat = registry.find("thermal.heat-equation.isotropic.v1");
        const auto* elasticity = registry.find("materials.elasticity.uniaxial-linear.v1");
        require(newton != nullptr && heat != nullptr && elasticity != nullptr &&
                    newton->maturity == datumloop::ModelMaturity::E3 &&
                    registry.findAtLeast(newton->id, datumloop::ModelMaturity::E3) == newton &&
                    registry.findAtLeast(newton->id, datumloop::ModelMaturity::E4) == nullptr,
                "equation maturity lookup did not enforce the declared evidence level");

        // Constant-force trajectory: m=2 kg, F=6 N, t=3 s, x0=1 m, v0=-1 m/s.
        const double acceleration = 6.0 / 2.0;
        const double position = 1.0 + (-1.0) * 3.0 + 0.5 * acceleration * 9.0;
        require(close(acceleration, 3.0, 1e-14) && close(position, 11.5, 1e-14),
                "Newton equation analytical trajectory failed");

        // A source-free 1D steady slab has a linear temperature and constant Fourier flux.
        const double temperature0K = 300.0;
        const double temperature1K = 350.0;
        const double lengthM = 0.5;
        const double conductivityWPerMK = 10.0;
        const double temperatureAtPointK = temperature0K + (temperature1K - temperature0K) * 0.2 / lengthM;
        const double heatFluxWPerM2 = -conductivityWPerMK * (temperature1K - temperature0K) / lengthM;
        require(close(temperatureAtPointK, 320.0, 1e-12) && close(heatFluxWPerM2, -1000.0, 1e-12),
                "linear heat-conduction analytical benchmark failed");

        // Uniaxial rod extension: delta = P L/(A E), epsilon = sigma/E.
        const double forceN = 1000.0;
        const double rodLengthM = 1.0;
        const double areaM2 = 1e-4;
        const double youngModulusPa = 200e9;
        const double extensionM = forceN * rodLengthM / (areaM2 * youngModulusPa);
        const double stressPa = forceN / areaM2;
        const double strain = stressPa / youngModulusPa;
        require(close(extensionM, 5e-5, 1e-15) && close(strain, extensionM / rodLengthM, 1e-15),
                "linear-elastic rod analytical benchmark failed");

        datumloop::SolverRequest request;
        request.requestId = "knowledge-test-1";
        request.equationDefinitionId = newton->id;
        request.designRevision = "design-revision-1";
        request.minimumMaturity = datumloop::ModelMaturity::E3;
        request.inputs = {{"m", {2.0, datumloop::UnitDimension::kilogram()}}};
        request.validate(registry);
        auto wrongUnitRequest = request;
        wrongUnitRequest.inputs.front().second.dimension = datumloop::UnitDimension::pascal();
        requireThrows([&]() { wrongUnitRequest.validate(registry); },
                      "solver request accepted a dimensionally incompatible input");
        auto underMatureRequest = request;
        underMatureRequest.minimumMaturity = datumloop::ModelMaturity::E4;
        requireThrows([&]() { underMatureRequest.validate(registry); },
                      "solver request accepted an equation below the requested maturity");

        datumloop::PhysicsSolverResult result;
        result.requestId = request.requestId;
        result.solverId = "test-analytical-backend";
        result.solverVersion = "1";
        result.status = datumloop::SolverResultStatus::Succeeded;
        result.fields.push_back({"acceleration", accelerationUnit(), 3.0, {}});
        result.validateFor(request);

        datumloop::TransientSolverRequest invalidTimeRequest;
        invalidTimeRequest.model = request;
        invalidTimeRequest.startTimeS = 0.0;
        invalidTimeRequest.endTimeS = 1.0;
        invalidTimeRequest.timeStepS = 0.0;
        requireThrows([&]() { invalidTimeRequest.validate(registry); },
                      "transient solver accepted a non-positive time step");

        const datumloop::CantileverBeamModel beam{
            1.0,       // m
            200e9,     // Pa
            1e-5,      // m^4
            2e7,       // N = kappa G A
            1000.0,    // N/m
        };
        const std::vector<int> meshSizes{1, 2, 4, 8, 16, 32};
        std::vector<datumloop::CantileverBeamResult> convergence;
        for (const int elements : meshSizes) convergence.push_back(datumloop::solveCantileverBeam(beam, elements));
        require(convergence.front().relativeError > convergence.back().relativeError &&
                    convergence.back().relativeError < 0.01,
                "cantilever FEM mesh refinement did not converge to the analytical Timoshenko result");
        for (std::size_t index = 1; index < convergence.size(); ++index) {
            require(convergence[index].relativeError < convergence[index - 1].relativeError,
                    "cantilever FEM error did not decrease monotonically under mesh refinement");
        }
        std::cout << std::setprecision(10) << "Cantilever uniform-load tip displacement (Timoshenko, SI):\n"
                  << "elements,numerical_m,analytical_m,relative_error\n";
        for (const auto& row : convergence) {
            std::cout << row.elementCount << ',' << row.tipDisplacementM << ','
                      << row.analyticalTipDisplacementM << ',' << row.relativeError << '\n';
        }

        const datumloop::CantileverReducedModel rom = datumloop::fitCantileverReducedModel(
            beam, 8, 32, {250.0, 500.0, 1000.0}, {350.0, 750.0});
        require(rom.trainingElementCount == 8 && rom.validationElementCount == 32 &&
                    rom.validationRelativeErrors.size() == 2 && rom.maximumRelativeValidationError < 0.01 &&
                    close(rom.predictTipDisplacement(500.0), rom.predictTipDisplacement(250.0) * 2.0, 1e-14),
                "linear beam reduced model did not reproduce held-out finite-element responses");
        std::cout << "beam_rom,training_elements=" << rom.trainingElementCount
                  << ",validation_elements=" << rom.validationElementCount
                  << ",max_heldout_relative_error=" << rom.maximumRelativeValidationError << '\n';

        std::cout << "Equation registry, units, analytical checks, generic solver contracts, cantilever FEM mesh convergence, and ROM validation passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Knowledge and verification test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
