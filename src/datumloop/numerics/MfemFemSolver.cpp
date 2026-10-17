#include "datumloop/numerics/MfemFemSolver.hpp"

#include <mfem.hpp>

#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

namespace datumloop {
namespace {

constexpr const char* kCantileverEquationId = "structural.timoshenko-beam.isotropic.v1";
constexpr const char* kCantileverBenchmarkId = "timoshenko-cantilever-uniform-load-v1";
constexpr double kAbsoluteTolerance = 1e-14;
constexpr double kRelativeTolerance = 1e-12;

std::string mfemVersion()
{
    return std::to_string(MFEM_VERSION_MAJOR) + "." + std::to_string(MFEM_VERSION_MINOR) + "." +
           std::to_string(MFEM_VERSION_PATCH);
}

}  // namespace

std::string MfemCantileverSolver::solverId() const
{
    return "mfem.serial-h1-timoshenko-cantilever";
}

std::string MfemCantileverSolver::solverVersion() const
{
    return mfemVersion();
}

FemSolverResult MfemCantileverSolver::solveCantilever(const CantileverBeamModel& model,
                                                       int elementCount) const
{
    model.validate();
    if (elementCount <= 0 || elementCount > 256) {
        throw std::invalid_argument("MFEM cantilever element count must be in [1, 256]");
    }

    const auto start = std::chrono::steady_clock::now();
    mfem::Mesh mesh = mfem::Mesh::MakeCartesian1D(elementCount, model.lengthM);
    constexpr int polynomialOrder = 2;
    mfem::H1_FECollection collection(polynomialOrder, mesh.Dimension());
    mfem::FiniteElementSpace space(&mesh, &collection);
    const int scalarDofCount = space.GetVSize();
    const int totalDofCount = 2 * scalarDofCount;
    mfem::SparseMatrix stiffness(totalDofCount);
    mfem::Vector load(totalDofCount);
    load = 0.0;

    const double bendingRigidity = model.youngModulusPa * model.secondMomentOfAreaM4;
    const mfem::IntegrationRule& quadrature = mfem::IntRules.Get(mfem::Geometry::SEGMENT, 2 * polynomialOrder + 2);
    mfem::Array<int> scalarDofs;
    mfem::Vector shape;
    mfem::DenseMatrix physicalDerivatives;
    for (int element = 0; element < mesh.GetNE(); ++element) {
        const mfem::FiniteElement* finiteElement = space.GetFE(element);
        if (finiteElement == nullptr) throw std::runtime_error("MFEM did not provide an H1 element");
        space.GetElementDofs(element, scalarDofs);
        const int localScalarDofCount = scalarDofs.Size();
        shape.SetSize(localScalarDofCount);
        physicalDerivatives.SetSize(localScalarDofCount, mesh.Dimension());
        mfem::ElementTransformation* transformation = space.GetElementTransformation(element);
        if (transformation == nullptr) throw std::runtime_error("MFEM did not provide an element transformation");

        for (int pointIndex = 0; pointIndex < quadrature.GetNPoints(); ++pointIndex) {
            const mfem::IntegrationPoint& point = quadrature.IntPoint(pointIndex);
            transformation->SetIntPoint(&point);
            finiteElement->CalcShape(point, shape);
            finiteElement->CalcPhysDShape(*transformation, physicalDerivatives);
            const double integrationWeight = point.weight * transformation->Weight();
            for (int localRow = 0; localRow < localScalarDofCount; ++localRow) {
                const int rowNode = scalarDofs[localRow];
                const int displacementRow = 2 * rowNode;
                const int rotationRow = displacementRow + 1;
                const double displacementDerivativeRow = physicalDerivatives(localRow, 0);
                const double rotationShapeRow = shape(localRow);
                const double rotationDerivativeRow = physicalDerivatives(localRow, 0);

                load[displacementRow] += model.distributedLoadNPerM * rotationShapeRow * integrationWeight;

                for (int localColumn = 0; localColumn < localScalarDofCount; ++localColumn) {
                    const int columnNode = scalarDofs[localColumn];
                    const int displacementColumn = 2 * columnNode;
                    const int rotationColumn = displacementColumn + 1;
                    const double displacementDerivativeColumn = physicalDerivatives(localColumn, 0);
                    const double rotationShapeColumn = shape(localColumn);
                    const double rotationDerivativeColumn = physicalDerivatives(localColumn, 0);
                    const double shearStiffness = model.shearStiffnessN * integrationWeight;
                    const double bendingStiffness = bendingRigidity * integrationWeight;

                    stiffness.Add(displacementRow, displacementColumn,
                                  shearStiffness * displacementDerivativeRow * displacementDerivativeColumn);
                    stiffness.Add(displacementRow, rotationColumn,
                                  -shearStiffness * displacementDerivativeRow * rotationShapeColumn);
                    stiffness.Add(rotationRow, displacementColumn,
                                  -shearStiffness * rotationShapeRow * displacementDerivativeColumn);
                    stiffness.Add(rotationRow, rotationColumn,
                                  shearStiffness * rotationShapeRow * rotationShapeColumn +
                                      bendingStiffness * rotationDerivativeRow * rotationDerivativeColumn);
                }
            }
        }
    }
    stiffness.Finalize();

    // The root is vertex zero in MFEM's Cartesian 1D mesh. Clamp both w and theta.
    mfem::Array<int> rootDofs;
    space.GetVertexDofs(0, rootDofs);
    if (rootDofs.Size() != 1) throw std::runtime_error("MFEM H1 root vertex should have one scalar degree of freedom");
    stiffness.EliminateRowCol(2 * rootDofs[0], 0.0, load);
    stiffness.EliminateRowCol(2 * rootDofs[0] + 1, 0.0, load);

    mfem::Vector solution(totalDofCount);
    solution = 0.0;
    mfem::GSSmoother preconditioner(stiffness, mfem::GSSmoother::SYMMETRIC);
    mfem::CGSolver cg;
    cg.SetOperator(stiffness);
    cg.SetPreconditioner(preconditioner);
    cg.SetAbsTol(kAbsoluteTolerance);
    cg.SetRelTol(kRelativeTolerance);
    cg.SetMaxIter(10000);
    cg.SetPrintLevel(-1);
    cg.Mult(load, solution);
    const bool converged = cg.GetConverged();
    const double relativeResidual = cg.GetFinalRelNorm();
    if (!converged || !std::isfinite(relativeResidual) || relativeResidual < 0.0) {
        throw std::runtime_error("MFEM conjugate-gradient solve did not converge to a finite residual");
    }

    mfem::Array<int> tipDofs;
    space.GetVertexDofs(mesh.GetNV() - 1, tipDofs);
    if (tipDofs.Size() != 1) throw std::runtime_error("MFEM H1 tip vertex should have one scalar degree of freedom");
    const double tipDisplacement = solution[2 * tipDofs[0]];
    const double analytical = model.distributedLoadNPerM * std::pow(model.lengthM, 4) /
                                  (8.0 * bendingRigidity) +
                              model.distributedLoadNPerM * model.lengthM * model.lengthM /
                                  (2.0 * model.shearStiffnessN);
    double strainEnergy = 0.0;
    for (int dof = 0; dof < totalDofCount; ++dof) strainEnergy += 0.5 * solution[dof] * load[dof];

    const auto stop = std::chrono::steady_clock::now();
    FemSolverResult result;
    result.solverId = solverId();
    result.solverVersion = solverVersion();
    result.elementCount = elementCount;
    result.tipDisplacementM = tipDisplacement;
    result.analyticalTipDisplacementM = analytical;
    result.relativeError = std::abs(tipDisplacement - analytical) / std::abs(analytical);
    result.strainEnergyJ = strainEnergy;
    result.relativeResidual = relativeResidual;
    result.executionTimeSeconds = std::chrono::duration<double>(stop - start).count();
    result.converged = converged;
    result.provenance = {kCantileverEquationId, "1", ModelMaturity::E4, "", "",
                         result.solverId, result.solverVersion,
                         std::to_string(elementCount) + " uniform line elements; " +
                             std::to_string(scalarDofCount) + " H1 scalar degrees of freedom per field",
                         "MFEM H1 Lagrange", polynomialOrder, kAbsoluteTolerance, kRelativeTolerance,
                         converged, kCantileverBenchmarkId, "analytical-comparison-recorded", "cantilever-model-v1"};
    result.validate();
    return result;
}

}  // namespace datumloop
