#include <mfem.hpp>

#include "datumloop/numerics/MfemLinearElasticitySolver.hpp"

#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/knowledge/EquationRegistry.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace datumloop {
namespace {

constexpr const char* kElasticityEquationId = "structural.linear-elasticity.3d.isotropic-small-strain.v1";

struct IsotropicMaterial {
    double densityKgPerM3 = 0.0;
    double youngsModulusPa = 0.0;
    double poissonRatio = 0.0;
    double lambdaPa = 0.0;
    double shearModulusPa = 0.0;
    std::optional<double> temperatureK;
    bool temperatureRangeVerified = false;
    std::string revision;
};

double norm(const Vector3& value)
{
    return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

Vector3 add(const Vector3& left, const Vector3& right)
{
    return {left[0] + right[0], left[1] + right[1], left[2] + right[2]};
}

Vector3 scaled(const Vector3& value, double factor)
{
    return {value[0] * factor, value[1] * factor, value[2] * factor};
}

std::string materialFingerprint(const MaterialDefinition& material)
{
    std::vector<MaterialSource> sources = material.sources;
    std::sort(sources.begin(), sources.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    std::vector<MaterialPropertyModel> properties = material.propertyModels;
    std::stable_sort(properties.begin(), properties.end(), [](const auto& left, const auto& right) {
        return std::tie(left.propertyName, left.propertyUnit) <
               std::tie(right.propertyName, right.propertyUnit);
    });
    QJsonArray compositionArray;
    for (const MaterialConstituent& constituent : material.composition) {
        compositionArray.append(QJsonObject{
            {QStringLiteral("chemicalSymbol"), QString::fromStdString(constituent.chemicalSymbol)},
            {QStringLiteral("massFraction"), constituent.massFraction},
        });
    }
    QJsonArray phaseArray;
    for (const MaterialPhaseDefinition& phase : material.phases) {
        phaseArray.append(QJsonObject{
            {QStringLiteral("name"), QString::fromStdString(phase.name)},
            {QStringLiteral("crystalStructure"), QString::fromStdString(phase.crystalStructure)},
        });
    }
    QJsonArray materialProvenanceArray;
    for (const std::string& entry : material.provenance) {
        materialProvenanceArray.append(QString::fromStdString(entry));
    }
    QJsonArray processHistoryArray;
    for (const MaterialProcessHistoryEntry& entry : material.processHistory) {
        const auto encodeOptional = [](const std::optional<double>& value) {
            return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
        };
        processHistoryArray.append(QJsonObject{
            {QStringLiteral("process"), QString::fromStdString(entry.process)},
            {QStringLiteral("recordedAt"), QString::fromStdString(entry.recordedAt)},
            {QStringLiteral("temperatureK"), encodeOptional(entry.temperatureK)},
            {QStringLiteral("pressurePa"), encodeOptional(entry.pressurePa)},
            {QStringLiteral("durationS"), encodeOptional(entry.durationS)},
            {QStringLiteral("provenance"), QString::fromStdString(entry.provenance)},
        });
    }
    QJsonValue microstructureValue(QJsonValue::Null);
    if (material.microstructure.has_value()) {
        const MaterialMicrostructure& value = *material.microstructure;
        const auto encodeOptional = [](const std::optional<double>& item) {
            return item.has_value() ? QJsonValue(*item) : QJsonValue(QJsonValue::Null);
        };
        microstructureValue = QJsonObject{
            {QStringLiteral("grainSizeM"), encodeOptional(value.grainSizeM)},
            {QStringLiteral("porosityFraction"), encodeOptional(value.porosityFraction)},
            {QStringLiteral("defectDensityPerM2"), encodeOptional(value.defectDensityPerM2)},
            {QStringLiteral("residualStressPa"), encodeOptional(value.residualStressPa)},
            {QStringLiteral("textureDescription"), QString::fromStdString(value.textureDescription)},
        };
    }
    QJsonArray sourceArray;
    for (const MaterialSource& source : sources) {
        sourceArray.append(QJsonObject{
            {QStringLiteral("id"), QString::fromStdString(source.id)},
            {QStringLiteral("kind"), QString::fromStdString(materialSourceKindName(source.kind))},
            {QStringLiteral("title"), QString::fromStdString(source.title)},
            {QStringLiteral("publisher"), QString::fromStdString(source.publisher)},
            {QStringLiteral("version"), QString::fromStdString(source.version)},
            {QStringLiteral("reference"), QString::fromStdString(source.reference)},
            {QStringLiteral("url"), QString::fromStdString(source.url)},
        });
    }
    QJsonArray propertyArray;
    for (const MaterialPropertyModel& property : properties) {
        QJsonObject encoded{
            {QStringLiteral("name"), QString::fromStdString(property.propertyName)},
            {QStringLiteral("unit"), QString::fromStdString(property.propertyUnit)},
            {QStringLiteral("law"), property.law == MaterialPropertyLaw::Constant
                                         ? QStringLiteral("constant") : QStringLiteral("piecewise_linear")},
            {QStringLiteral("source"), QString::fromStdString(property.source)},
            {QStringLiteral("sourceVersion"), QString::fromStdString(property.sourceVersion)},
            {QStringLiteral("independentVariable"), QString::fromStdString(property.independentVariable)},
            {QStringLiteral("independentUnit"), QString::fromStdString(property.independentUnit)},
        };
        if (property.constantValue.has_value()) encoded.insert(QStringLiteral("value"), *property.constantValue);
        if (property.sourceId.has_value()) encoded.insert(QStringLiteral("sourceId"), QString::fromStdString(*property.sourceId));
        if (property.absoluteUncertainty.has_value()) encoded.insert(QStringLiteral("uncertainty"), *property.absoluteUncertainty);
        if (property.confidence.has_value()) encoded.insert(QStringLiteral("confidence"), *property.confidence);
        if (property.minimumTemperatureK.has_value()) encoded.insert(QStringLiteral("minimumTemperatureK"), *property.minimumTemperatureK);
        if (property.maximumTemperatureK.has_value()) encoded.insert(QStringLiteral("maximumTemperatureK"), *property.maximumTemperatureK);
        QJsonArray table;
        for (const MaterialPropertyPoint& point : property.table) {
            table.append(QJsonArray{point.independentValue, point.propertyValue});
        }
        encoded.insert(QStringLiteral("table"), table);
        propertyArray.append(encoded);
    }
    const QJsonObject canonical{
        {QStringLiteral("id"), QString::fromStdString(material.id)},
        {QStringLiteral("name"), QString::fromStdString(material.name)},
        {QStringLiteral("crystalStructure"), QString::fromStdString(material.crystalStructure)},
        {QStringLiteral("composition"), compositionArray},
        {QStringLiteral("phases"), phaseArray},
        {QStringLiteral("microstructure"), microstructureValue},
        {QStringLiteral("processHistory"), processHistoryArray},
        {QStringLiteral("provenance"), materialProvenanceArray},
        {QStringLiteral("sources"), sourceArray},
        {QStringLiteral("properties"), propertyArray},
    };
    const QByteArray digest = QCryptographicHash::hash(
        QJsonDocument(canonical).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    return "sha256:" + std::string(digest.constData(), static_cast<std::size_t>(digest.size()));
}

const MaterialPropertyModel& findProperty(const MaterialDefinition& material,
                                          std::string_view name,
                                          std::string_view unit)
{
    const MaterialPropertyModel* found = nullptr;
    for (const MaterialPropertyModel& property : material.propertyModels) {
        if (property.propertyName != name) continue;
        if (found != nullptr) {
            throw std::invalid_argument("material contains duplicate structural property '" + name + "'");
        }
        found = &property;
    }
    if (found == nullptr || found->propertyUnit != unit ||
        found->law != MaterialPropertyLaw::Constant || !found->constantValue.has_value() ||
        !std::isfinite(*found->constantValue)) {
        throw std::invalid_argument("structural solve requires sourced constant material property '" +
                                    std::string(name) + "' in " + std::string(unit));
    }
    if (!found->sourceId.has_value() || std::none_of(material.sources.begin(), material.sources.end(),
            [&found](const MaterialSource& source) { return source.id == *found->sourceId; })) {
        throw std::invalid_argument("structural material property '" + std::string(name) +
                                    "' must reference a MaterialSource");
    }
    return *found;
}

IsotropicMaterial resolveMaterial(const DatumDocument& document, const StructuralAnalysisStudy& study)
{
    const MaterialDefinition* definition = document.electronicsModel().material(study.materialDefinitionId);
    if (definition == nullptr) {
        throw std::invalid_argument("structural study material is missing from the DatumLoop material catalog");
    }
    definition->validate();

    const MaterialPropertyModel& density = findProperty(*definition, "mass.density", "kg/m^3");
    const MaterialPropertyModel& youngs = findProperty(*definition, "elastic.youngs_modulus", "Pa");
    const MaterialPropertyModel& poisson = findProperty(*definition, "elastic.poisson_ratio", "1");

    IsotropicMaterial result;
    result.densityKgPerM3 = *density.constantValue;
    result.youngsModulusPa = *youngs.constantValue;
    result.poissonRatio = *poisson.constantValue;
    if (!std::isfinite(result.densityKgPerM3) || result.densityKgPerM3 <= 0.0 ||
        !std::isfinite(result.youngsModulusPa) || result.youngsModulusPa <= 0.0 ||
        !std::isfinite(result.poissonRatio) || result.poissonRatio <= -1.0 || result.poissonRatio >= 0.5) {
        throw std::invalid_argument("material density, Young's modulus, and Poisson ratio are outside the stable isotropic range");
    }

    const MaterialState* state = nullptr;
    if (study.materialStateId.has_value()) {
        const auto found = std::find_if(document.electronicsModel().materialStates.begin(),
                                        document.electronicsModel().materialStates.end(),
                                        [&study](const MaterialState& item) { return item.id == *study.materialStateId; });
        if (found == document.electronicsModel().materialStates.end() ||
            found->materialDefinitionId != definition->id) {
            throw std::invalid_argument("structural study material state is missing or belongs to a different material");
        }
        found->validate();
        state = &*found;
        result.temperatureK = state->temperatureK;
    }
    const std::array<const MaterialPropertyModel*, 3> properties{&density, &youngs, &poisson};
    result.temperatureRangeVerified = result.temperatureK.has_value();
    for (const MaterialPropertyModel* property : properties) {
        if (result.temperatureK.has_value()) {
            if (property->minimumTemperatureK.has_value() &&
                *result.temperatureK < *property->minimumTemperatureK) {
                throw std::invalid_argument("analysis temperature is below the sourced material property range");
            }
            if (property->maximumTemperatureK.has_value() &&
                *result.temperatureK > *property->maximumTemperatureK) {
                throw std::invalid_argument("analysis temperature is above the sourced material property range");
            }
        }
        if (!property->minimumTemperatureK.has_value() || !property->maximumTemperatureK.has_value()) {
            result.temperatureRangeVerified = false;
        }
    }
    result.lambdaPa = result.youngsModulusPa * result.poissonRatio /
                      ((1.0 + result.poissonRatio) * (1.0 - 2.0 * result.poissonRatio));
    result.shearModulusPa = result.youngsModulusPa / (2.0 * (1.0 + result.poissonRatio));
    if (!std::isfinite(result.lambdaPa) || !std::isfinite(result.shearModulusPa) ||
        result.lambdaPa <= 0.0 || result.shearModulusPa <= 0.0) {
        throw std::invalid_argument("isotropic material produced invalid Lamé parameters");
    }
    result.revision = materialFingerprint(*definition);
    return result;
}

const AnalysisBoundaryRegion& boundaryRegion(const AnalysisMesh& mesh, const std::string& topologyId)
{
    const auto found = std::find_if(mesh.boundaryRegions.begin(), mesh.boundaryRegions.end(),
                                    [&topologyId](const AnalysisBoundaryRegion& item) {
                                        return item.topologyReferenceId == topologyId;
                                    });
    if (found == mesh.boundaryRegions.end()) {
        throw std::invalid_argument("physics condition face reference was not regenerated into the analysis mesh");
    }
    return *found;
}

int maximumBoundaryAttribute(const AnalysisMesh& mesh)
{
    int maximum = 0;
    for (const AnalysisBoundaryRegion& region : mesh.boundaryRegions) {
        maximum = std::max(maximum, region.attribute);
    }
    if (maximum <= 0) throw std::invalid_argument("analysis mesh has no attributed boundary regions");
    return maximum;
}

void validateMeshAgainstCurrentCad(const DatumDocument& document, const AnalysisMesh& mesh)
{
    if (!mesh.tetrahedra.empty()) {
        validateTetrahedralMeshAgainstCad(document, mesh, 0.01, 0.01);
        return;
    }
    AnalysisMeshSettings settings;
    settings.maximumElementSizeM = mesh.provenance.requestedMaximumElementSizeM;
    settings.maximumElementAspectRatio = mesh.provenance.maximumAspectRatio;
    settings.maximumElementCount = mesh.provenance.elementCount;
    AnalysisMesh regenerated = OcctPrismAnalysisMesher::generate(document, mesh.cadObjectId, settings);
    regenerated.provenance.revisionId = mesh.provenance.revisionId;
    if (regenerated != mesh) {
        throw std::invalid_argument("analysis mesh does not match the current CAD-derived structured hex mesh");
    }
}

std::string utcTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
}

}  // namespace

std::string MfemLinearElasticitySolver::solverId() const
{
    return "mfem.serial-h1-q1-3d-linear-elasticity";
}

std::string MfemLinearElasticitySolver::solverVersion() const
{
    return std::to_string(MFEM_VERSION_MAJOR) + "." + std::to_string(MFEM_VERSION_MINOR) + "." +
           std::to_string(MFEM_VERSION_PATCH);
}

StructuralAnalysisRun MfemLinearElasticitySolver::solve(
    const DatumDocument& document,
    const StructuralAnalysisStudy& study,
    const AnalysisMesh& mesh,
    const LinearElasticitySolverSettings& settings) const
{
    const bool tetrahedral = !mesh.tetrahedra.empty();
    if (tetrahedral && !settings.sectionStressSamplePositionM)
        throw std::invalid_argument("tetrahedral elasticity requires an explicit material stress sample point");
    if (settings.sectionStressSamplePositionM &&
        !std::all_of(settings.sectionStressSamplePositionM->begin(), settings.sectionStressSamplePositionM->end(),
                     [](double x) { return std::isfinite(x); }))
        throw std::invalid_argument("stress sample point must contain finite SI coordinates");
    study.validate();
    mesh.validate();
    if (mesh.cadObjectId != study.cadObjectId || mesh.sourceDocumentRevision != document.revision()) {
        throw std::invalid_argument("analysis mesh is stale or belongs to a different study CAD object");
    }
    const DatumObject* object = document.object(study.cadObjectId);
    if (object == nullptr || object->shape.IsNull() || !OcctKernel::isValid(object->shape)) {
        throw std::invalid_argument("structural solve requires the study's exact OCCT CAD object");
    }
    if (!std::isfinite(settings.absoluteTolerance) || settings.absoluteTolerance <= 0.0 ||
        !std::isfinite(settings.relativeTolerance) || settings.relativeTolerance <= 0.0 ||
        settings.maximumIterations == 0 ||
        settings.maximumIterations > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        mesh.nodes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        mesh.elementCount() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        mesh.boundaryElementCount() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("linear elasticity solver settings or mesh sizes are invalid");
    }
    validateMeshAgainstCurrentCad(document, mesh);

    const EquationRegistry registry = makeInitialEquationRegistry();
    const EquationDefinition* equation = registry.findAtLeast(study.equationDefinitionId, ModelMaturity::E3);
    if (equation == nullptr || study.equationDefinitionId != kElasticityEquationId) {
        throw std::invalid_argument("MFEM 3D linear elasticity requires the registered E3 isotropic small-strain equation");
    }
    const IsotropicMaterial material = resolveMaterial(document, study);

    mfem::Mesh mfemMesh(3, static_cast<int>(mesh.nodes.size()), static_cast<int>(mesh.elementCount()),
                        static_cast<int>(mesh.boundaryElementCount()), 3);
    for (const AnalysisNode& node : mesh.nodes) {
        const std::array<mfem::real_t, 3> point{
            node.positionM[0], node.positionM[1], node.positionM[2]};
        mfemMesh.AddVertex(point.data());
    }
    for (const AnalysisHexElement& element : mesh.elements) {
        std::array<int, 8> vertices{};
        std::transform(element.nodeIndices.begin(), element.nodeIndices.end(), vertices.begin(),
                       [](std::uint32_t index) { return static_cast<int>(index); });
        mfemMesh.AddHex(vertices.data(), 1);
    }
    for (const AnalysisBoundaryElement& element : mesh.boundaryElements) {
        std::array<int, 4> vertices{};
        std::transform(element.nodeIndices.begin(), element.nodeIndices.end(), vertices.begin(),
                       [](std::uint32_t index) { return static_cast<int>(index); });
        mfemMesh.AddBdrQuad(vertices.data(), element.attribute);
    }
    for (const AnalysisTetElement& element : mesh.tetrahedra) {
        std::array<int, 4> vertices{};
        std::transform(element.nodeIndices.begin(), element.nodeIndices.end(), vertices.begin(),
                       [](std::uint32_t index) { return static_cast<int>(index); });
        mfemMesh.AddTet(vertices.data(), 1);
    }
    for (const AnalysisBoundaryTriangle& element : mesh.boundaryTriangles) {
        std::array<int, 3> vertices{};
        std::transform(element.nodeIndices.begin(), element.nodeIndices.end(), vertices.begin(),
                       [](std::uint32_t index) { return static_cast<int>(index); });
        mfemMesh.AddBdrTriangle(vertices.data(), element.attribute);
    }
    if (tetrahedral) mfemMesh.FinalizeTetMesh(1, 0, false);
    else mfemMesh.FinalizeHexMesh(1, 0, true);
    mfemMesh.SetAttributes();
    if (mfemMesh.Dimension() != 3 || mfemMesh.GetNV() != static_cast<int>(mesh.nodes.size()) ||
        mfemMesh.GetNE() != static_cast<int>(mesh.elementCount()) ||
        mfemMesh.GetNBE() != static_cast<int>(mesh.boundaryElementCount())) {
        throw std::runtime_error("MFEM did not preserve the explicit CAD-derived mesh");
    }

    mfem::H1_FECollection collection(1, 3);
    mfem::FiniteElementSpace finiteElementSpace(&mfemMesh, &collection, 3);
    const int maxAttribute = maximumBoundaryAttribute(mesh);
    std::array<mfem::Array<int>, 3> essentialMarkers;
    for (auto& marker : essentialMarkers) {
        marker.SetSize(maxAttribute);
        marker = 0;
    }
    std::map<int, std::vector<std::uint32_t>> boundaryNodes;
    const auto addBoundaryNodes = [&](const auto& elements) {
        for (const auto& element : elements) {
            auto& nodes = boundaryNodes[element.attribute];
            nodes.insert(nodes.end(), element.nodeIndices.begin(), element.nodeIndices.end());
        }
    };
    addBoundaryNodes(mesh.boundaryElements);
    addBoundaryNodes(mesh.boundaryTriangles);
    for (auto& [attribute, nodes] : boundaryNodes) {
        (void)attribute;
        std::sort(nodes.begin(), nodes.end());
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    }
    std::map<std::pair<std::uint32_t, int>, double> prescribedValues;
    mfem::GridFunction solutionField(&finiteElementSpace);
    solutionField = 0.0;

    for (const BoundaryCondition& condition : study.boundaryConditions) {
        if (const auto* fixed = std::get_if<FixedDisplacement>(&condition)) {
            const int attribute = boundaryRegion(mesh, fixed->topologyReferenceId).attribute;
            for (int component = 0; component < 3; ++component) {
                if (!fixed->constrainedComponents[static_cast<std::size_t>(component)]) continue;
                essentialMarkers[static_cast<std::size_t>(component)][attribute - 1] = 1;
            }
            {
                for (const std::uint32_t node : boundaryNodes.at(attribute)) {
                    for (int component = 0; component < 3; ++component) {
                        if (!fixed->constrainedComponents[static_cast<std::size_t>(component)]) continue;
                        const double value = fixed->displacementM[static_cast<std::size_t>(component)];
                        const auto key = std::make_pair(node, component);
                        const auto [found, inserted] = prescribedValues.emplace(key, value);
                        if (!inserted && std::abs(found->second - value) > 1e-12) {
                            throw std::invalid_argument("overlapping displacement constraints prescribe conflicting values");
                        }
                    }
                }
            }
        } else if (std::holds_alternative<InitialCondition>(condition)) {
            throw std::invalid_argument("initial conditions are not consumed by this static elasticity solver");
        }
    }

    if (prescribedValues.empty()) {
        throw std::invalid_argument("static elasticity requires at least one constrained displacement component");
    }
    for (const auto& [key, value] : prescribedValues) {
        mfem::Array<int> vertexDofs;
        finiteElementSpace.GetVertexVDofs(static_cast<int>(key.first), vertexDofs);
        if (vertexDofs.Size() != 3) {
            throw std::runtime_error("MFEM order-one vector space did not return three vertex degrees of freedom");
        }
        mfem::real_t sign = 1.0;
        const int dof = mfem::FiniteElementSpace::DecodeDof(vertexDofs[key.second], sign);
        solutionField[dof] = value / sign;
    }

    mfem::ConstantCoefficient lambdaCoefficient(material.lambdaPa);
    mfem::ConstantCoefficient shearCoefficient(material.shearModulusPa);
    mfem::BilinearForm stiffnessForm(&finiteElementSpace);
    stiffnessForm.AddDomainIntegrator(new mfem::ElasticityIntegrator(lambdaCoefficient, shearCoefficient));
    stiffnessForm.Assemble();
    // Keep structurally zero entries as well: exact cancellation in one vector
    // component must not leave an asymmetric sparsity graph for essential-DOF
    // elimination of the symmetric elasticity operator.
    stiffnessForm.Finalize(0);
    mfem::SparseMatrix uneliminatedStiffness(stiffnessForm.SpMat());

    Vector3 totalAppliedForce{0.0, 0.0, 0.0};
    std::vector<std::unique_ptr<mfem::Array<int>>> boundaryMarkers;
    std::vector<std::unique_ptr<mfem::VectorCoefficient>> loadCoefficients;
    std::unordered_set<std::uint32_t> loadedVertices;
    mfem::LinearForm rightHandSide(&finiteElementSpace);
    const auto addSurfaceLoad = [&](const std::string& topologyId, const Vector3& force) {
        const AnalysisBoundaryRegion& region = boundaryRegion(mesh, topologyId);
        totalAppliedForce = add(totalAppliedForce, force);
        mfem::Vector traction(3);
        for (int component = 0; component < 3; ++component) {
            traction[component] = force[static_cast<std::size_t>(component)] / region.areaM2;
        }
        auto coefficient = std::make_unique<mfem::VectorConstantCoefficient>(traction);
        auto marker = std::make_unique<mfem::Array<int>>(maxAttribute);
        *marker = 0;
        (*marker)[region.attribute - 1] = 1;
        rightHandSide.AddBoundaryIntegrator(new mfem::VectorBoundaryLFIntegrator(*coefficient), *marker);
        loadCoefficients.push_back(std::move(coefficient));
        boundaryMarkers.push_back(std::move(marker));
        const auto& nodes = boundaryNodes.at(region.attribute);
        loadedVertices.insert(nodes.begin(), nodes.end());
    };
    const ShapeMetrics cadMetrics = OcctKernel::measure(object->shape);
    const double cadVolumeM3 = cadMetrics.volume * 1e-9;
    if (!std::isfinite(cadVolumeM3) || cadVolumeM3 <= 0.0) {
        throw std::invalid_argument("structural study CAD body must have a positive finite exact volume");
    }
    double meshVolumeM3 = 0.0;
    for (int element = 0; element < mfemMesh.GetNE(); ++element)
        meshVolumeM3 += mfemMesh.GetElementVolume(element);
    if (!std::isfinite(meshVolumeM3) || meshVolumeM3 <= 0.0)
        throw std::invalid_argument("analysis mesh has no positive finite volume");
    for (const BoundaryCondition& condition : study.boundaryConditions) {
        if (const auto* force = std::get_if<AppliedForce>(&condition)) {
            addSurfaceLoad(force->topologyReferenceId, force->totalForceN);
        } else if (const auto* pressure = std::get_if<AppliedPressure>(&condition)) {
            const AnalysisBoundaryRegion& region = boundaryRegion(mesh, pressure->topologyReferenceId);
            if (std::abs(norm(region.outwardNormal) - 1.0) > 1e-10)
                throw std::invalid_argument("uniform pressure currently requires a planar face with a unit normal");
            const Vector3 force = scaled(region.outwardNormal, -pressure->pressurePa * region.areaM2);
            addSurfaceLoad(pressure->topologyReferenceId, force);
        } else if (const auto* bodyForce = std::get_if<BodyForce>(&condition)) {
            const Vector3 resultant = scaled(bodyForce->forceDensityNPerM3, meshVolumeM3);
            totalAppliedForce = add(totalAppliedForce, resultant);
            mfem::Vector bodyLoad(3);
            for (int component = 0; component < 3; ++component) {
                bodyLoad[component] = bodyForce->forceDensityNPerM3[static_cast<std::size_t>(component)];
            }
            auto coefficient = std::make_unique<mfem::VectorConstantCoefficient>(bodyLoad);
            rightHandSide.AddDomainIntegrator(new mfem::VectorDomainLFIntegrator(*coefficient));
            loadCoefficients.push_back(std::move(coefficient));
        }
    }
    rightHandSide.Assemble();
    if (rightHandSide.Norml2() <= std::numeric_limits<double>::epsilon()) {
        throw std::invalid_argument("static elasticity requires a non-zero applied load");
    }
    const mfem::Vector physicalRightHandSide(rightHandSide);

    std::set<int> essentialTrueDofs;
    std::array<std::vector<int>, 3> componentEssentialDofs;
    for (int component = 0; component < 3; ++component) {
        mfem::Array<int> componentDofs;
        finiteElementSpace.GetEssentialTrueDofs(essentialMarkers[static_cast<std::size_t>(component)],
                                                componentDofs, component);
        auto& selected = componentEssentialDofs[static_cast<std::size_t>(component)];
        selected.reserve(static_cast<std::size_t>(componentDofs.Size()));
        for (int index = 0; index < componentDofs.Size(); ++index) {
            selected.push_back(componentDofs[index]);
            essentialTrueDofs.insert(componentDofs[index]);
        }
    }
    if (essentialTrueDofs.empty()) {
        throw std::invalid_argument("MFEM produced no essential displacement degrees of freedom");
    }
    mfem::Array<int> essentialDofList(static_cast<int>(essentialTrueDofs.size()));
    int essentialIndex = 0;
    for (const int dof : essentialTrueDofs) essentialDofList[essentialIndex++] = dof;

    mfem::SparseMatrix systemMatrix;
    mfem::Vector systemSolution;
    mfem::Vector systemRhs;
    stiffnessForm.FormLinearSystem(essentialDofList, solutionField, rightHandSide,
                                   systemMatrix, systemSolution, systemRhs);
    mfem::GSSmoother preconditioner(systemMatrix, mfem::GSSmoother::SYMMETRIC);
    mfem::CGSolver conjugateGradient;
    conjugateGradient.SetOperator(systemMatrix);
    conjugateGradient.SetPreconditioner(preconditioner);
    conjugateGradient.SetAbsTol(settings.absoluteTolerance);
    conjugateGradient.SetRelTol(settings.relativeTolerance);
    conjugateGradient.SetMaxIter(static_cast<int>(settings.maximumIterations));
    conjugateGradient.SetPrintLevel(-1);
    conjugateGradient.Mult(systemRhs, systemSolution);
    if (!conjugateGradient.GetConverged() || !std::isfinite(conjugateGradient.GetFinalRelNorm())) {
        throw std::runtime_error("MFEM 3D linear elasticity conjugate-gradient solver did not converge");
    }
    stiffnessForm.RecoverFEMSolution(systemSolution, rightHandSide, solutionField);

    mfem::Vector trueSolution;
    solutionField.GetTrueDofs(trueSolution);
    mfem::Vector stiffnessTimesSolution(trueSolution.Size());
    uneliminatedStiffness.Mult(trueSolution, stiffnessTimesSolution);
    mfem::Vector residual(stiffnessTimesSolution);
    residual.Add(-1.0, physicalRightHandSide);

    StructuralAnalysisRun run;
    run.converged = true;
    run.iterations = static_cast<std::uint32_t>(conjugateGradient.GetNumIterations());
    run.relativeResidual = conjugateGradient.GetFinalRelNorm();
    run.appliedForceN = totalAppliedForce;
    run.provenance.solverRunId = createDatumUuidV4();
    run.provenance.timestampUtc = utcTimestamp();
    run.provenance.sourceProjectId = document.projectId();
    run.provenance.sourceDocumentRevision = document.revision();
    run.provenance.cadObjectId = study.cadObjectId;
    run.provenance.geometryFeatureId = object->featureGraph.outputFeatureId();
    run.provenance.materialDefinitionId = study.materialDefinitionId;
    run.provenance.materialRevision = material.revision;
    run.provenance.materialStateId = study.materialStateId;
    run.provenance.analysisTemperatureK = material.temperatureK;
    run.provenance.temperatureRangeVerified = material.temperatureRangeVerified;
    run.provenance.equationDefinitionId = equation->id;
    run.provenance.equationMaturity = equation->maturity;
    run.provenance.solverId = tetrahedral ? "mfem.serial-h1-p1-3d-linear-elasticity" : solverId();
    run.provenance.solverVersion = solverVersion();
    run.provenance.mesh = mesh.provenance;
    run.provenance.mesh.geometrySha256 = mesh.geometrySha256;
    run.provenance.mesh.backendBuildInfo = mesh.backendBuildInfo;
    AnalysisMeshSnapshot meshSnapshot;
    meshSnapshot.meshRevisionId = mesh.provenance.revisionId;
    meshSnapshot.nodePositionsM.reserve(mesh.nodes.size());
    for (const AnalysisNode& node : mesh.nodes) {
        meshSnapshot.nodePositionsM.push_back(node.positionM);
    }
    meshSnapshot.hexahedra.reserve(mesh.elementCount());
    for (const AnalysisHexElement& element : mesh.elements) {
        meshSnapshot.hexahedra.push_back(element.nodeIndices);
    }
    for (const AnalysisTetElement& element : mesh.tetrahedra)
        meshSnapshot.tetrahedra.push_back(element.nodeIndices);
    run.meshSnapshot = std::move(meshSnapshot);
    run.provenance.topologyReferenceIds = mesh.topologyReferenceIds;
    run.provenance.absoluteTolerance = settings.absoluteTolerance;
    run.provenance.relativeTolerance = settings.relativeTolerance;
    run.provenance.maximumIterations = settings.maximumIterations;
    for (const BoundaryCondition& condition : study.boundaryConditions) {
        run.provenance.boundaryConditionIds.push_back(boundaryConditionId(condition));
    }

    for (int component = 0; component < 3; ++component) {
        double reaction = 0.0;
        for (const int dof : componentEssentialDofs[static_cast<std::size_t>(component)]) {
            reaction += residual[dof];
        }
        run.reactionForceN[static_cast<std::size_t>(component)] = reaction;
    }
    const double forceScale = std::max({norm(run.appliedForceN), norm(run.reactionForceN), 1e-12});
    run.relativeForceBalanceError = norm(add(run.appliedForceN, run.reactionForceN)) / forceScale;
    run.strainEnergyJ = 0.5 * (trueSolution * stiffnessTimesSolution);
    if (!std::isfinite(run.strainEnergyJ) || run.strainEnergyJ < 0.0) {
        throw std::runtime_error("MFEM produced invalid structural strain energy");
    }

    std::vector<Vector3> nodalDisplacements(mesh.nodes.size());
    FieldDataset displacement;
    displacement.id = createDatumUuidV4();
    displacement.fieldType = FieldType::Displacement;
    displacement.location = FieldLocation::Nodes;
    displacement.components = {"x", "y", "z"};
    displacement.units = "m";
    displacement.meshRevisionId = mesh.provenance.revisionId;
    displacement.solverRunId = run.provenance.solverRunId;
    displacement.values.reserve(mesh.nodes.size() * 3);
    for (std::size_t vertex = 0; vertex < mesh.nodes.size(); ++vertex) {
        mfem::Array<int> vertexDofs;
        finiteElementSpace.GetVertexVDofs(static_cast<int>(vertex), vertexDofs);
        if (vertexDofs.Size() != 3) throw std::runtime_error("MFEM order-one vector vertex has an unexpected degree count");
        for (int component = 0; component < 3; ++component) {
            mfem::real_t sign = 1.0;
            const int dof = mfem::FiniteElementSpace::DecodeDof(vertexDofs[component], sign);
            const double value = sign * trueSolution[dof];
            nodalDisplacements[vertex][static_cast<std::size_t>(component)] = value;
            displacement.values.push_back(value);
        }
        run.maximumDisplacementM = std::max(run.maximumDisplacementM, norm(nodalDisplacements[vertex]));
    }
    run.fields.push_back(std::move(displacement));

    if (!loadedVertices.empty()) {
        Vector3 average{0.0, 0.0, 0.0};
        for (const std::uint32_t vertex : loadedVertices) {
            average = add(average, nodalDisplacements[vertex]);
        }
        average = scaled(average, 1.0 / static_cast<double>(loadedVertices.size()));
        const double appliedNorm = norm(run.appliedForceN);
        run.tipDisplacementM = appliedNorm > 0.0
            ? (average[0] * run.appliedForceN[0] + average[1] * run.appliedForceN[1] +
               average[2] * run.appliedForceN[2]) / appliedNorm
            : norm(average);
    } else {
        run.tipDisplacementM = run.maximumDisplacementM;
    }

    std::array<double, 3> minimum{
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array<double, 3> maximum{
        -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    for (const AnalysisNode& node : mesh.nodes) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], node.positionM[axis]);
            maximum[axis] = std::max(maximum[axis], node.positionM[axis]);
        }
    }
    const std::array<double, 3> dimensions{
        maximum[0] - minimum[0], maximum[1] - minimum[1], maximum[2] - minimum[2]};
    const auto longestAxis = static_cast<std::size_t>(
        std::max_element(dimensions.begin(), dimensions.end()) - dimensions.begin());
    const double centerAlongAxis = 0.5 * (minimum[longestAxis] + maximum[longestAxis]);
    using Tensor3 = std::array<std::array<double, 3>, 3>;
    const auto stressTensorAt = [&mfemMesh, &solutionField, &material](
                                    std::size_t element, const mfem::IntegrationPoint& point) {
        mfem::ElementTransformation* transformation = mfemMesh.GetElementTransformation(static_cast<int>(element));
        if (transformation == nullptr) throw std::runtime_error("MFEM did not return an element transformation");
        transformation->SetIntPoint(&point);
        mfem::DenseMatrix gradient;
        solutionField.GetVectorGradient(*transformation, gradient);
        if (gradient.Height() != 3 || gradient.Width() != 3) {
            throw std::runtime_error("MFEM displacement gradient is not a 3x3 tensor");
        }
        Tensor3 strainTensor{};
        Tensor3 stressTensor{};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                strainTensor[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
                    0.5 * (gradient(row, column) + gradient(column, row));
            }
        }
        const double trace = strainTensor[0][0] + strainTensor[1][1] + strainTensor[2][2];
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                stressTensor[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] =
                    2.0 * material.shearModulusPa * strainTensor[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] +
                    (row == column ? material.lambdaPa * trace : 0.0);
            }
        }
        return std::make_pair(strainTensor, stressTensor);
    };

    FieldDataset strain;
    strain.id = createDatumUuidV4();
    strain.fieldType = FieldType::StrainTensor;
    strain.location = FieldLocation::Elements;
    strain.components = {"xx", "yy", "zz", "xy", "yz", "zx"};
    strain.units = "1";
    strain.meshRevisionId = mesh.provenance.revisionId;
    strain.solverRunId = run.provenance.solverRunId;
    strain.values.reserve(mesh.elementCount() * 6);

    FieldDataset stress;
    stress.id = createDatumUuidV4();
    stress.fieldType = FieldType::StressTensor;
    stress.location = FieldLocation::Elements;
    stress.components = {"xx", "yy", "zz", "xy", "yz", "zx"};
    stress.units = "Pa";
    stress.meshRevisionId = mesh.provenance.revisionId;
    stress.solverRunId = run.provenance.solverRunId;
    stress.values.reserve(mesh.elementCount() * 6);

    FieldDataset vonMises;
    vonMises.id = createDatumUuidV4();
    vonMises.fieldType = FieldType::VonMisesStress;
    vonMises.location = FieldLocation::Elements;
    vonMises.components = {"von_mises"};
    vonMises.units = "Pa";
    vonMises.meshRevisionId = mesh.provenance.revisionId;
    vonMises.solverRunId = run.provenance.solverRunId;
    vonMises.values.reserve(mesh.elementCount());

    const mfem::IntegrationPoint& centerPoint = mfem::Geometries.GetCenter(tetrahedral ? mfem::Geometry::TETRAHEDRON : mfem::Geometry::CUBE);
    for (std::size_t element = 0; element < mesh.elementCount(); ++element) {
        const auto [strainTensor, stressTensor] = stressTensorAt(element, centerPoint);
        const double vm = std::sqrt(0.5 * (
            std::pow(stressTensor[0][0] - stressTensor[1][1], 2) +
            std::pow(stressTensor[1][1] - stressTensor[2][2], 2) +
            std::pow(stressTensor[2][2] - stressTensor[0][0], 2)) +
            3.0 * (std::pow(stressTensor[0][1], 2) + std::pow(stressTensor[1][2], 2) +
                   std::pow(stressTensor[2][0], 2)));
        strain.values.insert(strain.values.end(), {
            strainTensor[0][0], strainTensor[1][1], strainTensor[2][2],
            strainTensor[0][1], strainTensor[1][2], strainTensor[2][0]});
        stress.values.insert(stress.values.end(), {
            stressTensor[0][0], stressTensor[1][1], stressTensor[2][2],
            stressTensor[0][1], stressTensor[1][2], stressTensor[2][0]});
        vonMises.values.push_back(vm);
        run.maximumVonMisesStressPa = std::max(run.maximumVonMisesStressPa, vm);
    }

    std::array<std::size_t, 2> transverseAxes{};
    for (std::size_t axis = 0, index = 0; axis < 3; ++axis) {
        if (axis != longestAxis) transverseAxes[index++] = axis;
    }
    const double geometryScale = *std::max_element(dimensions.begin(), dimensions.end());
    const double sectionTolerance = std::max(1e-12, geometryScale * 1e-10);
    Vector3 samplePoint{0.0, 0.0, 0.0};
    samplePoint[longestAxis] = centerAlongAxis;
    const std::size_t stressOffsetAxis = transverseAxes[0];
    const std::size_t neutralAxis = transverseAxes[1];
    samplePoint[stressOffsetAxis] =
        0.5 * (minimum[stressOffsetAxis] + maximum[stressOffsetAxis]) +
        0.75 * 0.5 * dimensions[stressOffsetAxis];
    samplePoint[neutralAxis] = 0.5 * (minimum[neutralAxis] + maximum[neutralAxis]);

    if (settings.sectionStressSamplePositionM) samplePoint = *settings.sectionStressSamplePositionM;
    double axialStressSum = 0.0;
    std::size_t adjacentElementCount = 0;
    for (std::size_t element = 0; element < mesh.elementCount(); ++element) {
        Vector3 elementMinimum{
            std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()};
        Vector3 elementMaximum{
            -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()};
        mfem::Array<int> elementVertices;
        mfemMesh.GetElementVertices(static_cast<int>(element), elementVertices);
        for (const int node : elementVertices) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                elementMinimum[axis] = std::min(elementMinimum[axis], mesh.nodes[node].positionM[axis]);
                elementMaximum[axis] = std::max(elementMaximum[axis], mesh.nodes[node].positionM[axis]);
            }
        }
        bool containsPoint = true;
        mfem::IntegrationPoint point;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (samplePoint[axis] < elementMinimum[axis] - sectionTolerance ||
                samplePoint[axis] > elementMaximum[axis] + sectionTolerance) {
                containsPoint = false;
                break;
            }
        }
        if (!containsPoint) continue;
        mfem::Vector physicalPoint(3);
        for (int axis = 0; axis < 3; ++axis) {
            physicalPoint[axis] = samplePoint[static_cast<std::size_t>(axis)];
        }
        mfem::ElementTransformation* transformation = mfemMesh.GetElementTransformation(static_cast<int>(element));
        if (transformation == nullptr)
            throw std::runtime_error("MFEM did not return an element transformation");
        if (transformation->TransformBack(physicalPoint, point) != 0) continue;
        const auto tensors = stressTensorAt(element, point);
        axialStressSum += tensors.second[longestAxis][longestAxis];
        ++adjacentElementCount;
    }
    if (adjacentElementCount == 0) {
        throw std::runtime_error("MFEM could not evaluate the midspan axial stress at its fixed section point");
    }
    run.sectionAxialStressPa = std::abs(axialStressSum / static_cast<double>(adjacentElementCount));
    run.sectionStressSamplePositionM = samplePoint;
    run.fields.push_back(std::move(strain));
    run.fields.push_back(std::move(stress));
    run.fields.push_back(std::move(vonMises));

    run.validate([&study]() {
        std::vector<std::string> ids;
        ids.reserve(study.boundaryConditions.size());
        for (const BoundaryCondition& condition : study.boundaryConditions) ids.push_back(boundaryConditionId(condition));
        return ids;
    }());
    return run;
}

}  // namespace datumloop
