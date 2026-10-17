#include "datumloop/core/DatumDocument.hpp"
#include "datumloop/geometry/OcctKernel.hpp"
#include "datumloop/ui/DocumentViewModel.hpp"
#include "datumloop/ui/ExactShapeGeometry.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QDebug>

int main(int argc, char* argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DatumLoop DS"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0-native"));

    const bool smokeMode = app.arguments().contains(QStringLiteral("--smoke"));
    const bool loadSmokeMode = app.arguments().contains(QStringLiteral("--smoke-load"));
    datumloop::DatumDocument document;
    const datumloop::DatumObject& demoPart = document.createBox(
        "DatumLoop Reference Block", {48.0, 34.0, 24.0});
    const datumloop::TriangleMesh displayMesh = datumloop::OcctKernel::tessellate(demoPart.shape);
    datumloop::DocumentViewModel documentModel(&document);

    qmlRegisterType<datumloop::ExactShapeGeometry>(
        "DatumLoop.Native", 1, 0, "ExactShapeGeometry");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("documentModel"), &documentModel);
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
    geometry->setMesh(displayMesh);

    auto* window = qobject_cast<QQuickWindow*>(root);
    if (window == nullptr) {
        qCritical() << "DatumLoop QML root is not a Quick window.";
        return 3;
    }

    int presentedFrames = 0;
    QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&presentedFrames]() {
        ++presentedFrames;
    });
    bool windowVisibleDuringSmoke = false;
    bool windowExposedDuringSmoke = false;
    qInfo() << "DatumLoop native core ready: OCCT shape valid ="
            << demoPart.metrics.valid << ", display triangles =" << displayMesh.triangleCount()
            << ". FreeCAD is not loaded.";

    if (smokeMode) {
        QTimer::singleShot(1600, &app, [&]() {
            windowVisibleDuringSmoke = window->isVisible();
            windowExposedDuringSmoke = window->isExposed();
            app.quit();
        });
    } else if (loadSmokeMode) {
        QTimer::singleShot(350, &app, &QCoreApplication::quit);
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
        if (engine.rootObjects().isEmpty() || geometry == nullptr ||
            !demoPart.metrics.valid || displayMesh.triangleCount() == 0) {
            qCritical() << "DatumLoop native QML or OCCT-derived viewport data did not initialize.";
            return 5;
        }
        qInfo() << "DatumLoop QML root and OCCT-derived viewport data initialized without FreeCAD; frame presentation was not checked.";
    }
    return exitCode;
}
