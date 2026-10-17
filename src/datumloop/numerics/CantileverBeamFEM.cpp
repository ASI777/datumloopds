#include "datumloop/numerics/CantileverBeamFEM.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace datumloop {
namespace {

std::vector<double> solveDense(std::vector<std::vector<double>> matrix, std::vector<double> rhs)
{
    const std::size_t size = rhs.size();
    if (matrix.size() != size) throw std::invalid_argument("linear system dimensions do not match");
    for (std::size_t column = 0; column < size; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < size; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
        }
        if (std::abs(matrix[pivot][column]) <= 1e-18) {
            throw std::runtime_error("cantilever beam FEM produced a singular stiffness matrix");
        }
        std::swap(matrix[pivot], matrix[column]);
        std::swap(rhs[pivot], rhs[column]);
        const double diagonal = matrix[column][column];
        for (std::size_t entry = column; entry < size; ++entry) matrix[column][entry] /= diagonal;
        rhs[column] /= diagonal;
        for (std::size_t row = 0; row < size; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (std::size_t entry = column; entry < size; ++entry) {
                matrix[row][entry] -= factor * matrix[column][entry];
            }
            rhs[row] -= factor * rhs[column];
        }
    }
    return rhs;
}

}  // namespace

void CantileverBeamModel::validate() const
{
    if (!std::isfinite(lengthM) || lengthM <= 0.0 ||
        !std::isfinite(youngModulusPa) || youngModulusPa <= 0.0 ||
        !std::isfinite(secondMomentOfAreaM4) || secondMomentOfAreaM4 <= 0.0 ||
        !std::isfinite(shearStiffnessN) || shearStiffnessN <= 0.0 ||
        !std::isfinite(distributedLoadNPerM) || distributedLoadNPerM <= 0.0) {
        throw std::invalid_argument("cantilever beam properties and positive distributed load are required in SI units");
    }
}

CantileverBeamResult solveCantileverBeam(const CantileverBeamModel& model, int elementCount)
{
    model.validate();
    if (elementCount <= 0 || elementCount > 256) {
        throw std::invalid_argument("cantilever beam element count must be in [1, 256]");
    }
    const int nodeCount = elementCount + 1;
    const int degreeCount = 2 * nodeCount;
    const double elementLength = model.lengthM / static_cast<double>(elementCount);
    const double bendingRigidity = model.youngModulusPa * model.secondMomentOfAreaM4;
    std::vector<std::vector<double>> stiffness(static_cast<std::size_t>(degreeCount),
                                               std::vector<double>(static_cast<std::size_t>(degreeCount), 0.0));
    std::vector<double> load(static_cast<std::size_t>(degreeCount), 0.0);
    constexpr std::array<double, 2> gaussPoints{0.21132486540518713, 0.7886751345948129};

    for (int element = 0; element < elementCount; ++element) {
        const int firstNode = element;
        const int secondNode = element + 1;
        const std::array<int, 4> dofs{2 * firstNode, 2 * firstNode + 1,
                                      2 * secondNode, 2 * secondNode + 1};
        std::array<std::array<double, 4>, 4> local{};
        const double bend = bendingRigidity / elementLength;
        local[1][1] += bend;
        local[1][3] -= bend;
        local[3][1] -= bend;
        local[3][3] += bend;

        for (const double point : gaussPoints) {
            const double n1 = 1.0 - point;
            const double n2 = point;
            const std::array<double, 4> shearStrain{-1.0 / elementLength, -n1,
                                                    1.0 / elementLength, -n2};
            const double integrationWeight = 0.5 * elementLength;
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    local[row][column] += model.shearStiffnessN * integrationWeight *
                        shearStrain[row] * shearStrain[column];
                }
            }
        }

        const double nodalLoad = model.distributedLoadNPerM * elementLength / 2.0;
        load[static_cast<std::size_t>(dofs[0])] += nodalLoad;
        load[static_cast<std::size_t>(dofs[2])] += nodalLoad;
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                stiffness[static_cast<std::size_t>(dofs[row])][static_cast<std::size_t>(dofs[column])] += local[row][column];
            }
        }
    }

    // Eliminate the fixed displacement and rotation at the root.
    const std::size_t freeCount = static_cast<std::size_t>(degreeCount - 2);
    std::vector<std::vector<double>> reduced(freeCount, std::vector<double>(freeCount, 0.0));
    std::vector<double> reducedLoad(freeCount, 0.0);
    for (std::size_t row = 0; row < freeCount; ++row) {
        reducedLoad[row] = load[row + 2];
        for (std::size_t column = 0; column < freeCount; ++column) {
            reduced[row][column] = stiffness[row + 2][column + 2];
        }
    }
    const std::vector<double> displacement = solveDense(std::move(reduced), std::move(reducedLoad));
    const double tipDisplacement = displacement[displacement.size() - 2];
    const double analytical = model.distributedLoadNPerM * std::pow(model.lengthM, 4) /
                                  (8.0 * bendingRigidity) +
                              model.distributedLoadNPerM * model.lengthM * model.lengthM /
                                  (2.0 * model.shearStiffnessN);
    return {elementCount, tipDisplacement, analytical, std::abs(tipDisplacement - analytical) / analytical};
}

double CantileverReducedModel::predictTipDisplacement(double distributedLoadNPerM) const
{
    if (!std::isfinite(distributedLoadNPerM) || distributedLoadNPerM < 0.0 ||
        !std::isfinite(complianceMPerNPerM) || complianceMPerNPerM <= 0.0) {
        throw std::invalid_argument("reduced beam model requires a non-negative load and positive finite compliance");
    }
    return complianceMPerNPerM * distributedLoadNPerM;
}

CantileverReducedModel fitCantileverReducedModel(const CantileverBeamModel& model,
                                                  int trainingElementCount,
                                                  int validationElementCount,
                                                  const std::vector<double>& trainingLoadsNPerM,
                                                  const std::vector<double>& validationLoadsNPerM)
{
    model.validate();
    if (trainingLoadsNPerM.empty() || validationLoadsNPerM.empty() || trainingElementCount <= 0 ||
        validationElementCount <= 0) {
        throw std::invalid_argument("reduced beam model needs training and validation loads and meshes");
    }
    double numerator = 0.0;
    double denominator = 0.0;
    for (const double sampleLoad : trainingLoadsNPerM) {
        if (!std::isfinite(sampleLoad) || sampleLoad <= 0.0) {
            throw std::invalid_argument("reduced beam training loads must be finite and positive");
        }
        CantileverBeamModel sample = model;
        sample.distributedLoadNPerM = sampleLoad;
        const double response = solveCantileverBeam(sample, trainingElementCount).tipDisplacementM;
        numerator += sampleLoad * response;
        denominator += sampleLoad * sampleLoad;
    }
    CantileverReducedModel result;
    result.complianceMPerNPerM = numerator / denominator;
    result.trainingElementCount = trainingElementCount;
    result.validationElementCount = validationElementCount;
    for (const double validationLoad : validationLoadsNPerM) {
        if (!std::isfinite(validationLoad) || validationLoad <= 0.0) {
            throw std::invalid_argument("reduced beam validation loads must be finite and positive");
        }
        CantileverBeamModel reference = model;
        reference.distributedLoadNPerM = validationLoad;
        const double highFidelity = solveCantileverBeam(reference, validationElementCount).tipDisplacementM;
        const double prediction = result.predictTipDisplacement(validationLoad);
        const double relativeError = std::abs(prediction - highFidelity) / std::abs(highFidelity);
        result.validationRelativeErrors.push_back(relativeError);
        result.maximumRelativeValidationError = std::max(result.maximumRelativeValidationError, relativeError);
    }
    return result;
}

}  // namespace datumloop
