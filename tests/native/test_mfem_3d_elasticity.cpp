#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/io/DldProjectStore.hpp"
#include "datumloop/numerics/AnalysisMesh.hpp"
#include "datumloop/numerics/MfemLinearElasticitySolver.hpp"
#include "datumloop/numerics/StructuralReducedModel.hpp"
#include "datumloop/robotics/RobotLearning.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <BRepPrimAPI_MakeCylinder.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

datumloop::MaterialProperty sourcedConstant(const std::string& name,
                                           const std::string& unit,
                                           double value,
                                           double sourcePercentUncertainty,
                                           const std::string& sourceId)
{
    datumloop::MaterialProperty property;
    property.propertyName = name;
    property.propertyUnit = unit;
    property.law = datumloop::MaterialPropertyLaw::Constant;
    property.constantValue = value;
    property.source = "NBS NSRDS 61 Part V, Table 2(SI), mean value; percent uncertainty " +
                      std::to_string(sourcePercentUncertainty) + "%";
    property.sourceVersion = "NBS NSRDS 61 Part V (1982)";
    property.sourceId = sourceId;
    property.absoluteUncertainty = value * sourcePercentUncertainty / 100.0;
    return property;
}

datumloop::DatumDocument makeDocument(datumloop::StructuralAnalysisStudy& study,
                                     std::array<std::string, 3>& topologyIds,
                                     double& youngsModulusPa)
{
    datumloop::DatumDocument document("CAD-driven 3D aluminum cantilever");
    const std::string cadObjectId = document.createBox("200 mm cantilever beam", {200.0, 10.0, 10.0}).id;

    const datumloop::DatumTopologyReference* minimumX = nullptr;
    const datumloop::DatumTopologyReference* maximumX = nullptr;
    std::size_t faceCount = 0;
    for (const datumloop::DatumTopologyReference& reference : document.topologyReferences()) {
        if (reference.objectId != cadObjectId || reference.subshapeType != datumloop::DatumSubshapeType::Face) continue;
        ++faceCount;
        if (minimumX == nullptr || reference.geometrySignature.centroidMm[0] < minimumX->geometrySignature.centroidMm[0]) {
            minimumX = &reference;
        }
        if (maximumX == nullptr || reference.geometrySignature.centroidMm[0] > maximumX->geometrySignature.centroidMm[0]) {
            maximumX = &reference;
        }
    }
    require(faceCount == 6 && minimumX != nullptr && maximumX != nullptr && minimumX != maximumX,
            "the CAD beam must expose six persistent planar face references");
    topologyIds = {minimumX->id, maximumX->id, {}};

    datumloop::MaterialDefinition aluminum;
    aluminum.id = datumloop::createDatumUuidV4();
    aluminum.name = "Aluminum alloy 6061-T6 (NIST compiled properties)";
    datumloop::MaterialSource source;
    source.id = datumloop::createDatumUuidV4();
    source.kind = datumloop::MaterialSourceKind::PublishedSource;
    source.title = "Physical Properties Data Compilations Relevant to Energy Storage. V. Mechanical Properties Data on Alloys for Use in Flywheels";
    source.publisher = "National Bureau of Standards / NIST archive";
    source.version = "NBS NSRDS 61 Part V (1982)";
    source.reference = "H. M. Ledbetter (1982), NBS NSRDS 61 Part V, Table 2(SI), mean values and percent uncertainty";
    source.url = "https://srd.nist.gov/NSRDS/NSRDS-NBS-61-V.pdf";
    source.validate();
    aluminum.sources.push_back(source);
    youngsModulusPa = 69.6e9;
    aluminum.propertyModels = {
        sourcedConstant("mass.density", "kg/m^3", 2710.0, 0.2, source.id),
        sourcedConstant("elastic.youngs_modulus", "Pa", youngsModulusPa, 2.7, source.id),
        sourcedConstant("elastic.poisson_ratio", "1", 0.331, 1.5, source.id),
    };
    aluminum.validate();

    datumloop::MaterialState referenceState;
    referenceState.id = datumloop::createDatumUuidV4();
    referenceState.materialDefinitionId = aluminum.id;
    referenceState.temperatureK = 293.15;
    referenceState.provenance = {
        "Reference temperature selected for this numerical fixture; the cited compilation does not provide a verified property temperature range.",
    };
    referenceState.validate();
    datumloop::DatumElectronicsModel electronics;
    electronics.materials.push_back(aluminum);
    electronics.materialStates.push_back(referenceState);
    document.setElectronicsModel(std::move(electronics));

    study.id = datumloop::createDatumUuidV4();
    study.name = "6061-T6 CAD cantilever, 10 N transverse load";
    study.cadObjectId = cadObjectId;
    study.materialDefinitionId = aluminum.id;
    study.materialStateId = referenceState.id;
    study.equationDefinitionId = "structural.linear-elasticity.3d.isotropic-small-strain.v1";
    datumloop::FixedDisplacement fixed;
    fixed.id = datumloop::createDatumUuidV4();
    fixed.name = "Clamp at x = 0";
    fixed.cadObjectId = cadObjectId;
    fixed.topologyReferenceId = minimumX->id;
    datumloop::AppliedForce load;
    load.id = datumloop::createDatumUuidV4();
    load.name = "10 N downward tip force";
    load.cadObjectId = cadObjectId;
    load.topologyReferenceId = maximumX->id;
    load.totalForceN = {0.0, -10.0, 0.0};
    study.boundaryConditions = {fixed, load};

    datumloop::DatumEngineeringModel engineering;
    engineering.structuralStudies.push_back(study);
    document.setEngineeringModel(std::move(engineering));
    return document;
}

double relativeChange(double current, double previous)
{
    return std::abs(current - previous) / std::max(std::abs(current), 1e-30);
}

QJsonObject encodeTipResponseErrorMetrics(const datumloop::StructuralResponseErrorMetrics& metrics)
{
    return QJsonObject{
        {QStringLiteral("sample_count"), static_cast<int>(metrics.sampleCount)},
        {QStringLiteral("mean_absolute_relative_error"), metrics.meanAbsoluteRelativeError},
        {QStringLiteral("rms_relative_error"), metrics.rmsRelativeError},
        {QStringLiteral("maximum_relative_error"), metrics.maximumRelativeError},
        {QStringLiteral("worst_operating_point"), QJsonObject{
            {QStringLiteral("applied_force_n"), metrics.worstOperatingPoint.appliedForceMagnitudeN},
            {QStringLiteral("youngs_modulus_pa"), metrics.worstOperatingPoint.youngsModulusPa},
            {QStringLiteral("reference_tip_deflection_m"), metrics.worstOperatingPoint.referenceTipDeflectionM},
            {QStringLiteral("predicted_tip_deflection_m"), metrics.worstOperatingPoint.predictedTipDeflectionM},
            {QStringLiteral("relative_error"),
             datumloop::relativeTipDeflectionError(metrics.worstOperatingPoint)},
            {QStringLiteral("reference_run_id"), QString::fromStdString(metrics.worstOperatingPoint.referenceRunId)},
        }},
    };
}

bool preservesRigidObservation(const datumloop::RobotObservation& candidate,
                               const datumloop::RobotObservation& reference)
{
    if (candidate.timeS != reference.timeS || candidate.sensors.size() != reference.sensors.size()) {
        return false;
    }
    for (std::size_t index = 0; index < reference.sensors.size(); ++index) {
        const datumloop::RobotSensorObservation& left = candidate.sensors[index];
        const datumloop::RobotSensorObservation& right = reference.sensors[index];
        if (left.sensorId != right.sensorId || left.valid != right.valid || left.value != right.value ||
            left.unit != right.unit || left.captureTimeS != right.captureTimeS ||
            left.deliveryTimeS != right.deliveryTimeS || left.frameAvailable != right.frameAvailable ||
            left.imageWidthPixels != right.imageWidthPixels ||
            left.imageHeightPixels != right.imageHeightPixels || left.detail != right.detail) {
            return false;
        }
    }
    return true;
}

const datumloop::FieldDataset& field(const datumloop::StructuralAnalysisRun& run,
                                     datumloop::FieldType type)
{
    const auto found = std::find_if(run.fields.begin(), run.fields.end(), [type](const auto& candidate) {
        return candidate.fieldType == type;
    });
    if (found == run.fields.end()) throw std::runtime_error("structural run omitted a required field dataset");
    return *found;
}

datumloop::StructuralRomHighFidelitySample runRomSample(
    const datumloop::DatumDocument& sourceDocument,
    const datumloop::StructuralAnalysisStudy& sourceStudy,
    const datumloop::MfemLinearElasticitySolver& solver,
    const datumloop::AnalysisMeshSettings& meshSettings,
    const std::string& sharedMeshRevisionId,
    double appliedForceN,
    double youngsModulusPa)
{
    datumloop::DatumDocument sampleDocument = sourceDocument;
    datumloop::DatumElectronicsModel electronics = sampleDocument.electronicsModel();
    datumloop::MaterialDefinition* material = nullptr;
    for (datumloop::MaterialDefinition& candidate : electronics.materials) {
        if (candidate.id == sourceStudy.materialDefinitionId) {
            material = &candidate;
            break;
        }
    }
    require(material != nullptr, "ROM sample could not resolve its DatumLoop material definition");
    datumloop::MaterialProperty* youngsModulus = nullptr;
    for (datumloop::MaterialProperty& property : material->propertyModels) {
        if (property.propertyName == "elastic.youngs_modulus") {
            youngsModulus = &property;
            break;
        }
    }
    require(youngsModulus != nullptr && youngsModulus->constantValue.has_value() &&
                youngsModulus->absoluteUncertainty.has_value(),
            "ROM sample requires the sourced Young's-modulus value and uncertainty");
    const double sourceUncertaintyFraction = *youngsModulus->absoluteUncertainty /
                                            *youngsModulus->constantValue;
    youngsModulus->constantValue = youngsModulusPa;
    youngsModulus->absoluteUncertainty = youngsModulusPa * sourceUncertaintyFraction;
    material->validate();
    sampleDocument.setElectronicsModel(std::move(electronics));

    datumloop::StructuralAnalysisStudy sampleStudy = sourceStudy;
    bool loadUpdated = false;
    for (datumloop::BoundaryCondition& condition : sampleStudy.boundaryConditions) {
        if (auto* force = std::get_if<datumloop::AppliedForce>(&condition)) {
            force->totalForceN = {0.0, -appliedForceN, 0.0};
            loadUpdated = true;
        }
    }
    require(loadUpdated, "ROM sample could not find the persistent-face applied force");

    datumloop::AnalysisMesh mesh = datumloop::OcctPrismAnalysisMesher::generate(
        sampleDocument, sampleStudy.cadObjectId, meshSettings);
    mesh.provenance.revisionId = sharedMeshRevisionId;
    datumloop::StructuralAnalysisRun run = solver.solve(sampleDocument, sampleStudy, mesh);
    return {appliedForceN, youngsModulusPa, std::move(run)};
}

void writeReport(const std::filesystem::path& path,
                 const datumloop::StructuralAnalysisStudy& study,
                 const std::vector<datumloop::StructuralAnalysisRun>& runs,
                 const datumloop::MaterialDefinition& material,
                 double youngsModulusPa,
                 double analyticalTipM,
                 double analyticalSectionStressPa,
                 const datumloop::StructuralElasticityRom& rom,
                 const QJsonObject& mujocoRomBridge,
                 const QJsonObject& fastVsHighFidelity)
{
    QJsonArray refinement;
    for (std::size_t index = 0; index < runs.size(); ++index) {
        const auto& run = runs[index];
        refinement.append(QJsonObject{
            {QStringLiteral("solver_run_id"), QString::fromStdString(run.provenance.solverRunId)},
            {QStringLiteral("timestamp_utc"), QString::fromStdString(run.provenance.timestampUtc)},
            {QStringLiteral("source_project_id"), QString::fromStdString(run.provenance.sourceProjectId)},
            {QStringLiteral("source_document_revision"), static_cast<double>(run.provenance.sourceDocumentRevision)},
            {QStringLiteral("cad_object_id"), QString::fromStdString(run.provenance.cadObjectId)},
            {QStringLiteral("geometry_feature_id"), run.provenance.geometryFeatureId.has_value()
                 ? QJsonValue(QString::fromStdString(*run.provenance.geometryFeatureId)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("material_state_id"), run.provenance.materialStateId.has_value()
                 ? QJsonValue(QString::fromStdString(*run.provenance.materialStateId)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("mesh_generator"), QString::fromStdString(run.provenance.mesh.generator)},
            {QStringLiteral("mesh_generator_version"), QString::fromStdString(run.provenance.mesh.generatorVersion)},
            {QStringLiteral("mesh_element_type"), QString::fromStdString(run.provenance.mesh.elementType)},
            {QStringLiteral("mesh_minimum_scaled_jacobian"), run.provenance.mesh.minimumScaledJacobian},
            {QStringLiteral("mesh_revision"), QString::fromStdString(run.provenance.mesh.revisionId)},
            {QStringLiteral("requested_max_element_size_m"), run.provenance.mesh.requestedMaximumElementSizeM},
            {QStringLiteral("nodes"), static_cast<double>(run.provenance.mesh.nodeCount)},
            {QStringLiteral("hexahedra"), static_cast<double>(run.provenance.mesh.elementCount)},
            {QStringLiteral("maximum_aspect_ratio"), run.provenance.mesh.maximumAspectRatio},
            {QStringLiteral("iterations"), static_cast<double>(run.iterations)},
            {QStringLiteral("relative_residual"), run.relativeResidual},
            {QStringLiteral("absolute_tolerance"), run.provenance.absoluteTolerance},
            {QStringLiteral("relative_tolerance"), run.provenance.relativeTolerance},
            {QStringLiteral("maximum_iterations"), static_cast<double>(run.provenance.maximumIterations)},
            {QStringLiteral("equation_definition_id"), QString::fromStdString(run.provenance.equationDefinitionId)},
            {QStringLiteral("solver_id"), QString::fromStdString(run.provenance.solverId)},
            {QStringLiteral("solver_version"), QString::fromStdString(run.provenance.solverVersion)},
            {QStringLiteral("boundary_condition_ids"), [&run] {
                 QJsonArray ids;
                 for (const std::string& id : run.provenance.boundaryConditionIds) {
                     ids.append(QString::fromStdString(id));
                 }
                 return ids;
             }()},
            {QStringLiteral("topology_reference_ids"), [&run] {
                 QJsonArray ids;
                 for (const std::string& id : run.provenance.topologyReferenceIds) {
                     ids.append(QString::fromStdString(id));
                 }
                 return ids;
             }()},
            {QStringLiteral("applied_force_n"), QJsonArray{run.appliedForceN[0], run.appliedForceN[1], run.appliedForceN[2]}},
            {QStringLiteral("reaction_force_n"), QJsonArray{run.reactionForceN[0], run.reactionForceN[1], run.reactionForceN[2]}},
            {QStringLiteral("relative_force_balance_error"), run.relativeForceBalanceError},
            {QStringLiteral("tip_displacement_m"), run.tipDisplacementM},
            {QStringLiteral("tip_displacement_relative_error_vs_euler_bernoulli"),
             relativeChange(run.tipDisplacementM, analyticalTipM)},
            {QStringLiteral("section_axial_stress_pa"), run.sectionAxialStressPa},
            {QStringLiteral("section_stress_sample_position_m"),
             QJsonArray{run.sectionStressSamplePositionM[0], run.sectionStressSamplePositionM[1],
                        run.sectionStressSamplePositionM[2]}},
            {QStringLiteral("section_stress_relative_error_vs_beam_estimate"),
             relativeChange(run.sectionAxialStressPa, analyticalSectionStressPa)},
            {QStringLiteral("maximum_displacement_m"), run.maximumDisplacementM},
            {QStringLiteral("maximum_von_mises_stress_pa"), run.maximumVonMisesStressPa},
            {QStringLiteral("strain_energy_j"), run.strainEnergyJ},
            {QStringLiteral("field_dataset_count"), static_cast<int>(run.fields.size())},
            {QStringLiteral("temperature_range_verified"), run.provenance.temperatureRangeVerified},
        });
    }
    const auto encodeRomSamples = [](const std::vector<datumloop::StructuralRomSampleRecord>& samples) {
        QJsonArray encodedSamples;
        for (const datumloop::StructuralRomSampleRecord& sample : samples) {
            encodedSamples.append(QJsonObject{
                {QStringLiteral("applied_force_n"), sample.appliedForceMagnitudeN},
                {QStringLiteral("youngs_modulus_pa"), sample.youngsModulusPa},
                {QStringLiteral("high_fidelity_tip_displacement_m"), sample.highFidelityTipDisplacementM},
                {QStringLiteral("rom_tip_displacement_m"), sample.romTipDisplacementM},
                {QStringLiteral("rom_effective_stiffness_n_per_m"), sample.effectiveStiffnessNPerM},
                {QStringLiteral("relative_error"), sample.relativeError},
                {QStringLiteral("solver_run_id"), QString::fromStdString(sample.solverRunId)},
                {QStringLiteral("material_revision"), QString::fromStdString(sample.materialRevision)},
            });
        }
        return encodedSamples;
    };
    QJsonArray materialProperties;
    for (const datumloop::MaterialProperty& property : material.propertyModels) {
        QJsonObject encoded{
            {QStringLiteral("name"), QString::fromStdString(property.propertyName)},
            {QStringLiteral("value"), property.constantValue.value_or(0.0)},
            {QStringLiteral("unit"), QString::fromStdString(property.propertyUnit)},
            {QStringLiteral("source"), QString::fromStdString(property.source)},
        };
        if (property.absoluteUncertainty.has_value()) {
            encoded.insert(QStringLiteral("absolute_uncertainty"), *property.absoluteUncertainty);
            if (property.constantValue.has_value() && *property.constantValue != 0.0) {
                encoded.insert(QStringLiteral("source_percent_uncertainty"),
                               std::round(*property.absoluteUncertainty /
                                          std::abs(*property.constantValue) * 100.0 * 1e6) / 1e6);
            }
        }
        materialProperties.append(encoded);
    }
    const auto& convergence = *study.meshConvergence;
    QJsonObject report{
        {QStringLiteral("benchmark_id"), QStringLiteral("datumloop-cad-mfem-3d-linear-elasticity-v1")},
        {QStringLiteral("validation_classification"), QStringLiteral("numerical verification; no experimental validation claimed")},
        {QStringLiteral("equation_definition_id"), QString::fromStdString(study.equationDefinitionId)},
        {QStringLiteral("equation_maturity"), QStringLiteral("E3")},
        {QStringLiteral("solver"), QString::fromStdString(runs.back().provenance.solverId)},
        {QStringLiteral("solver_version"), QString::fromStdString(runs.back().provenance.solverVersion)},
        {QStringLiteral("material_definition_id"), QString::fromStdString(study.materialDefinitionId)},
        {QStringLiteral("material_revision"), QString::fromStdString(runs.back().provenance.materialRevision)},
        {QStringLiteral("material_source"), QStringLiteral("H. M. Ledbetter (1982), NBS NSRDS 61 Part V, Table 2(SI); https://doi.org/10.6028/NBS.NSRDS.61p5")},
        {QStringLiteral("material_properties"), materialProperties},
        {QStringLiteral("material_property_statistic"), QStringLiteral("source table mean values; solver uses central values and does not propagate uncertainty")},
        {QStringLiteral("analysis_temperature_k"), runs.back().provenance.analysisTemperatureK.value_or(0.0)},
        {QStringLiteral("youngs_modulus_pa"), youngsModulusPa},
        {QStringLiteral("analytical_euler_bernoulli_tip_m"), analyticalTipM},
        {QStringLiteral("analytical_midspan_section_stress_pa"), analyticalSectionStressPa},
        {QStringLiteral("mesh_refinement"), refinement},
        {QStringLiteral("structural_rom"), QJsonObject{
            {QStringLiteral("model_id"), QString::fromStdString(rom.id)},
            {QStringLiteral("source_project_id"), QString::fromStdString(rom.sourceProjectId)},
            {QStringLiteral("cad_object_id"), QString::fromStdString(rom.cadObjectId)},
            {QStringLiteral("material_definition_id"), QString::fromStdString(rom.materialDefinitionId)},
            {QStringLiteral("equation_definition_id"), QString::fromStdString(rom.equationDefinitionId)},
            {QStringLiteral("solver_id"), QString::fromStdString(rom.solverId)},
            {QStringLiteral("solver_version"), QString::fromStdString(rom.solverVersion)},
            {QStringLiteral("mesh_revision_id"), QString::fromStdString(rom.mesh.revisionId)},
            {QStringLiteral("mesh_element_count"), static_cast<double>(rom.mesh.elementCount)},
            {QStringLiteral("applied_force_direction_unit"), QJsonArray{
                rom.appliedForceDirectionUnit[0], rom.appliedForceDirectionUnit[1], rom.appliedForceDirectionUnit[2]}},
            {QStringLiteral("topology_reference_ids"), [&rom] {
                QJsonArray ids;
                for (const std::string& id : rom.topologyReferenceIds) ids.append(QString::fromStdString(id));
                return ids;
            }()},
            {QStringLiteral("boundary_condition_ids"), [&rom] {
                QJsonArray ids;
                for (const std::string& id : rom.boundaryConditionIds) ids.append(QString::fromStdString(id));
                return ids;
            }()},
            {QStringLiteral("response_law"), QStringLiteral("tip_displacement_m = normalized_compliance_per_m * force_n / youngs_modulus_pa")},
            {QStringLiteral("normalized_compliance_per_m"), rom.normalizedCompliancePerM},
            {QStringLiteral("input_domain"), QJsonObject{
                {QStringLiteral("minimum_applied_force_n"), rom.inputDomain.minimumAppliedForceN},
                {QStringLiteral("maximum_applied_force_n"), rom.inputDomain.maximumAppliedForceN},
                {QStringLiteral("minimum_youngs_modulus_pa"), rom.inputDomain.minimumYoungsModulusPa},
                {QStringLiteral("maximum_youngs_modulus_pa"), rom.inputDomain.maximumYoungsModulusPa},
            }},
            {QStringLiteral("training_samples"), encodeRomSamples(rom.trainingSamples)},
            {QStringLiteral("held_out_samples"), encodeRomSamples(rom.validationSamples)},
            {QStringLiteral("validation_tolerance_relative"), rom.validationToleranceRelative},
            {QStringLiteral("maximum_relative_validation_error"), rom.maximumRelativeValidationError},
            {QStringLiteral("rms_relative_validation_error"), rom.rmsRelativeValidationError},
            {QStringLiteral("validation_passed"), rom.validationPassed},
            {QStringLiteral("out_of_domain_policy"), QStringLiteral("reject")},
            {QStringLiteral("out_of_domain_inputs_rejected"), rom.rejectsOutOfDomainInputs},
            {QStringLiteral("scope"), QStringLiteral("Bounded static response model for this CAD solid; not a modal or general-purpose ROM.")},
        }},
        {QStringLiteral("mujoco_rom_bridge"), mujocoRomBridge},
        {QStringLiteral("fast_vs_high_fidelity"), fastVsHighFidelity},
        {QStringLiteral("convergence"), QJsonObject{
            {QStringLiteral("compared_runs"), QJsonArray{
                QString::fromStdString(convergence.solverRunIds[convergence.solverRunIds.size() - 2]),
                QString::fromStdString(convergence.solverRunIds.back())}},
            {QStringLiteral("relative_tip_displacement_change"), convergence.relativeTipDisplacementChange},
            {QStringLiteral("relative_strain_energy_change"), convergence.relativeStrainEnergyChange},
            {QStringLiteral("relative_section_stress_change"), convergence.relativeSectionStressChange},
            {QStringLiteral("tolerance"), convergence.tolerance},
            {QStringLiteral("converged"), convergence.converged},
        }},
        {QStringLiteral("metric_definitions"), QJsonObject{
            {QStringLiteral("maximum_displacement"), QStringLiteral("maximum nodal displacement magnitude")},
            {QStringLiteral("maximum_von_mises_stress"), QStringLiteral("maximum element-center von Mises stress; not a yield metric and may reflect ideal clamp/load-edge concentrations")},
            {QStringLiteral("section_axial_stress"), QStringLiteral("absolute axial stress sampled at the reported fixed midspan point; adjacent element values are averaged")},
            {QStringLiteral("mesh_convergence"), QStringLiteral("relative change from the medium mesh to the fine mesh for tip displacement, strain energy, and fixed-point section stress")},
        }},
        {QStringLiteral("scope_limitations"), QJsonArray{
            QStringLiteral("Structured hexahedral meshing currently supports one axis-aligned rectangular OCCT solid with six unsplit planar faces."),
            QStringLiteral("The solve is serial MFEM H1/Q1, static, homogeneous, isotropic, small-strain linear elasticity."),
            QStringLiteral("The cited material compilation reports property percent uncertainties, recorded above, but no verified temperature applicability range; 293.15 K is a fixture reference assumption."),
            QStringLiteral("No experiment or physical machine was used; this report is numerical verification only."),
        }},
    };
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) throw std::runtime_error("could not create 3D elasticity verification report");
    output << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
    if (!output) throw std::runtime_error("could not finish writing 3D elasticity verification report");
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    try {
        datumloop::StructuralAnalysisStudy study;
        std::array<std::string, 3> topologyIds;
        double youngsModulusPa = 0.0;
        datumloop::DatumDocument document = makeDocument(study, topologyIds, youngsModulusPa);
        const auto expectInvalidArgument = [](const std::function<void()>& operation, const char* message) {
            bool rejected = false;
            try {
                operation();
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, message);
        };
        const auto* beam = document.object(study.cadObjectId);
        require(beam != nullptr, "CAD cantilever body was not stored in DatumLoop");
        const datumloop::ShapeMetrics metrics = datumloop::OcctKernel::measure(beam->shape);
        const double lengthM = (metrics.maximum[0] - metrics.minimum[0]) * 1e-3;
        const double heightM = (metrics.maximum[1] - metrics.minimum[1]) * 1e-3;
        const double widthM = (metrics.maximum[2] - metrics.minimum[2]) * 1e-3;
        const double secondMomentM4 = widthM * std::pow(heightM, 3) / 12.0;
        const double forceN = 10.0;
        const double analyticalTipM = forceN * std::pow(lengthM, 3) /
                                      (3.0 * youngsModulusPa * secondMomentM4);
        const double analyticalSectionStressPa =
            forceN * (lengthM / 2.0) * (0.375 * heightM) / secondMomentM4;

        const auto* sourcedMaterial = document.electronicsModel().material(study.materialDefinitionId);
        require(sourcedMaterial != nullptr && sourcedMaterial->sources.size() == 1 &&
                    sourcedMaterial->propertyModels.size() == 3 &&
                    std::all_of(sourcedMaterial->propertyModels.begin(), sourcedMaterial->propertyModels.end(),
                                [](const datumloop::MaterialProperty& property) {
                                    return property.sourceId.has_value() && property.absoluteUncertainty.has_value() &&
                                           !property.minimumTemperatureK.has_value() &&
                                           !property.maximumTemperatureK.has_value();
                                }),
                "material fixture must preserve the cited mean values, source links, uncertainty, and unknown temperature bounds");

        datumloop::DatumDocument unsupportedGeometry("unsupported cylinder fixture");
        const auto& cylinder = unsupportedGeometry.addObject(
            datumloop::createDatumUuidV4(), "cylinder", BRepPrimAPI_MakeCylinder(5.0, 20.0).Shape());
        expectInvalidArgument([&] {
            (void)datumloop::OcctPrismAnalysisMesher::generate(
                unsupportedGeometry, cylinder.id, datumloop::AnalysisMeshSettings{});
        }, "rectangular structured mesher accepted an unsupported cylindrical CAD solid");

        datumloop::DatumDocument missingSourceDocument = document;
        datumloop::DatumElectronicsModel missingSourceMaterials = document.electronicsModel();
        const auto missingSourceMaterial = std::find_if(
            missingSourceMaterials.materials.begin(), missingSourceMaterials.materials.end(),
            [&study](const datumloop::MaterialDefinition& material) { return material.id == study.materialDefinitionId; });
        require(missingSourceMaterial != missingSourceMaterials.materials.end(),
                "could not construct missing-source material rejection fixture");
        missingSourceMaterial->propertyModels.front().sourceId.reset();
        missingSourceDocument.setElectronicsModel(std::move(missingSourceMaterials));

        const datumloop::AnalysisMeshSettings settings{0.01, 3.0, 250000};
        const std::array<double, 4> sizesM{0.01, 0.005, 0.0025, 0.00125};
        const datumloop::MfemLinearElasticitySolver solver;
        std::vector<datumloop::StructuralAnalysisRun> runs;
        for (const double size : sizesM) {
            datumloop::AnalysisMeshSettings resolution = settings;
            resolution.maximumElementSizeM = size;
            const datumloop::AnalysisMesh mesh = datumloop::OcctPrismAnalysisMesher::generate(
                document, study.cadObjectId, resolution);
            require(mesh.provenance.elementType == "hexahedron8" && mesh.provenance.nodeCount == mesh.nodes.size(),
                    "CAD analysis mesh omitted element or node provenance");
            if (runs.empty()) {
                datumloop::AnalysisMesh alteredMesh = mesh;
                const auto interior = std::find_if(alteredMesh.nodes.begin(), alteredMesh.nodes.end(),
                    [&metrics](const datumloop::AnalysisNode& node) {
                        return node.positionM[0] > metrics.minimum[0] * 1e-3 &&
                               node.positionM[0] < metrics.maximum[0] * 1e-3 &&
                               node.positionM[1] > metrics.minimum[1] * 1e-3 &&
                               node.positionM[1] < metrics.maximum[1] * 1e-3 &&
                               node.positionM[2] > metrics.minimum[2] * 1e-3 &&
                               node.positionM[2] < metrics.maximum[2] * 1e-3;
                    });
                require(interior != alteredMesh.nodes.end(), "coarse structured mesh has no interior node for mesh-integrity test");
                interior->positionM[1] += 1e-4;
                expectInvalidArgument([&] { (void)solver.solve(document, study, alteredMesh); },
                                      "MFEM solver accepted a mesh that no longer matches current CAD-derived nodes");

                const datumloop::AnalysisMesh unsourcedMesh = datumloop::OcctPrismAnalysisMesher::generate(
                    missingSourceDocument, study.cadObjectId, resolution);
                expectInvalidArgument([&] {
                    (void)solver.solve(missingSourceDocument, study, unsourcedMesh);
                }, "MFEM solver accepted a material property without a MaterialSource link");
            }
            const auto run = solver.solve(document, study, mesh);
            require(run.converged && run.relativeResidual < 1e-8 && run.iterations > 0,
                    "MFEM 3D elasticity did not converge with a recorded residual");
            require(run.meshSnapshot.has_value() &&
                        run.meshSnapshot->nodePositionsM.size() == mesh.nodes.size() &&
                        run.meshSnapshot->hexahedra.size() == mesh.elements.size() &&
                        run.meshSnapshot->meshRevisionId == mesh.provenance.revisionId,
                    "MFEM result did not retain its exact mesh coordinates and element connectivity");
            for (std::size_t node = 0; node < mesh.nodes.size(); ++node) {
                require(run.meshSnapshot->nodePositionsM[node] == mesh.nodes[node].positionM,
                        "persisted mesh snapshot changed a CAD-derived node coordinate");
            }
            for (std::size_t element = 0; element < mesh.elements.size(); ++element) {
                require(run.meshSnapshot->hexahedra[element] == mesh.elements[element].nodeIndices,
                        "persisted mesh snapshot changed an MFEM hexahedron's field indexing");
            }
            require(run.provenance.temperatureRangeVerified == false &&
                        run.provenance.analysisTemperatureK == std::optional<double>(293.15),
                    "material temperature uncertainty was incorrectly presented as verified");
            require(run.relativeForceBalanceError < 1e-8 &&
                        std::abs(run.reactionForceN[1] - 10.0) < 1e-7,
                    "integrated MFEM support reactions do not balance the applied CAD-face force");
            require(run.strainEnergyJ > 0.0 && run.maximumDisplacementM > 0.0 &&
                        run.maximumVonMisesStressPa > 0.0 && run.sectionAxialStressPa > 0.0,
                    "MFEM 3D result omitted positive energy, displacement, or stress metrics");
            const auto& displacement = field(run, datumloop::FieldType::Displacement);
            const auto& strain = field(run, datumloop::FieldType::StrainTensor);
            const auto& stress = field(run, datumloop::FieldType::StressTensor);
            const auto& vonMises = field(run, datumloop::FieldType::VonMisesStress);
            require(displacement.values.size() == mesh.nodes.size() * 3 &&
                        strain.values.size() == mesh.elements.size() * 6 &&
                        stress.values.size() == mesh.elements.size() * 6 &&
                        vonMises.values.size() == mesh.elements.size(),
                    "MFEM field datasets do not map to the generated CAD analysis mesh");
            require(run.provenance.boundaryConditionIds.size() == study.boundaryConditions.size() &&
                        std::find(run.provenance.topologyReferenceIds.begin(),
                                  run.provenance.topologyReferenceIds.end(), topologyIds[0]) !=
                            run.provenance.topologyReferenceIds.end() &&
                        std::find(run.provenance.topologyReferenceIds.begin(),
                                  run.provenance.topologyReferenceIds.end(), topologyIds[1]) !=
                            run.provenance.topologyReferenceIds.end(),
                    "MFEM result provenance omitted persistent CAD face references");
            runs.push_back(run);
        }

        const double tipError = relativeChange(runs.back().tipDisplacementM, analyticalTipM);
        const double stressError = relativeChange(runs.back().sectionAxialStressPa, analyticalSectionStressPa);
        require(tipError < 0.08,
                ("fine MFEM tip displacement is outside the Euler-Bernoulli sanity bound: " +
                 std::to_string(runs.back().tipDisplacementM) + " m vs " + std::to_string(analyticalTipM) +
                 " m (relative error " + std::to_string(tipError) + ")").c_str());
        require(stressError < 0.20,
                ("fine MFEM midspan axial stress is outside the beam sanity bound: " +
                 std::to_string(runs.back().sectionAxialStressPa) + " Pa vs " +
                 std::to_string(analyticalSectionStressPa) + " Pa (relative error " +
                 std::to_string(stressError) + ")").c_str());
        for (std::size_t index = 1; index < runs.size(); ++index) {
            require(relativeChange(runs[index].tipDisplacementM, analyticalTipM) <
                        relativeChange(runs[index - 1].tipDisplacementM, analyticalTipM) &&
                        relativeChange(runs[index].sectionAxialStressPa, analyticalSectionStressPa) <
                        relativeChange(runs[index - 1].sectionAxialStressPa, analyticalSectionStressPa),
                    "mesh refinement did not move the fixed-point displacement and stress toward beam estimates");
        }

        study.runs = runs;
        datumloop::StructuralMeshConvergence convergence;
        for (const auto& run : runs) convergence.solverRunIds.push_back(run.provenance.solverRunId);
        convergence.relativeTipDisplacementChange = relativeChange(runs.back().tipDisplacementM,
                                                                     runs[runs.size() - 2].tipDisplacementM);
        convergence.relativeStrainEnergyChange = relativeChange(runs.back().strainEnergyJ,
                                                                  runs[runs.size() - 2].strainEnergyJ);
        convergence.relativeSectionStressChange = relativeChange(runs.back().sectionAxialStressPa,
                                                                  runs[runs.size() - 2].sectionAxialStressPa);
        convergence.tolerance = 0.05;
        convergence.converged = convergence.relativeTipDisplacementChange <= convergence.tolerance &&
                                convergence.relativeStrainEnergyChange <= convergence.tolerance &&
                                convergence.relativeSectionStressChange <= convergence.tolerance;
        study.meshConvergence = convergence;
        study.validate();
        require(convergence.converged, "fine mesh did not satisfy the recorded 5% convergence tolerance");

        const datumloop::AnalysisMeshSettings romMeshSettings{0.0025, 3.0, 250000};
        const datumloop::AnalysisMesh romBaseMesh = datumloop::OcctPrismAnalysisMesher::generate(
            document, study.cadObjectId, romMeshSettings);
        const std::array<double, 3> romTrainingLoadsN{5.0, 10.0, 15.0};
        const std::array<double, 3> romTrainingModulusFactors{0.973, 1.0, 1.027};
        std::vector<datumloop::StructuralRomHighFidelitySample> romTrainingSamples;
        for (const double loadN : romTrainingLoadsN) {
            for (const double modulusFactor : romTrainingModulusFactors) {
                const double sampledYoungsModulusPa = youngsModulusPa * modulusFactor;
                romTrainingSamples.push_back(runRomSample(
                    document, study, solver, romMeshSettings, romBaseMesh.provenance.revisionId,
                    loadN, sampledYoungsModulusPa));
            }
        }
        const std::array<double, 2> romHeldOutLoadsN{7.5, 12.5};
        const std::array<double, 2> romHeldOutModulusFactors{0.9865, 1.0135};
        std::vector<datumloop::StructuralRomHighFidelitySample> romHeldOutSamples;
        for (const double loadN : romHeldOutLoadsN) {
            for (const double modulusFactor : romHeldOutModulusFactors) {
                romHeldOutSamples.push_back(runRomSample(
                    document, study, solver, romMeshSettings, romBaseMesh.provenance.revisionId,
                    loadN, youngsModulusPa * modulusFactor));
            }
        }
        const datumloop::StructuralElasticityRom rom = datumloop::fitStructuralElasticityRom(
            "datumloop.cad-3d-elasticity.compliance-rom.v1",
            romTrainingSamples,
            romHeldOutSamples,
            0.01);
        require(rom.validationPassed && rom.trainingSamples.size() == 9 &&
                    rom.validationSamples.size() == 4 && rom.maximumRelativeValidationError < 0.01 &&
                    rom.rmsRelativeValidationError < 0.01 &&
                    rom.predictEffectiveStiffness(10.0, youngsModulusPa) > 0.0,
                "MFEM-sampled 3D structural ROM did not pass its held-out load/modulus grid");
        bool extrapolationRejected = false;
        try {
            (void)rom.predictTipDisplacement(16.0, youngsModulusPa);
        } catch (const std::out_of_range&) {
            extrapolationRejected = true;
        }
        require(extrapolationRejected && rom.rejectsOutOfDomainInputs,
                "3D structural ROM accepted an applied load outside its validated domain");

        QFile mujocoReportFile(QString::fromUtf8(DATUMLOOP_MUJOCO_RUNTIME_REPORT));
        require(mujocoReportFile.open(QIODevice::ReadOnly),
                "ROM bridge could not read the verified MuJoCo runtime report");
        const QJsonObject mujocoReport = QJsonDocument::fromJson(mujocoReportFile.readAll()).object();
        require(mujocoReport.value(QStringLiteral("backend")).toString() == QStringLiteral("MuJoCo") &&
                    mujocoReport.value(QStringLiteral("runtime")).toString() == QStringLiteral("passed"),
                "ROM bridge requires a passing MuJoCo rigid-body runtime record");
        const double mujocoPositionM = mujocoReport.value(QStringLiteral("final_position_m")).toDouble();
        const double mujocoVelocityMPerS = mujocoReport.value(QStringLiteral("final_velocity_m_per_s")).toDouble();
        require(std::isfinite(mujocoPositionM) && std::isfinite(mujocoVelocityMPerS),
                "ROM bridge requires finite rigid position and velocity from MuJoCo");
        datumloop::RobotObservation rigidObservation;
        rigidObservation.timeS = mujocoReport.value(QStringLiteral("steps")).toDouble() * 0.01;
        datumloop::RobotSensorObservation jointPosition;
        jointPosition.sensorId = "mujoco.slider_x.position";
        jointPosition.valid = true;
        jointPosition.value = mujocoPositionM;
        jointPosition.unit = "m";
        jointPosition.captureTimeS = rigidObservation.timeS;
        jointPosition.deliveryTimeS = rigidObservation.timeS;
        datumloop::RobotSensorObservation jointVelocity;
        jointVelocity.sensorId = "mujoco.slider_x.velocity";
        jointVelocity.valid = true;
        jointVelocity.value = mujocoVelocityMPerS;
        jointVelocity.unit = "m/s";
        jointVelocity.captureTimeS = rigidObservation.timeS;
        jointVelocity.deliveryTimeS = rigidObservation.timeS;
        rigidObservation.sensors = {jointPosition, jointVelocity};

        std::vector<datumloop::StructuralResponseComparisonSample> romVsMfemComparisons;
        std::vector<datumloop::StructuralResponseComparisonSample> mujocoPlusRomVsMfemComparisons;
        std::vector<datumloop::StructuralResponseComparisonSample> mujocoPlusRomVsRomComparisons;
        QJsonArray heldOutOperatingPoints;
        for (const datumloop::StructuralRomSampleRecord& sample : rom.validationSamples) {
            datumloop::RobotObservation combinedObservation = rigidObservation;
            datumloop::Vector3 appliedForceN{
                rom.appliedForceDirectionUnit[0] * sample.appliedForceMagnitudeN,
                rom.appliedForceDirectionUnit[1] * sample.appliedForceMagnitudeN,
                rom.appliedForceDirectionUnit[2] * sample.appliedForceMagnitudeN,
            };
            datumloop::addStructuralResponseObservation(combinedObservation, rom, appliedForceN,
                                                        sample.youngsModulusPa);
            require(preservesRigidObservation(combinedObservation, rigidObservation) &&
                        combinedObservation.structuralResponse.has_value() &&
                        combinedObservation.structuralResponse->romId == rom.id,
                    "held-out MuJoCo+ROM observation changed rigid sensors or omitted its structural response");

            const double romDeflectionM = rom.predictTipDisplacement(
                sample.appliedForceMagnitudeN, sample.youngsModulusPa);
            const double integratedDeflectionM =
                combinedObservation.structuralResponse->tipDeflectionMagnitudeM;
            require(std::abs(romDeflectionM - sample.romTipDisplacementM) <= 1e-15 &&
                        std::abs(integratedDeflectionM - romDeflectionM) <= 1e-15,
                    "MuJoCo+ROM held-out response diverged from the bounded ROM prediction");

            romVsMfemComparisons.push_back({sample.appliedForceMagnitudeN, sample.youngsModulusPa,
                                           sample.highFidelityTipDisplacementM, romDeflectionM,
                                           sample.solverRunId});
            mujocoPlusRomVsMfemComparisons.push_back({sample.appliedForceMagnitudeN, sample.youngsModulusPa,
                                                     sample.highFidelityTipDisplacementM, integratedDeflectionM,
                                                     sample.solverRunId});
            mujocoPlusRomVsRomComparisons.push_back({sample.appliedForceMagnitudeN, sample.youngsModulusPa,
                                                    romDeflectionM, integratedDeflectionM,
                                                    sample.solverRunId});
            const datumloop::StructuralResponseComparisonSample romPrediction{
                sample.appliedForceMagnitudeN, sample.youngsModulusPa,
                sample.highFidelityTipDisplacementM, romDeflectionM,
                sample.solverRunId};
            const datumloop::StructuralResponseComparisonSample integratedPrediction{
                sample.appliedForceMagnitudeN, sample.youngsModulusPa,
                sample.highFidelityTipDisplacementM, integratedDeflectionM,
                sample.solverRunId};
            heldOutOperatingPoints.append(QJsonObject{
                {QStringLiteral("applied_force_n"), sample.appliedForceMagnitudeN},
                {QStringLiteral("youngs_modulus_pa"), sample.youngsModulusPa},
                {QStringLiteral("mfem_solver_run_id"), QString::fromStdString(sample.solverRunId)},
                {QStringLiteral("material_revision"), QString::fromStdString(sample.materialRevision)},
                {QStringLiteral("mfem_tip_displacement_m"), sample.highFidelityTipDisplacementM},
                {QStringLiteral("rom_tip_displacement_m"), romDeflectionM},
                {QStringLiteral("mujoco_plus_rom_tip_deflection_m"), integratedDeflectionM},
                {QStringLiteral("rom_relative_error_vs_mfem"),
                 datumloop::relativeTipDeflectionError(romPrediction)},
                {QStringLiteral("mujoco_plus_rom_relative_error_vs_mfem"),
                 datumloop::relativeTipDeflectionError(integratedPrediction)},
                {QStringLiteral("mujoco_plus_rom_relative_error_vs_rom"),
                 datumloop::relativeTipDeflectionError({sample.appliedForceMagnitudeN, sample.youngsModulusPa,
                                                        romDeflectionM, integratedDeflectionM, sample.solverRunId})},
            });
        }
        const datumloop::StructuralResponseErrorMetrics romVsMfemMetrics =
            datumloop::compareStructuralTipDeflections(romVsMfemComparisons);
        const datumloop::StructuralResponseErrorMetrics mujocoPlusRomVsMfemMetrics =
            datumloop::compareStructuralTipDeflections(mujocoPlusRomVsMfemComparisons);
        const datumloop::StructuralResponseErrorMetrics mujocoPlusRomVsRomMetrics =
            datumloop::compareStructuralTipDeflections(mujocoPlusRomVsRomComparisons);
        require(romVsMfemMetrics.sampleCount == rom.validationSamples.size() &&
                    mujocoPlusRomVsMfemMetrics.maximumRelativeError <= rom.validationToleranceRelative &&
                    mujocoPlusRomVsRomMetrics.maximumRelativeError <= 1e-12,
                "held-out MFEM, ROM, and MuJoCo+ROM comparison exceeded its declared response tolerance");
        const QJsonObject fastVsHighFidelity{
            {QStringLiteral("comparison_quantity"), QStringLiteral("static tip deflection magnitude")},
            {QStringLiteral("reference_backend"), QString::fromStdString(rom.solverId)},
            {QStringLiteral("reference_solver_version"), QString::fromStdString(rom.solverVersion)},
            {QStringLiteral("fast_backend"), QStringLiteral("MuJoCo rigid runtime plus DatumLoop bounded structural ROM")},
            {QStringLiteral("relative_error_definition"),
             QStringLiteral("absolute(predicted - MFEM) / abs(MFEM); mean is arithmetic and RMS is root-mean-square")},
            {QStringLiteral("validation_tolerance_relative"), rom.validationToleranceRelative},
            {QStringLiteral("rigid_observation_preserved"), true},
            {QStringLiteral("held_out_operating_points"), heldOutOperatingPoints},
            {QStringLiteral("rom_vs_mfem"), encodeTipResponseErrorMetrics(romVsMfemMetrics)},
            {QStringLiteral("mujoco_plus_rom_vs_mfem"),
             encodeTipResponseErrorMetrics(mujocoPlusRomVsMfemMetrics)},
            {QStringLiteral("mujoco_plus_rom_vs_rom"),
             encodeTipResponseErrorMetrics(mujocoPlusRomVsRomMetrics)},
        };

        datumloop::RobotObservation lightLoadObservation = rigidObservation;
        datumloop::addStructuralResponseObservation(lightLoadObservation, rom, {0.0, -5.0, 0.0}, youngsModulusPa);
        datumloop::RobotObservation heavyLoadObservation = rigidObservation;
        datumloop::addStructuralResponseObservation(heavyLoadObservation, rom, {0.0, -15.0, 0.0}, youngsModulusPa);
        require(lightLoadObservation.sensors.size() == rigidObservation.sensors.size() &&
                    heavyLoadObservation.sensors.size() == rigidObservation.sensors.size() &&
                    lightLoadObservation.sensors[0].value == rigidObservation.sensors[0].value &&
                    heavyLoadObservation.sensors[1].value == rigidObservation.sensors[1].value &&
                    lightLoadObservation.structuralResponse.has_value() &&
                    heavyLoadObservation.structuralResponse.has_value() &&
                    lightLoadObservation.structuralResponse->romId == rom.id &&
                    heavyLoadObservation.structuralResponse->tipDeflectionMagnitudeM >
                        lightLoadObservation.structuralResponse->tipDeflectionMagnitudeM &&
                    std::abs(heavyLoadObservation.structuralResponse->tipDeflectionMagnitudeM -
                             rom.predictTipDisplacement(15.0, youngsModulusPa)) < 1e-15,
                "MuJoCo rigid observations must be preserved while the FEM ROM adds load-dependent flex deflection");
        bool incompatibleLoadDirectionRejected = false;
        try {
            datumloop::RobotObservation wrongDirectionObservation = rigidObservation;
            datumloop::addStructuralResponseObservation(wrongDirectionObservation, rom,
                                                        {5.0, 0.0, 0.0}, youngsModulusPa);
        } catch (const std::invalid_argument&) {
            incompatibleLoadDirectionRejected = true;
        }
        require(incompatibleLoadDirectionRejected,
                "MuJoCo/ROM bridge accepted a force direction that differs from the trained ROM");
        const QJsonObject mujocoRomBridge{
            {QStringLiteral("backend"), mujocoReport.value(QStringLiteral("backend"))},
            {QStringLiteral("version"), mujocoReport.value(QStringLiteral("version"))},
            {QStringLiteral("runtime_status"), mujocoReport.value(QStringLiteral("runtime"))},
            {QStringLiteral("rigid_position_m"), mujocoPositionM},
            {QStringLiteral("rigid_velocity_m_per_s"), mujocoVelocityMPerS},
            {QStringLiteral("rigid_dynamics_modified"), false},
            {QStringLiteral("rom_id"), QString::fromStdString(rom.id)},
            {QStringLiteral("load_direction_unit"), QJsonArray{
                rom.appliedForceDirectionUnit[0], rom.appliedForceDirectionUnit[1], rom.appliedForceDirectionUnit[2]}},
            {QStringLiteral("youngs_modulus_pa"), youngsModulusPa},
            {QStringLiteral("observations"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("applied_force_n"), lightLoadObservation.structuralResponse->appliedForceMagnitudeN},
                    {QStringLiteral("joint_position_m"), lightLoadObservation.sensors[0].value.value()},
                    {QStringLiteral("joint_velocity_m_per_s"), lightLoadObservation.sensors[1].value.value()},
                    {QStringLiteral("tip_deflection_m"), lightLoadObservation.structuralResponse->tipDeflectionMagnitudeM},
                    {QStringLiteral("effective_stiffness_n_per_m"), lightLoadObservation.structuralResponse->effectiveStiffnessNPerM}},
                QJsonObject{
                    {QStringLiteral("applied_force_n"), heavyLoadObservation.structuralResponse->appliedForceMagnitudeN},
                    {QStringLiteral("joint_position_m"), heavyLoadObservation.sensors[0].value.value()},
                    {QStringLiteral("joint_velocity_m_per_s"), heavyLoadObservation.sensors[1].value.value()},
                    {QStringLiteral("tip_deflection_m"), heavyLoadObservation.structuralResponse->tipDeflectionMagnitudeM},
                    {QStringLiteral("effective_stiffness_n_per_m"), heavyLoadObservation.structuralResponse->effectiveStiffnessNPerM}},
            }},
        };

        datumloop::DatumEngineeringModel engineering = document.engineeringModel();
        engineering.structuralStudies = {study};
        document.setEngineeringModel(std::move(engineering));
        const auto* persistedStudy = &document.engineeringModel().structuralStudies.front();
        require(persistedStudy->runs.size() == sizesM.size() && persistedStudy->meshConvergence.has_value(),
                "document did not retain structural runs and convergence data");

        QTemporaryDir temporaryDirectory;
        require(temporaryDirectory.isValid(), "could not create temporary directory for DLD round trip");
        const QString projectPath = temporaryDirectory.filePath(QStringLiteral("cad-elasticity.dld"));
        datumloop::DldProjectStore::save(document, projectPath);
        const datumloop::DatumDocument loaded = datumloop::DldProjectStore::load(projectPath);
        require(loaded.engineeringModel().structuralStudies.size() == 1 &&
                    loaded.engineeringModel().structuralStudies.front() == study &&
                    loaded.electronicsModel().materials == document.electronicsModel().materials &&
                    loaded.electronicsModel().materialStates == document.electronicsModel().materialStates,
                "DLD v8 round trip did not preserve sourced material, state, mesh snapshots, fields, and provenance");

        datumloop::DatumDocument legacySource = document;
        datumloop::DatumEngineeringModel legacyEngineering = legacySource.engineeringModel();
        legacyEngineering.structuralStudies.front().runs = {runs.front()};
        legacyEngineering.structuralStudies.front().meshConvergence.reset();
        legacySource.setEngineeringModel(std::move(legacyEngineering));
        const QString legacyPath = temporaryDirectory.filePath(QStringLiteral("schema-v7-fields.dld"));
        datumloop::DldProjectStore::save(legacySource, legacyPath);
        QFile legacyFile(legacyPath);
        require(legacyFile.open(QIODevice::ReadOnly), "could not open the schema-v7 migration fixture");
        QJsonObject legacyRoot = QJsonDocument::fromJson(legacyFile.readAll()).object();
        legacyFile.close();
        legacyRoot.insert(QStringLiteral("schemaVersion"), 7);
        QJsonObject legacyModel = legacyRoot.value(QStringLiteral("engineeringModel")).toObject();
        QJsonArray legacyStudies = legacyModel.value(QStringLiteral("structuralStudies")).toArray();
        QJsonObject legacyStudy = legacyStudies.at(0).toObject();
        QJsonArray legacyRuns = legacyStudy.value(QStringLiteral("runs")).toArray();
        QJsonObject legacyRun = legacyRuns.at(0).toObject();
        legacyRun.remove(QStringLiteral("meshSnapshot"));
        legacyRuns.replace(0, legacyRun);
        legacyStudy.insert(QStringLiteral("runs"), legacyRuns);
        legacyStudies.replace(0, legacyStudy);
        legacyModel.insert(QStringLiteral("structuralStudies"), legacyStudies);
        legacyRoot.insert(QStringLiteral("engineeringModel"), legacyModel);
        require(legacyFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
                "could not write the schema-v7 migration fixture");
        const QByteArray legacyBytes = QJsonDocument(legacyRoot).toJson(QJsonDocument::Indented);
        require(legacyFile.write(legacyBytes) == legacyBytes.size(),
                "could not finish writing the schema-v7 migration fixture");
        legacyFile.close();
        datumloop::DatumDocument migratedLegacy = datumloop::DldProjectStore::load(legacyPath);
        const auto& migratedRun = migratedLegacy.engineeringModel().structuralStudies.front().runs.front();
        require(!migratedRun.meshSnapshot.has_value() && migratedRun.fields == runs.front().fields &&
                    migratedRun.provenance == runs.front().provenance,
                "schema-v7 structural fields or provenance were not preserved without a mesh snapshot");
        datumloop::DldProjectStore::save(migratedLegacy, legacyPath);
        const datumloop::DatumDocument resavedLegacy = datumloop::DldProjectStore::load(legacyPath);
        require(!resavedLegacy.engineeringModel().structuralStudies.front().runs.front().meshSnapshot.has_value(),
                "resaving a schema-v7 field result did not preserve its unavailable-mesh state");

        if (argc == 3 && std::string(argv[1]) == "--report") {
            writeReport(argv[2], study, runs, *sourcedMaterial, youngsModulusPa,
                        analyticalTipM, analyticalSectionStressPa, rom, mujocoRomBridge,
                        fastVsHighFidelity);
        } else if (argc != 1) {
            throw std::invalid_argument("usage: datumloop-mfem-elasticity-tests [--report path]");
        }
        std::cout << "MFEM " << solver.solverVersion()
                  << " CAD-derived 3D elasticity, mesh convergence, fields, bounded ROM sampling, held-out validation, and DLD provenance passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "MFEM CAD 3D elasticity test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
