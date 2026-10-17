#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/core/DatumUuid.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/geometry/PcbShapeBuilder.hpp"
#include "datumloop/io/ElectronicsAdapter.hpp"
#include "datumloop/ui/DocumentViewModel.hpp"
#include "datumloop/ui/ExactShapeGeometry.hpp"
#include "datumloop/ui/PickingService.hpp"
#include "datumloop/ui/viewport/QtQuick3DRenderer.hpp"
#include "datumloop/ui/workspace/DatumEditorType.hpp"

#include <QGuiApplication>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointF>
#include <QtQml/qqml.h>
#include <QTimer>
#include <QDebug>
#include <cmath>
#include <exception>

int main(int argc, char* argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DatumLoop DS"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0-native"));

    const bool smokeMode = app.arguments().contains(QStringLiteral("--smoke"));
    const bool loadSmokeMode = app.arguments().contains(QStringLiteral("--smoke-load"));
    const bool interactionSmokeMode = app.arguments().contains(QStringLiteral("--interaction-smoke"));
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
    if (geometry == nullptr) {
        qCritical() << "DatumLoop viewport geometry item was not created.";
        return 2;
    }
    datumloop::QtQuick3DRenderer renderer(geometry);
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
    return exitCode;
}
