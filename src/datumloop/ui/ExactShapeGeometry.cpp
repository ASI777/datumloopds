#include "datumloop/ui/ExactShapeGeometry.hpp"

#include <QVector3D>

#include <cstddef>

namespace datumloop {
namespace {

struct Vertex {
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
};

static_assert(sizeof(Vertex) == sizeof(float) * 6);

}  // namespace

ExactShapeGeometry::ExactShapeGeometry(QQuick3DObject* parent)
    : QQuick3DGeometry(parent)
{
    setPrimitiveType(PrimitiveType::Triangles);
    setStride(sizeof(Vertex));
    addAttribute(Attribute::PositionSemantic, offsetof(Vertex, px), Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, offsetof(Vertex, nx), Attribute::F32Type);
}

void ExactShapeGeometry::setMesh(const TriangleMesh& mesh)
{
    QByteArray vertexData;
    vertexData.reserve(static_cast<qsizetype>(mesh.vertices.size() * sizeof(Vertex)));
    for (const MeshVertex& vertex : mesh.vertices) {
        const Vertex packed{
            vertex.position[0], vertex.position[1], vertex.position[2],
            vertex.normal[0], vertex.normal[1], vertex.normal[2]
        };
        vertexData.append(reinterpret_cast<const char*>(&packed), static_cast<qsizetype>(sizeof(Vertex)));
    }

    clear();
    setPrimitiveType(PrimitiveType::Triangles);
    setStride(sizeof(Vertex));
    addAttribute(Attribute::PositionSemantic, offsetof(Vertex, px), Attribute::F32Type);
    addAttribute(Attribute::NormalSemantic, offsetof(Vertex, nx), Attribute::F32Type);
    setVertexData(vertexData);
    setBounds(QVector3D(static_cast<float>(mesh.minimum[0]),
                        static_cast<float>(mesh.minimum[1]),
                        static_cast<float>(mesh.minimum[2])),
              QVector3D(static_cast<float>(mesh.maximum[0]),
                        static_cast<float>(mesh.maximum[1]),
                        static_cast<float>(mesh.maximum[2])));
}

}  // namespace datumloop
