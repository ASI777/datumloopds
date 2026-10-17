#include "datumloop/numerics/MfemFemSolver.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
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

datumloop::CantileverBeamModel benchmarkModel(double loadNPerM = 1000.0)
{
    return {1.0, 200e9, 1e-5, 2e7, loadNPerM};
}

void writeReport(const std::filesystem::path& path,
                 const datumloop::IFemSolver& mfemSolver,
                 const std::vector<datumloop::FemSolverResult>& mfemResults,
                 const std::vector<datumloop::FemSolverResult>& referenceResults,
                 const datumloop::CantileverReducedModel& rom)
{
    if (mfemResults.size() != referenceResults.size()) throw std::logic_error("benchmark report mesh counts differ");
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) throw std::runtime_error("cannot create MFEM benchmark report");
    output << std::setprecision(17)
           << "{\n  \"benchmark_id\": \"timoshenko-cantilever-uniform-load-v1\",\n"
           << "  \"equation_definition_id\": \"structural.timoshenko-beam.isotropic.v1\",\n"
           << "  \"equation_maturity\": \"E4\",\n"
           << "  \"units\": \"SI\",\n"
           << "  \"model\": {\"length_m\": 1, \"young_modulus_pa\": 200000000000, \"second_moment_m4\": 0.00001, \"shear_stiffness_n\": 20000000, \"distributed_load_n_per_m\": 1000},\n"
           << "  \"high_fidelity_solver\": \"" << mfemSolver.solverId() << "\",\n"
           << "  \"high_fidelity_version\": \"" << mfemSolver.solverVersion() << "\",\n"
           << "  \"reference_solver\": \"" << referenceResults.front().solverId << "\",\n"
           << "  \"reference_version\": \"" << referenceResults.front().solverVersion << "\",\n"
           << "  \"mesh_refinement\": [\n";
    for (std::size_t index = 0; index < mfemResults.size(); ++index) {
        const auto& mfem = mfemResults[index];
        const auto& reference = referenceResults[index];
        const double relativeDifference = std::abs(mfem.tipDisplacementM - reference.tipDisplacementM) /
                                          std::abs(mfem.analyticalTipDisplacementM);
        output << "    {\"elements\": " << mfem.elementCount
               << ", \"analytical_tip_m\": " << mfem.analyticalTipDisplacementM
               << ", \"mfem_tip_m\": " << mfem.tipDisplacementM
               << ", \"mfem_relative_error\": " << mfem.relativeError
               << ", \"mfem_relative_residual\": " << mfem.relativeResidual
               << ", \"mfem_strain_energy_j\": " << mfem.strainEnergyJ
               << ", \"mfem_execution_seconds\": " << mfem.executionTimeSeconds
               << ", \"mfem_converged\": " << (mfem.converged ? "true" : "false")
               << ", \"reference_tip_m\": " << reference.tipDisplacementM
               << ", \"reference_relative_error\": " << reference.relativeError
               << ", \"relative_difference_vs_reference\": " << relativeDifference << "}"
               << (index + 1 == mfemResults.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"rom\": {\"id\": \"" << rom.id << "\", \"source_solver\": \""
           << rom.highFidelitySolverId << "\", \"source_version\": \"" << rom.highFidelitySolverVersion
           << "\", \"training_mesh_elements\": " << rom.trainingElementCount
           << ", \"validation_mesh_elements\": " << rom.validationElementCount
           << ", \"training_loads_n_per_m\": [";
    for (std::size_t index = 0; index < rom.trainingLoadsNPerM.size(); ++index) {
        output << (index == 0 ? "" : ", ") << rom.trainingLoadsNPerM[index];
    }
    output << "], \"held_out_samples\": [";
    for (std::size_t index = 0; index < rom.heldOutSamples.size(); ++index) {
        const auto& sample = rom.heldOutSamples[index];
        output << (index == 0 ? "" : ", ") << "{\"load_n_per_m\": " << sample.inputLoadNPerM
               << ", \"rom_prediction_m\": " << sample.reducedPredictionM
               << ", \"mfem_reference_m\": " << sample.highFidelityResponseM
               << ", \"relative_error\": " << sample.relativeError << "}";
    }
    output << "], \"maximum_held_out_relative_error\": " << rom.maximumRelativeValidationError
           << ", \"rms_held_out_relative_error\": " << rom.rmsRelativeValidationError
           << "},\n  \"status\": \"passed\"\n}\n";
    if (!output) throw std::runtime_error("cannot finish writing MFEM benchmark report");
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        datumloop::MfemCantileverSolver mfemSolver;
        datumloop::DatumReferenceFemSolver referenceSolver;
        const auto baseModel = benchmarkModel();
        const std::vector<int> meshSizes{1, 2, 4, 8, 16, 32};
        std::vector<datumloop::FemSolverResult> mfemResults;
        std::vector<datumloop::FemSolverResult> referenceResults;
        for (int elements : meshSizes) {
            const auto mfem = mfemSolver.solveCantilever(baseModel, elements);
            const auto reference = referenceSolver.solveCantilever(baseModel, elements);
            require(mfem.converged && mfem.relativeResidual < 1e-8 && mfem.strainEnergyJ > 0.0,
                    "MFEM solve must converge with a recorded residual and positive strain energy");
            require(mfem.provenance.elementFamily == "MFEM H1 Lagrange" &&
                        mfem.provenance.polynomialOrder == 2 && mfem.provenance.backendId == mfemSolver.solverId(),
                    "MFEM result omitted backend or discretization provenance");
            mfemResults.push_back(mfem);
            referenceResults.push_back(reference);
        }
        require(mfemResults.back().relativeError < 0.01 &&
                    mfemResults.back().relativeError < mfemResults.front().relativeError,
                "MFEM cantilever mesh refinement did not approach the analytical Timoshenko solution");
        for (std::size_t index = 1; index < mfemResults.size(); ++index) {
            const double previousReferenceDifference =
                std::abs(mfemResults[index - 1].tipDisplacementM - referenceResults[index - 1].tipDisplacementM) /
                mfemResults[index - 1].analyticalTipDisplacementM;
            const double currentReferenceDifference =
                std::abs(mfemResults[index].tipDisplacementM - referenceResults[index].tipDisplacementM) /
                mfemResults[index].analyticalTipDisplacementM;
            require(mfemResults[index].relativeError < mfemResults[index - 1].relativeError &&
                        currentReferenceDifference < previousReferenceDifference,
                    "MFEM analytical and independent-reference differences must decrease with mesh refinement");
        }
        require(std::abs(mfemResults.back().tipDisplacementM - referenceResults.back().tipDisplacementM) /
                    mfemResults.back().analyticalTipDisplacementM < 0.01,
                "MFEM and independent DatumLoop reference backends disagree on the refined cantilever");

        const auto rom = datumloop::fitCantileverReducedModel(
            mfemSolver, baseModel, 8, 32, {250.0, 500.0, 1000.0}, {350.0, 750.0});
        require(rom.highFidelitySolverId == mfemSolver.solverId() &&
                    rom.highFidelitySolverVersion == mfemSolver.solverVersion() &&
                    rom.trainingLoadsNPerM.size() == 3 && rom.heldOutSamples.size() == 2 &&
                    rom.sourceLengthM == baseModel.lengthM &&
                    rom.sourceYoungModulusPa == baseModel.youngModulusPa &&
                    rom.sourceSecondMomentOfAreaM4 == baseModel.secondMomentOfAreaM4 &&
                    rom.sourceShearStiffnessN == baseModel.shearStiffnessN &&
                    rom.maximumRelativeValidationError < 0.01 && rom.rmsRelativeValidationError < 0.01,
                "MFEM-trained reduced model failed its held-out high-fidelity validation");
        bool rejectedExtrapolation = false;
        try {
            (void)rom.predictTipDisplacement(1250.0);
        } catch (const std::out_of_range&) {
            rejectedExtrapolation = true;
        }
        require(rejectedExtrapolation, "MFEM-trained reduced model allowed an out-of-domain prediction");

        if (argc == 3 && std::string(argv[1]) == "--report") {
            writeReport(argv[2], mfemSolver, mfemResults, referenceResults, rom);
        } else if (argc != 1) {
            throw std::invalid_argument("usage: datumloop-mfem-tests [--report path]");
        }
        std::cout << "MFEM " << mfemSolver.solverVersion()
                  << " serial H1 Timoshenko cantilever, analytical/refinement comparison, and held-out ROM validation passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "MFEM cantilever test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
