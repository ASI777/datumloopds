#include "datumloop/solver/NgspiceProcessCircuitSolver.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <cmath>
#include <stdexcept>

namespace datumloop {
namespace {

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().toStdString());
}

QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

double resultNumber(const QJsonValue& value, const QString& field)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        fail(QStringLiteral("ngspice worker result field '%1' is missing or invalid.").arg(field));
    }
    return value.toDouble();
}

}  // namespace

NgspiceProcessCircuitSolver::NgspiceProcessCircuitSolver(QString workerExecutable, int timeoutMilliseconds)
    : workerExecutable_(std::move(workerExecutable)), timeoutMilliseconds_(timeoutMilliseconds)
{
    if (workerExecutable_.trimmed().isEmpty() || timeoutMilliseconds_ <= 0) {
        throw std::invalid_argument("ngspice worker path and positive timeout are required");
    }
}

CircuitSimulationResult NgspiceProcessCircuitSolver::solveDcOperatingPoint(const CircuitDocument& circuit) const
{
    circuit.validate();
    QJsonArray nets;
    for (const CircuitNet& net : circuit.nets) {
        nets.append(QJsonObject{{QStringLiteral("id"), fromUtf8(net.id)},
                                {QStringLiteral("name"), fromUtf8(net.name)},
                                {QStringLiteral("referenceNode"), net.referenceNode}});
    }
    QJsonArray components;
    for (const CircuitComponent& component : circuit.components) {
        QJsonArray pinNets;
        for (const std::string& pinId : component.pinIds) {
            const CircuitNet* net = circuit.netForPin(pinId);
            pinNets.append(net == nullptr ? QJsonValue(QJsonValue::Null) : QJsonValue(fromUtf8(net->id)));
        }
        components.append(QJsonObject{{QStringLiteral("id"), fromUtf8(component.id)},
                                      {QStringLiteral("reference"), fromUtf8(component.reference)},
                                      {QStringLiteral("kind"), fromUtf8(circuitComponentKindName(component.kind))},
                                      {QStringLiteral("value"), component.electricalValue.has_value()
                                           ? QJsonValue(*component.electricalValue) : QJsonValue(QJsonValue::Null)},
                                      {QStringLiteral("unit"), fromUtf8(component.electricalValueUnit)},
                                      {QStringLiteral("pinNets"), pinNets}});
    }
    const QByteArray request = QJsonDocument(QJsonObject{
        {QStringLiteral("analysis"), QStringLiteral("dc_operating_point")},
        {QStringLiteral("nets"), nets}, {QStringLiteral("components"), components},
    }).toJson(QJsonDocument::Compact);

    QProcess process;
    process.setProgram(workerExecutable_);
    process.start(QIODevice::ReadWrite);
    if (!process.waitForStarted(timeoutMilliseconds_)) {
        fail(QStringLiteral("Could not start ngspice worker '%1': %2").arg(workerExecutable_, process.errorString()));
    }
    if (process.write(request) != request.size()) {
        process.kill();
        process.waitForFinished();
        fail(QStringLiteral("Could not write the circuit request to the ngspice worker."));
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMilliseconds_)) {
        process.kill();
        process.waitForFinished();
        fail(QStringLiteral("ngspice DC operating point exceeded the %1 ms worker timeout.").arg(timeoutMilliseconds_));
    }
    const QByteArray responseBytes = process.readAllStandardOutput();
    const QByteArray diagnostics = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        fail(QStringLiteral("ngspice worker failed: %1").arg(QString::fromUtf8(diagnostics).trimmed()));
    }
    QJsonParseError parseError;
    const QJsonDocument response = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
        fail(QStringLiteral("Could not parse ngspice worker response: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = response.object();
    if (root.value(QStringLiteral("error")).isString()) {
        fail(QStringLiteral("ngspice DC operating point failed: %1").arg(root.value(QStringLiteral("error")).toString()));
    }
    const QJsonObject voltages = root.value(QStringLiteral("netVoltagesV")).toObject();
    const QJsonObject sourceCurrents = root.value(QStringLiteral("voltageSourceCurrentsA")).toObject();

    CircuitSimulationResult result;
    result.backend = "ngspice shared library worker";
    result.analysis = "DC operating point";
    for (const CircuitNet& net : circuit.nets) {
        result.netVoltagesV.emplace(net.id, resultNumber(voltages.value(fromUtf8(net.id)), fromUtf8(net.id)));
    }
    for (const CircuitComponent& component : circuit.components) {
        if (component.pinIds.size() != 2) fail(QStringLiteral("Component '%1' does not have two circuit pins.").arg(fromUtf8(component.reference)));
        const CircuitNet* firstNet = circuit.netForPin(component.pinIds[0]);
        const CircuitNet* secondNet = circuit.netForPin(component.pinIds[1]);
        if (firstNet == nullptr || secondNet == nullptr) {
            fail(QStringLiteral("Component '%1' has a pin that is not assigned to a net.").arg(fromUtf8(component.reference)));
        }
        const double voltageDrop = result.netVoltagesV.at(firstNet->id) - result.netVoltagesV.at(secondNet->id);
        double current = 0.0;
        if (component.kind == CircuitComponentKind::Resistor) {
            if (!component.electricalValue.has_value() || component.electricalValueUnit != "ohm") {
                fail(QStringLiteral("Resistor '%1' requires a resistance in ohms.").arg(fromUtf8(component.reference)));
            }
            current = voltageDrop / *component.electricalValue;
        } else if (component.kind == CircuitComponentKind::VoltageSource) {
            current = resultNumber(sourceCurrents.value(fromUtf8(component.id)), fromUtf8(component.reference));
        } else {
            fail(QStringLiteral("Component '%1' is not supported by the ngspice DC resistor/source backend.")
                     .arg(fromUtf8(component.reference)));
        }
        const double power = voltageDrop * current;
        result.components.push_back({component.id, voltageDrop, current, power});
        if (component.kind == CircuitComponentKind::Resistor) {
            result.thermalSourcePlaceholders.push_back({component.id, std::max(0.0, power), false,
                "Resistor dissipation is available as a source estimate; no temperature or thermal field was solved."});
        }
    }
    return result;
}

}  // namespace datumloop
