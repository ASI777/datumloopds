#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>

#include <dlfcn.h>

#include <cmath>
#include <cctype>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using SendCharCallback = int (*)(char*, int, void*);
using SendStatusCallback = int (*)(char*, int, void*);
using ControlledExitCallback = int (*)(int, bool, bool, int, void*);
using SendDataCallback = int (*)(void*, int, void*);
using SendInitDataCallback = int (*)(void*, int, void*);
using BackgroundThreadCallback = int (*)(bool*, int, void*);
using NgSpiceInit = int (*)(SendCharCallback, SendStatusCallback, ControlledExitCallback,
                            SendDataCallback, SendInitDataCallback, BackgroundThreadCallback, void*);
using NgSpiceCommand = int (*)(char*);

std::string callbackOutput;

int receiveChar(char* text, int, void*)
{
    if (text != nullptr) callbackOutput.append(text, std::strlen(text));
    return 0;
}

int controlledExit(int, bool, bool, int, void*)
{
    return 0;
}

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().toStdString());
}

QString stringField(const QJsonObject& object, const QString& name)
{
    const QJsonValue value = object.value(name);
    if (!value.isString() || value.toString().isEmpty()) fail(QStringLiteral("Missing string field '%1'.").arg(name));
    return value.toString();
}

double numberField(const QJsonObject& object, const QString& name)
{
    const QJsonValue value = object.value(name);
    if (!value.isDouble() || !std::isfinite(value.toDouble())) fail(QStringLiteral("Missing numeric field '%1'.").arg(name));
    return value.toDouble();
}

struct Net {
    QString id;
    QString node;
    bool reference = false;
    int voltageVariable = -1;
};

struct Component {
    QString id;
    QString kind;
    QString reference;
    QString firstNetId;
    QString secondNetId;
    QString elementName;
    double value = 0.0;
    int currentVariable = -1;
};

std::unordered_map<std::string, double> parseVariables(const std::string& output)
{
    std::unordered_map<std::string, double> result;
    std::size_t position = 0;
    while (position < output.size()) {
        const std::size_t voltage = output.find("dlv", position);
        const std::size_t current = output.find("dli", position);
        std::size_t variable = std::string::npos;
        if (voltage == std::string::npos) variable = current;
        else if (current == std::string::npos) variable = voltage;
        else variable = std::min(voltage, current);
        if (variable == std::string::npos) break;
        std::size_t endName = variable + 3;
        while (endName < output.size() && std::isdigit(static_cast<unsigned char>(output[endName]))) ++endName;
        const std::string key = output.substr(variable, endName - variable);
        const std::size_t equals = output.find('=', endName);
        if (equals == std::string::npos) break;
        const std::size_t valueStart = output.find_first_of("+-0123456789.", equals + 1);
        if (valueStart != std::string::npos) {
            try {
                std::size_t used = 0;
                const double parsed = std::stod(output.substr(valueStart), &used);
                if (used > 0 && std::isfinite(parsed)) result[key] = parsed;
            } catch (...) {
            }
        }
        position = endName;
    }
    return result;
}

QJsonObject run(const QJsonObject& request)
{
    if (request.value(QStringLiteral("analysis")).toString() != QStringLiteral("dc_operating_point")) {
        fail(QStringLiteral("Only DC operating point analysis is implemented."));
    }
    const QJsonValue netsValue = request.value(QStringLiteral("nets"));
    const QJsonValue componentsValue = request.value(QStringLiteral("components"));
    if (!netsValue.isArray() || !componentsValue.isArray()) fail(QStringLiteral("Circuit request must include net and component arrays."));

    std::vector<Net> nets;
    std::unordered_map<std::string, std::size_t> netIndexById;
    int nodeIndex = 0;
    bool hasReference = false;
    for (const QJsonValue& value : netsValue.toArray()) {
        if (!value.isObject()) fail(QStringLiteral("Circuit net entry is invalid."));
        const QJsonObject item = value.toObject();
        Net net;
        net.id = stringField(item, QStringLiteral("id"));
        net.reference = item.value(QStringLiteral("referenceNode")).toBool(false);
        if (net.reference) {
            net.node = QStringLiteral("0");
            hasReference = true;
        } else {
            net.node = QStringLiteral("dl_n%1").arg(nodeIndex++);
            net.voltageVariable = nodeIndex - 1;
        }
        netIndexById.emplace(net.id.toStdString(), nets.size());
        nets.push_back(net);
    }
    if (!hasReference) fail(QStringLiteral("Circuit requires an explicitly selected reference node."));

    std::vector<Component> components;
    std::string netlist = "* DatumLoop native DC operating point\n";
    int resistorIndex = 0;
    int sourceIndex = 0;
    int currentVariable = 0;
    for (const QJsonValue& value : componentsValue.toArray()) {
        if (!value.isObject()) fail(QStringLiteral("Circuit component entry is invalid."));
        const QJsonObject item = value.toObject();
        Component component;
        component.id = stringField(item, QStringLiteral("id"));
        component.reference = stringField(item, QStringLiteral("reference"));
        component.kind = stringField(item, QStringLiteral("kind"));
        component.value = numberField(item, QStringLiteral("value"));
        const QString unit = stringField(item, QStringLiteral("unit"));
        const QJsonValue pinNetsValue = item.value(QStringLiteral("pinNets"));
        if (!pinNetsValue.isArray() || pinNetsValue.toArray().size() != 2 ||
            !pinNetsValue.toArray().at(0).isString() || !pinNetsValue.toArray().at(1).isString()) {
            fail(QStringLiteral("Component '%1' must have two pins assigned to nets.").arg(component.reference));
        }
        component.firstNetId = pinNetsValue.toArray().at(0).toString();
        component.secondNetId = pinNetsValue.toArray().at(1).toString();
        const auto first = netIndexById.find(component.firstNetId.toStdString());
        const auto second = netIndexById.find(component.secondNetId.toStdString());
        if (first == netIndexById.end() || second == netIndexById.end()) {
            fail(QStringLiteral("Component '%1' references an unknown net.").arg(component.reference));
        }
        const QString nodeA = nets[first->second].node;
        const QString nodeB = nets[second->second].node;
        if (nodeA == nodeB) fail(QStringLiteral("Component '%1' pins resolve to the same node.").arg(component.reference));
        if (component.kind == QStringLiteral("resistor")) {
            if (unit != QStringLiteral("ohm") || component.value <= 0.0) {
                fail(QStringLiteral("Resistor '%1' requires a positive value in ohms.").arg(component.reference));
            }
            component.elementName = QStringLiteral("Rdl%1").arg(resistorIndex++);
            netlist += component.elementName.toStdString() + " " + nodeA.toStdString() + " " + nodeB.toStdString() + " " +
                       std::to_string(component.value) + "\n";
        } else if (component.kind == QStringLiteral("voltage_source")) {
            if (unit != QStringLiteral("V")) fail(QStringLiteral("Voltage source '%1' requires a value in volts.").arg(component.reference));
            component.elementName = QStringLiteral("Vdl%1").arg(sourceIndex++);
            component.currentVariable = currentVariable++;
            netlist += component.elementName.toStdString() + " " + nodeA.toStdString() + " " + nodeB.toStdString() + " DC " +
                       std::to_string(component.value) + "\n";
        } else {
            fail(QStringLiteral("Component '%1' has unsupported type '%2'; this backend supports voltage sources and resistors.")
                     .arg(component.reference, component.kind));
        }
        components.push_back(std::move(component));
    }
    if (components.empty()) fail(QStringLiteral("Circuit has no supported components."));

    netlist += ".control\nop\n";
    for (const Net& net : nets) {
        if (net.reference) continue;
        netlist += "let dlv" + std::to_string(net.voltageVariable) + " = v(" + net.node.toStdString() + ")\n";
        netlist += "print dlv" + std::to_string(net.voltageVariable) + "\n";
    }
    for (const Component& component : components) {
        if (component.kind != QStringLiteral("voltage_source")) continue;
        netlist += "let dli" + std::to_string(component.currentVariable) + " = i(" + component.elementName.toStdString() + ")\n";
        netlist += "print dli" + std::to_string(component.currentVariable) + "\n";
    }
    netlist += ".endc\n.end\n";

    QTemporaryFile temporaryNetlist;
    if (!temporaryNetlist.open()) fail(QStringLiteral("Could not create temporary ngspice input."));
    const QByteArray netlistBytes = QByteArray::fromStdString(netlist);
    if (temporaryNetlist.write(netlistBytes) != netlistBytes.size() || !temporaryNetlist.flush()) {
        fail(QStringLiteral("Could not write temporary ngspice input."));
    }
    const QByteArray path = temporaryNetlist.fileName().toUtf8();

    void* library = dlopen("libngspice.so.0", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char* error = dlerror();
        fail(QStringLiteral("libngspice.so.0 is not available: %1")
                 .arg(QString::fromUtf8(error == nullptr ? "dynamic loader failed" : error)));
    }
    auto init = reinterpret_cast<NgSpiceInit>(dlsym(library, "ngSpice_Init"));
    auto command = reinterpret_cast<NgSpiceCommand>(dlsym(library, "ngSpice_Command"));
    if (init == nullptr || command == nullptr) {
        dlclose(library);
        fail(QStringLiteral("ngspice shared library is missing required API symbols."));
    }
    callbackOutput.clear();
    if (init(&receiveChar, nullptr, &controlledExit, nullptr, nullptr, nullptr, nullptr) != 0) {
        dlclose(library);
        fail(QStringLiteral("ngspice shared-library initialization failed."));
    }
    QByteArray sourceCommand = QByteArray("source ") + path;
    if (command(sourceCommand.data()) != 0) {
        const QString diagnostics = QString::fromStdString(callbackOutput).trimmed();
        dlclose(library);
        fail(QStringLiteral("ngspice did not complete the operating point: %1").arg(diagnostics));
    }
    const auto variables = parseVariables(callbackOutput);
    QJsonObject voltageOutput;
    for (const Net& net : nets) {
        double voltage = 0.0;
        if (!net.reference) {
            const std::string key = "dlv" + std::to_string(net.voltageVariable);
            const auto found = variables.find(key);
            if (found == variables.end()) {
                const QString diagnostics = QString::fromStdString(callbackOutput).trimmed();
                dlclose(library);
                fail(QStringLiteral("ngspice omitted the voltage for net '%1': %2")
                         .arg(net.id, diagnostics));
            }
            voltage = found->second;
        }
        voltageOutput.insert(net.id, voltage);
    }
    QJsonObject currentOutput;
    for (const Component& component : components) {
        if (component.kind != QStringLiteral("voltage_source")) continue;
        const std::string key = "dli" + std::to_string(component.currentVariable);
        const auto found = variables.find(key);
        if (found == variables.end()) {
            const QString diagnostics = QString::fromStdString(callbackOutput).trimmed();
            dlclose(library);
            fail(QStringLiteral("ngspice omitted the current for source '%1': %2")
                     .arg(component.reference, diagnostics));
        }
        currentOutput.insert(component.id, found->second);
    }
    dlclose(library);
    return {{QStringLiteral("netVoltagesV"), voltageOutput},
            {QStringLiteral("voltageSourceCurrentsA"), currentOutput}};
}

}  // namespace

int main()
{
    std::string requestBytes((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    QJsonParseError parseError;
    const QJsonDocument request = QJsonDocument::fromJson(QByteArray::fromStdString(requestBytes), &parseError);
    QJsonObject response;
    try {
        if (parseError.error != QJsonParseError::NoError || !request.isObject()) {
            fail(QStringLiteral("Could not parse worker request: %1").arg(parseError.errorString()));
        }
        response = run(request.object());
    } catch (const std::exception& error) {
        response = {{QStringLiteral("error"), QString::fromUtf8(error.what())}};
    }
    std::cout << QJsonDocument(response).toJson(QJsonDocument::Compact).constData() << '\n';
    return 0;
}
