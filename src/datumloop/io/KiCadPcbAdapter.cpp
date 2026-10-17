#include "datumloop/io/ElectronicsAdapter.hpp"

#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace datumloop {
namespace {

[[noreturn]] void fail(const QString& message)
{
    throw std::runtime_error(message.toUtf8().toStdString());
}

std::string uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

struct SExpr {
    bool list = false;
    std::string atom;
    std::vector<SExpr> children;
};

class SExprReader {
public:
    explicit SExprReader(std::string_view source) : source_(source) {}

    SExpr parse()
    {
        skipTrivia();
        SExpr value = parseValue();
        skipTrivia();
        if (position_ != source_.size()) fail(QStringLiteral("Unexpected text after KiCad PCB S-expression."));
        if (!value.list) fail(QStringLiteral("KiCad PCB root must be an S-expression list."));
        return value;
    }

private:
    void skipTrivia()
    {
        while (position_ < source_.size()) {
            const char character = source_[position_];
            if (character == ';') {
                while (position_ < source_.size() && source_[position_] != '\n') ++position_;
            } else if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
                ++position_;
            } else {
                break;
            }
        }
    }

    SExpr parseValue()
    {
        skipTrivia();
        if (position_ >= source_.size()) fail(QStringLiteral("Unexpected end of KiCad PCB S-expression."));
        if (source_[position_] == '(') return parseList();
        if (source_[position_] == ')') fail(QStringLiteral("Unexpected closing parenthesis in KiCad PCB S-expression."));
        return parseAtom();
    }

    SExpr parseList()
    {
        ++position_;
        SExpr result;
        result.list = true;
        while (true) {
            skipTrivia();
            if (position_ >= source_.size()) fail(QStringLiteral("Unclosed KiCad PCB S-expression list."));
            if (source_[position_] == ')') {
                ++position_;
                return result;
            }
            result.children.push_back(parseValue());
        }
    }

    SExpr parseAtom()
    {
        SExpr result;
        if (source_[position_] == '"') {
            ++position_;
            while (position_ < source_.size()) {
                const char character = source_[position_++];
                if (character == '"') return result;
                if (character == '\\' && position_ < source_.size()) {
                    const char escaped = source_[position_++];
                    if (escaped == 'n') result.atom.push_back('\n');
                    else if (escaped == 'r') result.atom.push_back('\r');
                    else if (escaped == 't') result.atom.push_back('\t');
                    else result.atom.push_back(escaped);
                } else {
                    result.atom.push_back(character);
                }
            }
            fail(QStringLiteral("Unclosed quoted string in KiCad PCB S-expression."));
        }
        while (position_ < source_.size()) {
            const char character = source_[position_];
            if (character == '(' || character == ')' || character == ' ' || character == '\t' ||
                character == '\r' || character == '\n') break;
            result.atom.push_back(character);
            ++position_;
        }
        if (result.atom.empty()) fail(QStringLiteral("Empty token in KiCad PCB S-expression."));
        return result;
    }

    std::string_view source_;
    std::size_t position_ = 0;
};

const SExpr* child(const SExpr& parent, std::string_view name)
{
    for (const SExpr& value : parent.children) {
        if (value.list && !value.children.empty() && !value.children.front().list &&
            value.children.front().atom == name) return &value;
    }
    return nullptr;
}

std::vector<const SExpr*> children(const SExpr& parent, std::string_view name)
{
    std::vector<const SExpr*> result;
    for (const SExpr& value : parent.children) {
        if (value.list && !value.children.empty() && !value.children.front().list &&
            value.children.front().atom == name) result.push_back(&value);
    }
    return result;
}

std::string atomAt(const SExpr* expression, std::size_t index, const QString& description, std::string fallback = {})
{
    (void)description;
    if (expression == nullptr || index >= expression->children.size() || expression->children[index].list) return fallback;
    return expression->children[index].atom;
}

double numberAt(const SExpr* expression, std::size_t index, const QString& description, double fallback = 0.0)
{
    const std::string text = atomAt(expression, index, description);
    if (text.empty()) return fallback;
    std::size_t consumed = 0;
    double value = 0.0;
    try { value = std::stod(text, &consumed); }
    catch (...) { fail(QStringLiteral("KiCad field '%1' must be numeric.").arg(description)); }
    if (consumed != text.size() || !std::isfinite(value)) fail(QStringLiteral("KiCad field '%1' must be finite and numeric.").arg(description));
    return value;
}

double nodeNumber(const SExpr& parent, std::string_view name, std::size_t index, const QString& description, double fallback = 0.0)
{
    return numberAt(child(parent, name), index, description, fallback);
}

std::string property(const SExpr& footprint, const std::string& name, std::string fallback = {})
{
    for (const SExpr* value : children(footprint, "property")) {
        if (atomAt(value, 1, QStringLiteral("property name")) == name) {
            return atomAt(value, 2, QStringLiteral("property value"), fallback);
        }
    }
    for (const SExpr* value : children(footprint, "fp_text")) {
        if (atomAt(value, 1, QStringLiteral("fp_text kind")) == (name == "Reference" ? "reference" : "value")) {
            return atomAt(value, 2, QStringLiteral("fp_text value"), fallback);
        }
    }
    return fallback;
}

bool copperLayer(const std::string& name)
{
    return name == "F.Cu" || name == "B.Cu" || (name.starts_with("In") && name.ends_with(".Cu"));
}

Point2M rotateAndTranslate(double xMm, double yMm, double rotationDeg, double centerXmm, double centerYmm)
{
    const double angle = rotationDeg * std::numbers::pi / 180.0;
    return {(centerXmm + xMm * std::cos(angle) - yMm * std::sin(angle)) / 1000.0,
            (centerYmm + xMm * std::sin(angle) + yMm * std::cos(angle)) / 1000.0};
}

}  // namespace

ElectronicsImportResult KiCadPcbAdapter::importBytes(const QByteArray& bytes, std::string boardName)
{
    const std::string source(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    const SExpr root = SExprReader(source).parse();
    if (atomAt(&root, 0, QStringLiteral("root type")) != "kicad_pcb") {
        fail(QStringLiteral("KiCad file root must be kicad_pcb."));
    }
    const SExpr* general = child(root, "general");
    const double thicknessMm = nodeNumber(general == nullptr ? root : *general, "thickness", 1,
                                          QStringLiteral("general.thickness"));
    if (thicknessMm <= 0.0) fail(QStringLiteral("KiCad PCB thickness must be positive."));

    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    auto includePoint = [&](double x, double y) {
        minimumX = std::min(minimumX, x); minimumY = std::min(minimumY, y);
        maximumX = std::max(maximumX, x); maximumY = std::max(maximumY, y);
    };
    for (const SExpr* rectangle : children(root, "gr_rect")) {
        if (atomAt(child(*rectangle, "layer"), 1, QStringLiteral("Edge.Cuts layer")) != "Edge.Cuts") continue;
        includePoint(nodeNumber(*rectangle, "start", 1, QStringLiteral("gr_rect.start.x")),
                     nodeNumber(*rectangle, "start", 2, QStringLiteral("gr_rect.start.y")));
        includePoint(nodeNumber(*rectangle, "end", 1, QStringLiteral("gr_rect.end.x")),
                     nodeNumber(*rectangle, "end", 2, QStringLiteral("gr_rect.end.y")));
    }
    for (const SExpr* line : children(root, "gr_line")) {
        if (atomAt(child(*line, "layer"), 1, QStringLiteral("Edge.Cuts layer")) != "Edge.Cuts") continue;
        includePoint(nodeNumber(*line, "start", 1, QStringLiteral("gr_line.start.x")),
                     nodeNumber(*line, "start", 2, QStringLiteral("gr_line.start.y")));
        includePoint(nodeNumber(*line, "end", 1, QStringLiteral("gr_line.end.x")),
                     nodeNumber(*line, "end", 2, QStringLiteral("gr_line.end.y")));
    }
    if (!std::isfinite(minimumX) || !std::isfinite(minimumY) || maximumX <= minimumX || maximumY <= minimumY) {
        fail(QStringLiteral("KiCad import requires a rectangular Edge.Cuts outline made from gr_rect or gr_line segments."));
    }

    ElectronicsImportResult result;
    DatumElectronicsModel& model = result.model;
    MaterialDefinition laminate;
    laminate.id = uuid();
    laminate.name = "PCB laminate (grade and properties unspecified)";
    laminate.provenance = {"No laminate grade or physical properties were supplied by this KiCad profile."};
    result.model.materials.push_back(laminate);
    MaterialDefinition copper;
    copper.id = uuid();
    copper.name = "Copper (grade and properties unspecified)";
    copper.provenance = {"Copper layer classification only; electrical and thermal properties remain unknown."};
    result.model.materials.push_back(copper);

    PcbDocument pcb;
    pcb.id = uuid();
    pcb.name = boardName;
    pcb.widthM = (maximumX - minimumX) / 1000.0;
    pcb.heightM = (maximumY - minimumY) / 1000.0;
    pcb.thicknessM = thicknessMm / 1000.0;
    pcb.centerM = {(minimumX + maximumX) / 2000.0, (minimumY + maximumY) / 2000.0};
    pcb.boardMaterialDefinitionId = laminate.id;
    pcb.stackup.id = uuid();
    std::unordered_map<std::string, std::string> layerIds;
    const SExpr* layersNode = child(root, "layers");
    if (layersNode != nullptr) {
        for (const SExpr& layer : layersNode->children) {
            if (!layer.list || layer.children.size() < 3) continue;
            const std::string layerName = atomAt(&layer, 1, QStringLiteral("layer name"));
            const std::string layerKind = atomAt(&layer, 2, QStringLiteral("layer type"));
            if (!copperLayer(layerName) || layerKind != "signal") continue;
            PcbLayer nativeLayer;
            nativeLayer.id = uuid();
            nativeLayer.name = layerName;
            nativeLayer.role = PcbLayerRole::Copper;
            nativeLayer.materialDefinitionId = copper.id;
            layerIds.emplace(layerName, nativeLayer.id);
            pcb.stackup.layers.push_back(std::move(nativeLayer));
        }
    }
    if (pcb.stackup.layers.empty()) {
        for (const std::string layerName : {"F.Cu", "B.Cu"}) {
            PcbLayer nativeLayer{uuid(), layerName, PcbLayerRole::Copper, copper.id, {}, {}, {}, {}};
            layerIds.emplace(layerName, nativeLayer.id);
            pcb.stackup.layers.push_back(std::move(nativeLayer));
        }
    }
    auto findLayer = [&](const std::string& name) -> std::string {
        const auto found = layerIds.find(name);
        return found == layerIds.end() ? std::string{} : found->second;
    };

    CircuitDocument circuit;
    circuit.id = uuid();
    circuit.name = boardName + " netlist";
    pcb.circuitDocumentId = circuit.id;
    std::unordered_map<std::string, std::string> netIds;
    for (const SExpr* netNode : children(root, "net")) {
        const std::string number = atomAt(netNode, 1, QStringLiteral("net number"));
        const std::string name = atomAt(netNode, 2, QStringLiteral("net name"), "Net " + number);
        if (number == "0") continue;
        CircuitNet net{uuid(), number, name, name == "GND", {}};
        netIds.emplace(number, net.id);
        circuit.nets.push_back(std::move(net));
    }
    auto getNetId = [&](const SExpr* netNode) -> std::string {
        if (netNode == nullptr) return {};
        const std::string number = atomAt(netNode, 1, QStringLiteral("pad net number"));
        if (number.empty() || number == "0") return {};
        if (const auto found = netIds.find(number); found != netIds.end()) return found->second;
        const std::string name = atomAt(netNode, 2, QStringLiteral("pad net name"), "Net " + number);
        const std::string id = uuid();
        netIds.emplace(number, id);
        circuit.nets.push_back({id, number, name, name == "GND", {}});
        return id;
    };

    for (const SExpr* footprint : children(root, "footprint")) {
        const std::string libraryName = atomAt(footprint, 1, QStringLiteral("footprint library name"));
        const double centerX = nodeNumber(*footprint, "at", 1, QStringLiteral("footprint.at.x"));
        const double centerY = nodeNumber(*footprint, "at", 2, QStringLiteral("footprint.at.y"));
        const double rotation = nodeNumber(*footprint, "at", 3, QStringLiteral("footprint.at.rotation"));
        const std::string layerName = atomAt(child(*footprint, "layer"), 1, QStringLiteral("footprint.layer"), "F.Cu");
        const std::string reference = property(*footprint, "Reference", {});
        const std::string value = property(*footprint, "Value", libraryName.empty() ? "Component" : libraryName);
        const auto padNodes = children(*footprint, "pad");
        if (reference.empty() || padNodes.empty()) {
            result.warnings.push_back("Skipped a footprint without a reference designator or pads.");
            continue;
        }
        CircuitComponent component;
        component.id = uuid();
        component.sourceId = property(*footprint, "Reference", reference);
        component.reference = reference;
        component.displayName = value;
        PcbComponent pcbComponent;
        pcbComponent.id = uuid();
        pcbComponent.sourceId = component.sourceId;
        pcbComponent.reference = reference;
        pcbComponent.footprint = libraryName;
        pcbComponent.circuitComponentId = component.id;
        pcbComponent.centerM = {centerX / 1000.0, centerY / 1000.0};
        pcbComponent.rotationRad = rotation * std::numbers::pi / 180.0;
        pcbComponent.layerName = layerName;
        double minPadX = std::numeric_limits<double>::infinity();
        double maxPadX = -std::numeric_limits<double>::infinity();
        double minPadY = std::numeric_limits<double>::infinity();
        double maxPadY = -std::numeric_limits<double>::infinity();

        for (const SExpr* padNode : padNodes) {
            const std::string padType = atomAt(padNode, 2, QStringLiteral("pad type"));
            if (padType != "smd") {
                result.warnings.push_back("Skipped a non-SMD pad because drill and plated-barrel geometry is not represented yet.");
                continue;
            }
            const std::string padShape = atomAt(padNode, 3, QStringLiteral("pad shape"));
            if (padShape != "rect" && padShape != "circle") {
                result.warnings.push_back("Skipped an unsupported KiCad pad shape: " + padShape + ".");
                continue;
            }
            const std::string number = atomAt(padNode, 1, QStringLiteral("pad number"), "?");
            const double localX = nodeNumber(*padNode, "at", 1, QStringLiteral("pad.at.x"));
            const double localY = nodeNumber(*padNode, "at", 2, QStringLiteral("pad.at.y"));
            const double padRotation = nodeNumber(*padNode, "at", 3, QStringLiteral("pad.at.rotation"));
            const double widthMm = nodeNumber(*padNode, "size", 1, QStringLiteral("pad.size.x"));
            const double heightMm = nodeNumber(*padNode, "size", 2, QStringLiteral("pad.size.y"));
            if (widthMm <= 0.0 || heightMm <= 0.0) {
                result.warnings.push_back("Skipped a KiCad pad with a non-positive size.");
                continue;
            }
            const Point2M position = rotateAndTranslate(localX, localY, rotation, centerX, centerY);
            const double footprintLocalAngle = rotation * std::numbers::pi / 180.0;
            const double rotatedX = localX * std::cos(footprintLocalAngle) - localY * std::sin(footprintLocalAngle);
            const double rotatedY = localX * std::sin(footprintLocalAngle) + localY * std::cos(footprintLocalAngle);
            minPadX = std::min(minPadX, rotatedX - widthMm / 2.0);
            maxPadX = std::max(maxPadX, rotatedX + widthMm / 2.0);
            minPadY = std::min(minPadY, rotatedY - heightMm / 2.0);
            maxPadY = std::max(maxPadY, rotatedY + heightMm / 2.0);

            CircuitPin pin{uuid(), component.id, number, number};
            const std::string netId = getNetId(child(*padNode, "net"));
            component.pinIds.push_back(pin.id);
            if (!netId.empty()) {
                const auto net = std::find_if(circuit.nets.begin(), circuit.nets.end(),
                                              [&](const CircuitNet& item) { return item.id == netId; });
                if (net != circuit.nets.end()) net->pinIds.push_back(pin.id);
            }
            circuit.pins.push_back(pin);
            PcbPad nativePad;
            nativePad.id = uuid();
            nativePad.sourceId = component.sourceId + ":" + number;
            nativePad.componentId = pcbComponent.id;
            nativePad.pinId = pin.id;
            if (!netId.empty()) nativePad.netId = netId;
            nativePad.shape = padShape == "circle" ? PcbPadShape::Circle : PcbPadShape::Rectangle;
            nativePad.centerM = position;
            nativePad.widthM = widthMm / 1000.0;
            nativePad.heightM = heightMm / 1000.0;
            nativePad.rotationRad = (rotation + padRotation) * std::numbers::pi / 180.0;
            for (const SExpr* layers : children(*padNode, "layers")) {
                for (std::size_t index = 1; index < layers->children.size(); ++index) {
                    if (layers->children[index].list) continue;
                    const std::string layerId = findLayer(layers->children[index].atom);
                    if (!layerId.empty() && std::find(nativePad.layerIds.begin(), nativePad.layerIds.end(), layerId) == nativePad.layerIds.end()) {
                        nativePad.layerIds.push_back(layerId);
                    }
                }
            }
            if (nativePad.layerIds.empty()) {
                if (const std::string fallbackLayer = findLayer(layerName); !fallbackLayer.empty()) nativePad.layerIds.push_back(fallbackLayer);
            }
            if (nativePad.layerIds.empty()) {
                result.warnings.push_back("Skipped a pad without a copper layer in the imported stackup.");
                component.pinIds.pop_back();
                circuit.pins.pop_back();
                if (!netId.empty()) {
                    auto net = std::find_if(circuit.nets.begin(), circuit.nets.end(),
                                            [&](const CircuitNet& item) { return item.id == netId; });
                    if (net != circuit.nets.end()) net->pinIds.pop_back();
                }
                continue;
            }
            pcb.pads.push_back(std::move(nativePad));
        }
        if (component.pinIds.empty()) {
            result.warnings.push_back("Skipped a footprint with no supported electrical SMT pads.");
            continue;
        }
        if (std::isfinite(minPadX) && maxPadX > minPadX && maxPadY > minPadY) {
            pcbComponent.widthM = (maxPadX - minPadX) / 1000.0;
            pcbComponent.heightM = (maxPadY - minPadY) / 1000.0;
        } else {
            result.warnings.push_back("Footprint bounds were unavailable; using its supported pad extents.");
            pcbComponent.widthM = 0.001;
            pcbComponent.heightM = 0.001;
        }
        circuit.components.push_back(std::move(component));
        pcb.components.push_back(std::move(pcbComponent));
    }

    for (const SExpr* segment : children(root, "segment")) {
        const std::string layerName = atomAt(child(*segment, "layer"), 1, QStringLiteral("segment.layer"));
        const std::string layerId = findLayer(layerName);
        if (layerId.empty()) {
            result.warnings.push_back("Skipped a segment on a non-copper or unknown KiCad layer.");
            continue;
        }
        const double x1 = nodeNumber(*segment, "start", 1, QStringLiteral("segment.start.x"));
        const double y1 = nodeNumber(*segment, "start", 2, QStringLiteral("segment.start.y"));
        const double x2 = nodeNumber(*segment, "end", 1, QStringLiteral("segment.end.x"));
        const double y2 = nodeNumber(*segment, "end", 2, QStringLiteral("segment.end.y"));
        const double widthMm = nodeNumber(*segment, "width", 1, QStringLiteral("segment.width"));
        if (widthMm <= 0.0) continue;
        const std::string netId = getNetId(child(*segment, "net"));
        PcbTrace trace{uuid(), {}, {}, layerId, widthMm / 1000.0,
                       {{x1 / 1000.0, y1 / 1000.0}, {x2 / 1000.0, y2 / 1000.0}}};
        if (!netId.empty()) trace.netId = netId;
        pcb.traces.push_back(std::move(trace));
    }

    for (const SExpr* viaNode : children(root, "via")) {
        const double x = nodeNumber(*viaNode, "at", 1, QStringLiteral("via.at.x"));
        const double y = nodeNumber(*viaNode, "at", 2, QStringLiteral("via.at.y"));
        const double sizeMm = nodeNumber(*viaNode, "size", 1, QStringLiteral("via.size"));
        const double drillMm = nodeNumber(*viaNode, "drill", 1, QStringLiteral("via.drill"));
        if (sizeMm <= 0.0 || drillMm <= 0.0 || drillMm >= sizeMm) {
            result.warnings.push_back("Skipped a KiCad via with invalid hole or annular dimensions.");
            continue;
        }
        PcbVia via{uuid(), {}, {}, {x / 1000.0, y / 1000.0}, sizeMm / 1000.0, drillMm / 1000.0, {}};
        const std::string netId = getNetId(child(*viaNode, "net"));
        if (!netId.empty()) via.netId = netId;
        for (const SExpr* layers : children(*viaNode, "layers")) {
            for (std::size_t index = 1; index < layers->children.size(); ++index) {
                if (layers->children[index].list) continue;
                const std::string layerId = findLayer(layers->children[index].atom);
                if (!layerId.empty() && std::find(via.layerIds.begin(), via.layerIds.end(), layerId) == via.layerIds.end()) {
                    via.layerIds.push_back(layerId);
                }
            }
        }
        if (via.layerIds.size() >= 2) pcb.vias.push_back(std::move(via));
        else result.warnings.push_back("Skipped a KiCad via not spanning at least two imported copper layers.");
    }

    model.pcbs.push_back(std::move(pcb));
    model.circuits.push_back(std::move(circuit));
    try {
        model.validate();
    } catch (const std::invalid_argument& error) {
        fail(QStringLiteral("Imported KiCad PCB model is invalid: %1").arg(QString::fromUtf8(error.what())));
    }
    return result;
}

}  // namespace datumloop
