#include "datumloop/geometry/PcbShapeBuilder.hpp"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Compound.hxx>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace datumloop {
namespace {

constexpr double MmPerM = 1000.0;
constexpr double DisplayCopperThicknessMm = 0.035;

struct LayerPlacement {
    bool bottom = false;
    bool inner = false;
    double copperThicknessMm = DisplayCopperThicknessMm;
};

TopoDS_Shape prismRectangle(double cx,
                           double cy,
                           double z,
                           double width,
                           double height,
                           double rotation,
                           double thickness)
{
    const double cosine = std::cos(rotation);
    const double sine = std::sin(rotation);
    const std::array<Point2M, 4> corners{{
        {-width / 2.0, -height / 2.0}, {width / 2.0, -height / 2.0},
        {width / 2.0, height / 2.0}, {-width / 2.0, height / 2.0}}};
    BRepBuilderAPI_MakePolygon polygon;
    for (const Point2M& corner : corners) {
        polygon.Add(gp_Pnt(cx + corner[0] * cosine - corner[1] * sine,
                           cy + corner[0] * sine + corner[1] * cosine, z));
    }
    polygon.Close();
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
    return BRepPrimAPI_MakePrism(face, gp_Vec(0.0, 0.0, thickness)).Shape();
}

TopoDS_Shape makeCircle(double x, double y, double z, double radius, double height)
{
    return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, z), gp_Dir(0.0, 0.0, 1.0)), radius, height).Shape();
}

}  // namespace

PcbViewportGeometry PcbShapeBuilder::build(const PcbDocument& pcb)
{
    if (!std::isfinite(pcb.widthM) || !std::isfinite(pcb.heightM) || !std::isfinite(pcb.thicknessM) ||
        pcb.widthM <= 0.0 || pcb.heightM <= 0.0 || pcb.thicknessM <= 0.0 ||
        !std::isfinite(pcb.centerM[0]) || !std::isfinite(pcb.centerM[1])) {
        throw std::invalid_argument("PCB display geometry requires finite positive board dimensions and center");
    }

    PcbViewportGeometry result;
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    const double centerX = pcb.centerM[0] * MmPerM;
    const double centerY = pcb.centerM[1] * MmPerM;
    const double width = pcb.widthM * MmPerM;
    const double height = pcb.heightM * MmPerM;
    const double boardThickness = pcb.thicknessM * MmPerM;
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(centerX - width / 2.0,
                                                    centerY - height / 2.0, 0.0),
                                               width, height, boardThickness).Shape());

    std::unordered_map<std::string, LayerPlacement> layerPlacements;
    bool usedDisplayThickness = false;
    bool hasIllustrativeInnerLayerPlacement = false;
    for (const PcbLayer& layer : pcb.stackup.layers) {
        LayerPlacement placement;
        placement.bottom = layer.name.starts_with("B.");
        placement.inner = layer.name.starts_with("In");
        hasIllustrativeInnerLayerPlacement = hasIllustrativeInnerLayerPlacement || placement.inner;
        if (layer.thicknessM.has_value()) {
            placement.copperThicknessMm = *layer.thicknessM * MmPerM;
        } else {
            usedDisplayThickness = true;
        }
        layerPlacements.emplace(layer.id, placement);
    }
    if (usedDisplayThickness) {
        result.notices.push_back("0.035 mm copper thickness is used only to draw the 3D preview; layer thickness stays unspecified in the PCB model.");
    }
    if (hasIllustrativeInnerLayerPlacement) {
        result.notices.push_back("Inner copper layers are placed at the board mid-plane for display; their physical stack-up positions are not specified.");
    }

    const auto layerZ = [boardThickness](const LayerPlacement& layer) {
        if (layer.bottom) return -layer.copperThicknessMm;
        if (layer.inner) return boardThickness / 2.0 - layer.copperThicknessMm / 2.0;
        return boardThickness;
    };
    for (const PcbPad& pad : pcb.pads) {
        const double x = pad.centerM[0] * MmPerM;
        const double y = pad.centerM[1] * MmPerM;
        const double padWidth = pad.widthM * MmPerM;
        const double padHeight = pad.heightM * MmPerM;
        for (const std::string& layerId : pad.layerIds) {
            const auto placement = layerPlacements.find(layerId);
            if (placement == layerPlacements.end()) continue;
            const double z = layerZ(placement->second);
            const double depth = placement->second.copperThicknessMm;
            TopoDS_Shape shape = pad.shape == PcbPadShape::Circle
                ? makeCircle(x, y, z, padWidth / 2.0, depth)
                : prismRectangle(x, y, z, padWidth, padHeight, pad.rotationRad, depth);
            builder.Add(compound, shape);
        }
    }
    for (const PcbTrace& trace : pcb.traces) {
        const auto placement = layerPlacements.find(trace.layerId);
        if (placement == layerPlacements.end()) continue;
        const double halfWidth = trace.widthM * MmPerM / 2.0;
        const double z = layerZ(placement->second);
        const double depth = placement->second.copperThicknessMm;
        for (std::size_t index = 1; index < trace.routeM.size(); ++index) {
            const Point2M& a = trace.routeM[index - 1];
            const Point2M& b = trace.routeM[index];
            const double ax = a[0] * MmPerM;
            const double ay = a[1] * MmPerM;
            const double bx = b[0] * MmPerM;
            const double by = b[1] * MmPerM;
            const double dx = bx - ax;
            const double dy = by - ay;
            const double length = std::hypot(dx, dy);
            if (length <= 1e-12) continue;
            const double nx = -dy * halfWidth / length;
            const double ny = dx * halfWidth / length;
            BRepBuilderAPI_MakePolygon polygon;
            polygon.Add(gp_Pnt(ax + nx, ay + ny, z));
            polygon.Add(gp_Pnt(bx + nx, by + ny, z));
            polygon.Add(gp_Pnt(bx - nx, by - ny, z));
            polygon.Add(gp_Pnt(ax - nx, ay - ny, z));
            polygon.Close();
            const TopoDS_Face face = BRepBuilderAPI_MakeFace(polygon.Wire()).Face();
            builder.Add(compound, BRepPrimAPI_MakePrism(face, gp_Vec(0.0, 0.0, depth)).Shape());
        }
        for (const Point2M& point : trace.routeM) {
            builder.Add(compound, makeCircle(point[0] * MmPerM, point[1] * MmPerM,
                                             z, halfWidth, depth));
        }
    }
    std::size_t omittedComponentEnvelopes = 0;
    bool addedComponentEnvelope = false;
    for (const PcbComponent& component : pcb.components) {
        if (!component.packageHeightM.has_value()) {
            ++omittedComponentEnvelopes;
            continue;
        }
        const bool bottomSide = component.layerName.starts_with("B.");
        const double packageHeight = *component.packageHeightM * MmPerM;
        const double baseZ = bottomSide ? -packageHeight : boardThickness;
        builder.Add(compound, prismRectangle(component.centerM[0] * MmPerM,
                                             component.centerM[1] * MmPerM,
                                             baseZ,
                                             component.widthM * MmPerM,
                                             component.heightM * MmPerM,
                                             component.rotationRad,
                                             packageHeight));
        addedComponentEnvelope = true;
    }
    if (addedComponentEnvelope) {
        result.notices.push_back("Component bodies are display-only rectangular envelopes from footprint bounds and supplied package heights; no package CAD models are represented.");
    }
    if (omittedComponentEnvelopes != 0) {
        result.notices.push_back(std::to_string(omittedComponentEnvelopes) +
            " component package envelope(s) omitted because package height is unknown.");
    }
    for (const PcbVia& via : pcb.vias) {
        const double x = via.centerM[0] * MmPerM;
        const double y = via.centerM[1] * MmPerM;
        const double outerRadius = via.outerDiameterM * MmPerM / 2.0;
        const double innerRadius = via.holeDiameterM * MmPerM / 2.0;
        const double depth = boardThickness + 2.0 * DisplayCopperThicknessMm;
        const TopoDS_Shape outer = makeCircle(x, y, -DisplayCopperThicknessMm, outerRadius, depth);
        const TopoDS_Shape inner = makeCircle(x, y, -DisplayCopperThicknessMm, innerRadius, depth);
        const TopoDS_Shape ring = BRepAlgoAPI_Cut(outer, inner).Shape();
        builder.Add(compound, ring);
    }
    if (!pcb.vias.empty()) {
        result.notices.push_back("Via preview rings span the board; plating thickness and layer-to-layer via spans are not represented.");
    }

    result.shape = compound;
    return result;
}

}  // namespace datumloop
