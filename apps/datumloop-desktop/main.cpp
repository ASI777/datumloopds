#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/engineering/StructuralAnalysis.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/geometry/PcbShapeBuilder.hpp"
#include "datumloop/io/ElectronicsAdapter.hpp"
#include "datumloop/materials/MaterialModel.hpp"
#include "datumloop/numerics/AnalysisMesh.hpp"
#include "datumloop/ui/DocumentViewModel.hpp"
#include "datumloop/ui/ExactShapeGeometry.hpp"
#include "datumloop/ui/PickingService.hpp"
#include "datumloop/ui/viewport/QtQuick3DRenderer.hpp"
#include "datumloop/ui/workspace/DatumEditorType.hpp"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointF>
#include <QSGRendererInterface>
#include <QScreen>
#include <QtQml/qqml.h>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <utility>

namespace {

void addGraphicsSmokeStudy(datumloop::DatumDocument& document, const std::string& cadObjectId)
{
    using namespace datumloop;
    AnalysisMeshSettings meshSettings;
    meshSettings.maximumElementSizeM = 0.024;
    const AnalysisMesh mesh = OcctPrismAnalysisMesher::generate(document, cadObjectId, meshSettings);

    MaterialDefinition material;
    material.id = createDatumUuidV4();
    material.name = "Viewport graphics smoke fixture";
    DatumElectronicsModel electronics = document.electronicsModel();
    electronics.materials.push_back(material);
    document.setElectronicsModel(std::move(electronics));

    FixedDisplacement fixed;
    fixed.id = createDatumUuidV4();
    fixed.name = "Synthetic viewport fixture support";
    fixed.cadObjectId = cadObjectId;
    fixed.topologyReferenceId = mesh.boundaryRegions.front().topologyReferenceId;

    StructuralAnalysisStudy study;
    study.id = createDatumUuidV4();
    study.name = "Synthetic graphics-only field fixture";
    study.cadObjectId = cadObjectId;
    study.materialDefinitionId = material.id;
    study.equationDefinitionId = "graphics-smoke.synthetic-field-fixture.v1";
    study.boundaryConditions.push_back(fixed);

    StructuralAnalysisRun run;
    run.converged = true;
    run.iterations = 1;
    run.maximumDisplacementM = 0.0005;
    run.maximumVonMisesStressPa = 1e6 * static_cast<double>(mesh.elements.size() + 1);
    run.provenance.solverRunId = createDatumUuidV4();
    run.provenance.timestampUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    run.provenance.sourceProjectId = document.projectId();
    run.provenance.sourceDocumentRevision = document.revision();
    run.provenance.cadObjectId = cadObjectId;
    run.provenance.materialDefinitionId = material.id;
    run.provenance.materialRevision = "synthetic-graphics-fixture-not-solver-output";
    run.provenance.equationDefinitionId = study.equationDefinitionId;
    run.provenance.solverId = "datumloop.graphics-smoke.synthetic-fields";
    run.provenance.solverVersion = "1.0";
    run.provenance.mesh = mesh.provenance;
    run.provenance.topologyReferenceIds = mesh.topologyReferenceIds;
    run.provenance.boundaryConditionIds = {fixed.id};
    run.provenance.absoluteTolerance = 1e-10;
    run.provenance.relativeTolerance = 1e-8;
    run.provenance.maximumIterations = 1;
    run.meshSnapshot.emplace();
    run.meshSnapshot->meshRevisionId = mesh.provenance.revisionId;
    for (const AnalysisNode& node : mesh.nodes) {
        run.meshSnapshot->nodePositionsM.push_back(node.positionM);
    }
    for (const AnalysisHexElement& element : mesh.elements) {
        run.meshSnapshot->hexahedra.push_back(element.nodeIndices);
    }

    FieldDataset displacement;
    displacement.id = createDatumUuidV4();
    displacement.fieldType = FieldType::Displacement;
    displacement.location = FieldLocation::Nodes;
    displacement.components = {"x", "y", "z"};
    displacement.units = "m";
    displacement.meshRevisionId = mesh.provenance.revisionId;
    displacement.solverRunId = run.provenance.solverRunId;
    for (const AnalysisNode& node : mesh.nodes) {
        displacement.values.insert(displacement.values.end(), {node.positionM[0] * 0.01, 0.0, 0.0});
    }
    run.fields.push_back(std::move(displacement));

    FieldDataset strain;
    strain.id = createDatumUuidV4();
    strain.fieldType = FieldType::StrainTensor;
    strain.location = FieldLocation::Elements;
    strain.components = {"xx", "yy", "zz", "xy", "yz", "zx"};
    strain.units = "1";
    strain.meshRevisionId = mesh.provenance.revisionId;
    strain.solverRunId = run.provenance.solverRunId;
    FieldDataset stress;
    stress.id = createDatumUuidV4();
    stress.fieldType = FieldType::StressTensor;
    stress.location = FieldLocation::Elements;
    stress.components = {"xx", "yy", "zz", "xy", "yz", "zx"};
    stress.units = "Pa";
    stress.meshRevisionId = mesh.provenance.revisionId;
    stress.solverRunId = run.provenance.solverRunId;
    FieldDataset vonMises;
    vonMises.id = createDatumUuidV4();
    vonMises.fieldType = FieldType::VonMisesStress;
    vonMises.location = FieldLocation::Elements;
    vonMises.components = {"von_mises"};
    vonMises.units = "Pa";
    vonMises.meshRevisionId = mesh.provenance.revisionId;
    vonMises.solverRunId = run.provenance.solverRunId;
    for (std::size_t element = 0; element < mesh.elements.size(); ++element) {
        const double stressPa = 1e6 * static_cast<double>(element + 1);
        strain.values.insert(strain.values.end(), {stressPa * 1e-10, -stressPa * 0.5e-10,
                                                    -stressPa * 0.5e-10, 0.0, 0.0, 0.0});
        stress.values.insert(stress.values.end(), {stressPa, 0.0, 0.0, 0.0, 0.0, 0.0});
        vonMises.values.push_back(stressPa);
    }
    run.fields.push_back(std::move(strain));
    run.fields.push_back(std::move(stress));
    run.fields.push_back(std::move(vonMises));
    study.runs.push_back(std::move(run));

    DatumEngineeringModel engineering = document.engineeringModel();
    engineering.structuralStudies.push_back(std::move(study));
    document.setEngineeringModel(std::move(engineering));
}

QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Unknown: return QStringLiteral("Unknown");
    case QSGRendererInterface::Software: return QStringLiteral("Software");
    case QSGRendererInterface::OpenVG: return QStringLiteral("OpenVG");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D11");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Null: return QStringLiteral("Null");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D12");
    }
    return QStringLiteral("Unknown");
}

QRect viewportRectInImage(QQuickItem* viewport, const QImage& image)
{
    if (viewport == nullptr || image.isNull()) {
        return {};
    }
    const QPointF topLeft = viewport->mapToScene(QPointF(0.0, 0.0));
    const qreal scale = image.devicePixelRatio();
    const QRect rect(qRound(topLeft.x() * scale), qRound(topLeft.y() * scale),
                     qRound(viewport->width() * scale), qRound(viewport->height() * scale));
    return rect.intersected(image.rect());
}

int visiblePixelCount(const QImage& image, const QRect& area)
{
    if (image.isNull() || area.isEmpty()) {
        return 0;
    }
    const QColor background = image.pixelColor(area.right() - 8, area.bottom() - 8);
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const int difference = std::abs(pixel.red() - background.red()) +
                                   std::abs(pixel.green() - background.green()) +
                                   std::abs(pixel.blue() - background.blue());
            if (difference > 48) {
                ++count;
            }
        }
    }
    return count;
}

int changedPixelCount(const QImage& before, const QImage& after, const QRect& area)
{
    if (before.isNull() || after.isNull() || before.size() != after.size() || area.isEmpty()) {
        return 0;
    }
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QColor left = before.pixelColor(x, y);
            const QColor right = after.pixelColor(x, y);
            const int difference = std::abs(left.red() - right.red()) +
                                   std::abs(left.green() - right.green()) +
                                   std::abs(left.blue() - right.blue());
            if (difference > 40) {
                ++count;
            }
        }
    }
    return count;
}

}  // namespace

int main(int argc, char* argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DatumLoop DS"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0-native"));

    const bool smokeMode = app.arguments().contains(QStringLiteral("--smoke"));
    const bool loadSmokeMode = app.arguments().contains(QStringLiteral("--smoke-load"));
    const bool gpuSmokeMode = app.arguments().contains(QStringLiteral("--gpu-smoke"));
    const bool interactionSmokeMode = app.arguments().contains(QStringLiteral("--interaction-smoke")) || gpuSmokeMode;
    datumloop::DatumDocument document;
    const datumloop::DatumObject* initialObject = nullptr;
    datumloop::TriangleMesh displayMesh;
    const QStringList arguments = app.arguments();
    const int pcbArgumentIndex = arguments.indexOf(QStringLiteral("--pcb"));
    try {
        if (pcbArgumentIndex >= 0) {
            if (pcbArgumentIndex + 1 >= arguments.size() ||
                arguments[pcbArgumentIndex + 1].startsWith(QStringLiteral("--"))) {
                qCritical() << "--pcb requires a .kicad_pcb or Circuit JSON file path.";
                return 1;
            }
            const QString pcbPath = arguments[pcbArgumentIndex + 1];
            QFile file(pcbPath);
            if (!file.open(QIODevice::ReadOnly)) {
                qCritical() << "Could not open PCB input:" << pcbPath << file.errorString();
                return 1;
            }
            const QByteArray bytes = file.readAll();
            const datumloop::ElectronicsImportResult imported =
                QFileInfo(pcbPath).suffix().compare(QStringLiteral("kicad_pcb"), Qt::CaseInsensitive) == 0
                    ? datumloop::KiCadPcbAdapter::importBytes(
                          bytes, QFileInfo(pcbPath).completeBaseName().toStdString())
                    : datumloop::CircuitJsonAdapter::importBytes(bytes);
            if (imported.model.pcbs.empty()) {
                qCritical() << "PCB input did not contain a supported board.";
                return 1;
            }
            document.setElectronicsModel(imported.model);
            const datumloop::PcbDocument& pcb = document.electronicsModel().pcbs.front();
            const datumloop::PcbViewportGeometry preview = datumloop::PcbShapeBuilder::build(pcb);
            initialObject = &document.addObject(
                datumloop::createDatumUuidV4(), pcb.name, preview.shape);
            for (const std::string& warning : imported.warnings) {
                qWarning().noquote() << QString::fromStdString(warning);
            }
            for (const std::string& notice : preview.notices) {
                qInfo().noquote() << QString::fromStdString(notice);
            }
            qInfo() << "Imported PCB into the native electronics model:" << pcb.name
                    << "components =" << pcb.components.size()
                    << ", pads =" << pcb.pads.size()
                    << ", traces =" << pcb.traces.size()
                    << ", vias =" << pcb.vias.size();
        } else {
            initialObject = &document.createBox("DatumLoop Reference Block", {48.0, 34.0, 24.0});
        }
        displayMesh = datumloop::OcctKernel::tessellate(initialObject->shape);
    } catch (const std::exception& error) {
        qCritical() << "Could not initialize native geometry:" << error.what();
        return 1;
    }
    if (gpuSmokeMode) {
        try {
            addGraphicsSmokeStudy(document, initialObject->id);
        } catch (const std::exception& error) {
            qCritical() << "Could not prepare the synthetic viewport field fixture:" << error.what();
            return 8;
        }
    }
    datumloop::DocumentViewModel documentModel(&document);
    if (interactionSmokeMode) {
        documentModel.selectRow(-1);
    }
    datumloop::PickingService pickingService(&documentModel, &document);

    qmlRegisterType<datumloop::ExactShapeGeometry>(
        "DatumLoop.Native", 1, 0, "ExactShapeGeometry");
    qmlRegisterUncreatableMetaObject(
        datumloop::DatumEditorType::staticMetaObject,
        "DatumLoop.App", 1, 0, "DatumEditorType",
        QStringLiteral("DatumEditorType is an enumeration"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("documentModel"), &documentModel);
    engine.rootContext()->setContextProperty(QStringLiteral("pickingService"), &pickingService);
    engine.loadFromModule("DatumLoop.App", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "DatumLoop QML root did not load.";
        return 1;
    }

    QObject* root = engine.rootObjects().front();
    auto* geometry = root->findChild<datumloop::ExactShapeGeometry*>(QStringLiteral("cadGeometry"));
    auto* analysisGeometry = root->findChild<datumloop::ExactShapeGeometry*>(
        QStringLiteral("analysisResultGeometry"));
    if (geometry == nullptr || analysisGeometry == nullptr) {
        qCritical() << "DatumLoop viewport CAD or analysis geometry item was not created.";
        return 2;
    }
    datumloop::QtQuick3DRenderer renderer(geometry, analysisGeometry);
    renderer.setDisplayMesh(displayMesh);
    const auto updateSelectedDisplayMesh = [&]() {
        const datumloop::DatumObject* selected =
            document.object(documentModel.displayObjectId().toStdString());
        renderer.setDisplayMesh(selected == nullptr
                                    ? datumloop::TriangleMesh{}
                                    : datumloop::OcctKernel::tessellate(selected->shape));
    };
    QObject::connect(&documentModel, &datumloop::DocumentViewModel::documentContentChanged,
                     &app, updateSelectedDisplayMesh);
    QObject::connect(&documentModel, &datumloop::DocumentViewModel::selectionChanged,
                     &app, updateSelectedDisplayMesh);
    const auto updateAnalysisDisplayMesh = [&]() {
        renderer.setAnalysisDisplayMesh(documentModel.analysisDisplayMesh());
    };
    QObject::connect(&documentModel, &datumloop::DocumentViewModel::analysisDisplayChanged,
                     &app, updateAnalysisDisplayMesh);
    updateAnalysisDisplayMesh();

    auto* window = qobject_cast<QQuickWindow*>(root);
    if (window == nullptr) {
        qCritical() << "DatumLoop QML root is not a Quick window.";
        return 3;
    }

    bool interactionTargetsReady = false;
    if (interactionSmokeMode) {
        const QStringList targetNames{
            QStringLiteral("cadViewport"),
            QStringLiteral("selectionModeFaceButton"),
            QStringLiteral("moveAxisZButton"),
            QStringLiteral("moveDistanceField"),
            QStringLiteral("cancelMoveButton"),
            QStringLiteral("confirmMoveButton"),
            QStringLiteral("undoMoveButton"),
            QStringLiteral("redoMoveButton"),
        };
        QTimer::singleShot(1200, &app, [root, targetNames, &interactionTargetsReady]() {
            bool allTargetsFound = true;
            for (const QString& name : targetNames) {
                auto* item = root->findChild<QQuickItem*>(name);
                if (item == nullptr) {
                    qCritical() << "Missing pointer-smoke target:" << name;
                    allTargetsFound = false;
                    continue;
                }
                const QPointF localPoint(item->width() / 2.0, item->height() / 2.0);
                const QPointF globalPoint = item->mapToGlobal(localPoint);
                qInfo().noquote() << "INTERACTION_TARGET" << name
                                  << qRound(globalPoint.x()) << qRound(globalPoint.y());
            }
            interactionTargetsReady = allTargetsFound;
        });
    }

    int presentedFrames = 0;
    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&presentedFrames]() {
        ++presentedFrames;
    });
    if (gpuSmokeMode) {
        QObject::connect(window, &QQuickWindow::beforeRendering, window, [window, &app]() {
            static std::atomic_bool reported{false};
            if (reported.exchange(true)) {
                return;
            }
            const QString api = graphicsApiName(window->rendererInterface()->graphicsApi());
            QByteArray vendor;
            QByteArray renderer;
            QByteArray version;
            if (QOpenGLContext* context = QOpenGLContext::currentContext()) {
                QOpenGLFunctions* functions = context->functions();
                functions->initializeOpenGLFunctions();
                const auto getString = [functions](GLenum name) {
                    const GLubyte* value = functions->glGetString(name);
                    return value == nullptr ? QByteArray{} : QByteArray(reinterpret_cast<const char*>(value));
                };
                vendor = getString(GL_VENDOR);
                renderer = getString(GL_RENDERER);
                version = getString(GL_VERSION);
            }
            QMetaObject::invokeMethod(&app, [&app, api, vendor, renderer, version]() {
                qInfo().noquote() << "DATUMLOOP_GRAPHICS_API=" + api;
                qInfo().noquote() << "DATUMLOOP_GRAPHICS_VENDOR=" + QString::fromLatin1(vendor);
                qInfo().noquote() << "DATUMLOOP_GRAPHICS_RENDERER=" + QString::fromLatin1(renderer);
                qInfo().noquote() << "DATUMLOOP_GRAPHICS_VERSION=" + QString::fromLatin1(version);
                qInfo().noquote() << "DATUMLOOP_QT_VERSION=" + QString::fromLatin1(qVersion());
            }, Qt::QueuedConnection);
        }, Qt::DirectConnection);
    }
    bool windowVisibleDuringSmoke = false;
    bool windowExposedDuringSmoke = false;
    qInfo() << "DatumLoop native core ready: OCCT shape valid ="
            << initialObject->metrics.valid << ", display triangles =" << displayMesh.triangleCount()
            << ". FreeCAD is not loaded.";

    if (smokeMode) {
        QTimer::singleShot(1600, &app, [&]() {
            windowVisibleDuringSmoke = window->isVisible();
            windowExposedDuringSmoke = window->isExposed();
            app.quit();
        });
    } else if (loadSmokeMode) {
        QTimer::singleShot(350, &app, &QCoreApplication::quit);
    } else if (gpuSmokeMode) {
        QTimer::singleShot(6500, &app, [&]() {
            QImage geometryFrame = window->grabWindow();
            QQuickItem* viewport = root->findChild<QQuickItem*>(QStringLiteral("cadViewport"));
            const QRect viewportArea = viewportRectInImage(viewport, geometryFrame);
            const QRect centralViewportArea = viewportArea.adjusted(
                viewportArea.width() / 4, viewportArea.height() / 4,
                -viewportArea.width() / 4, -viewportArea.height() / 4);
            const int geometryPixelCount = visiblePixelCount(geometryFrame, centralViewportArea);
            const QString captureDirectory = qEnvironmentVariable("DATUMLOOPDS_GPU_CAPTURE_DIR");
            bool geometryFrameSaved = captureDirectory.isEmpty();
            if (!captureDirectory.isEmpty()) {
                QDir().mkpath(captureDirectory);
                geometryFrameSaved = geometryFrame.save(
                    QDir(captureDirectory).filePath(QStringLiteral("cad-geometry.png")));
            }
            documentModel.setAnalysisDisplayMode(QStringLiteral("von Mises Stress"));
            QTimer::singleShot(700, &app, [&, geometryFrame, centralViewportArea, geometryPixelCount, captureDirectory]() {
                QImage fieldFrame = window->grabWindow();
                QObject* cadModel = root->findChild<QObject*>(QStringLiteral("datumLoopExactSolid"));
                QObject* fieldModel = root->findChild<QObject*>(QStringLiteral("datumLoopAnalysisResult"));
                const bool cadHiddenForField = cadModel != nullptr && !cadModel->property("visible").toBool();
                const bool fieldVisible = fieldModel != nullptr && fieldModel->property("visible").toBool();
                const std::size_t fieldTriangles = documentModel.analysisDisplayMesh().triangleCount();
                const int fieldChangedPixelCount = changedPixelCount(geometryFrame, fieldFrame, centralViewportArea);
                bool fieldFrameSaved = captureDirectory.isEmpty();
                if (!captureDirectory.isEmpty()) {
                    fieldFrameSaved = fieldFrame.save(
                        QDir(captureDirectory).filePath(QStringLiteral("von-mises-overlay.png")));
                }
                qInfo().noquote() << "DATUMLOOP_WINDOW_SIZE=" +
                    QStringLiteral("%1x%2").arg(window->width()).arg(window->height());
                const QSize screenSize = window->screen() == nullptr
                    ? QSize{} : window->screen()->geometry().size();
                qInfo().noquote() << "DATUMLOOP_SCREEN_SIZE=" +
                    QStringLiteral("%1x%2").arg(screenSize.width()).arg(screenSize.height());
                qInfo().noquote() << "DATUMLOOP_CAD_RENDER_VISIBLE=" +
                    QString::number(cadModel != nullptr && cadModel->property("visible").toBool());
                qInfo().noquote() << "DATUMLOOP_CAD_RENDER_PIXELS=" + QString::number(geometryPixelCount);
                qInfo().noquote() << "DATUMLOOP_FIELD_OVERLAY_VISIBLE=" +
                    QString::number(fieldVisible && cadHiddenForField);
                qInfo().noquote() << "DATUMLOOP_FIELD_OVERLAY_TRIANGLES=" + QString::number(fieldTriangles);
                qInfo().noquote() << "DATUMLOOP_FIELD_OVERLAY_MODE=" + documentModel.analysisDisplayMode();
                qInfo().noquote() << "DATUMLOOP_FIELD_OVERLAY_CHANGED_PIXELS=" +
                    QString::number(fieldChangedPixelCount);
                qInfo().noquote() << "DATUMLOOP_CAD_FRAME_SAVED=" + QString::number(geometryFrameSaved);
                qInfo().noquote() << "DATUMLOOP_FIELD_FRAME_SAVED=" + QString::number(fieldFrameSaved);
                qInfo().noquote() << "DATUMLOOP_PRESENTED_FRAMES=" + QString::number(presentedFrames);
                qInfo().noquote() << "DATUMLOOP_GPU_CAPTURE_DIR=" + captureDirectory;
                const bool graphicsSmokePassed = !geometryFrame.isNull() && !fieldFrame.isNull() &&
                    geometryPixelCount > 100 && fieldVisible && cadHiddenForField && fieldTriangles > 0 &&
                    documentModel.analysisDisplayMode() == QStringLiteral("von Mises Stress") &&
                    fieldChangedPixelCount > 100 && geometryFrameSaved && fieldFrameSaved;
                if (!graphicsSmokePassed) {
                    qCritical() << "Hardware viewport smoke did not render both the CAD body and structural field overlay.";
                } else {
                    qInfo() << "CAD geometry and synthetic von Mises field overlay rendered in the viewport.";
                }
                app.setProperty("datumLoopGpuSmokePassed", graphicsSmokePassed);
                app.quit();
            });
        });
    } else if (interactionSmokeMode) {
        QTimer::singleShot(8000, &app, &QCoreApplication::quit);
    }
    const int exitCode = app.exec();
    if (smokeMode) {
        if (!windowVisibleDuringSmoke || presentedFrames == 0) {
            qCritical() << "DatumLoop native window did not present a frame. visible ="
                        << windowVisibleDuringSmoke << ", exposed =" << windowExposedDuringSmoke
                        << ", frames =" << presentedFrames;
            return 4;
        }
        qInfo() << "DatumLoop native QML window presented" << presentedFrames << "frames without FreeCAD.";
    }
    if (loadSmokeMode) {
        const auto hasItem = [root](const QString& name) {
            return root->findChild<QObject*>(name) != nullptr;
        };
        if (engine.rootObjects().isEmpty() || geometry == nullptr ||
            !hasItem(QStringLiteral("datumWorkspace")) ||
            !hasItem(QStringLiteral("datumArea")) ||
            !hasItem(QStringLiteral("outlinerEditor")) ||
            !hasItem(QStringLiteral("viewportEditor")) ||
            !hasItem(QStringLiteral("selectionModeToolbar")) ||
            !hasItem(QStringLiteral("analysisModeSelector")) ||
            !hasItem(QStringLiteral("datumLoopAnalysisResult")) ||
            !hasItem(QStringLiteral("analysisResultLegend")) ||
            !hasItem(QStringLiteral("analysisResultStatusLabel")) ||
            !hasItem(QStringLiteral("propertiesEditor")) ||
            initialObject == nullptr || !initialObject->metrics.valid || displayMesh.triangleCount() == 0) {
            qCritical() << "DatumLoop workspace, editor components, or OCCT-derived viewport data did not initialize.";
            return 5;
        }
        qInfo() << "DatumLoop QML root and OCCT-derived viewport data initialized without FreeCAD; frame presentation was not checked.";
    }
    if (interactionSmokeMode) {
        if (!interactionTargetsReady) {
            qCritical() << "Pointer-smoke targets were not available after QML layout.";
            return 6;
        }
        const datumloop::DatumObject* movedObject = document.object(initialObject->id);
        QObject* camera = root->findChild<QObject*>(QStringLiteral("datumLoopCamera"));
        const bool cameraInputPassed = camera != nullptr &&
            (std::abs(camera->property("orbitYaw").toReal() - 35.0) > 0.1 ||
             std::abs(camera->property("orbitDistance").toReal() - 112.0) > 0.1);
        const bool pointerWorkflowPassed = documentModel.selectedRow() == 0 &&
            !documentModel.moveActive() && documentModel.canUndo() && !documentModel.canRedo() &&
            documentModel.selectedType() == QStringLiteral("OCCT B-Rep Face") &&
            !documentModel.selectedTopologyReferenceId().isEmpty() &&
            pickingService.selectionMode() == QStringLiteral("Face") &&
            movedObject != nullptr && std::abs(movedObject->position[2] - 4.5) < 1e-8 &&
            cameraInputPassed;
        if (!pointerWorkflowPassed) {
            qCritical() << "Pointer-driven object/subshape selection, move cancel/confirm, or undo/redo failed."
                        << "selected row =" << documentModel.selectedRow()
                        << "move active =" << documentModel.moveActive()
                        << "status =" << documentModel.statusText()
                        << "position =" << (movedObject == nullptr ? -999.0 : movedObject->position[2])
                        << "can undo =" << documentModel.canUndo()
                        << "can redo =" << documentModel.canRedo()
                        << "camera input passed =" << cameraInputPassed;
            return 7;
        }
        qInfo() << "Pointer-driven object/face pick, camera navigation, and Move cancel/confirm/undo/redo passed.";
    }
    if (gpuSmokeMode && !app.property("datumLoopGpuSmokePassed").toBool()) {
        return 9;
    }
    return exitCode;
}
