cmake_minimum_required(VERSION 3.24)
project(DatumLoopDS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Quick3D Concurrent)
find_package(OpenCASCADE REQUIRED COMPONENTS FoundationClasses ModelingData ModelingAlgorithms)

qt_standard_project_setup(REQUIRES 6.6)
qt_policy(SET QTP0004 NEW)

add_library(datumloop_core STATIC
    src/datumloop/core/DatumDocument.cpp
    src/datumloop/core/DatumCommand.cpp
    src/datumloop/geometry/OcctKernel.cpp
)
target_include_directories(datumloop_core PUBLIC src)
target_link_libraries(datumloop_core PUBLIC
    ${OpenCASCADE_FoundationClasses_LIBRARIES}
    ${OpenCASCADE_ModelingData_LIBRARIES}
    ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
)
target_compile_options(datumloop_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(datumloop_io STATIC
    src/datumloop/io/DldProjectStore.cpp
)
target_include_directories(datumloop_io PUBLIC src)
target_link_libraries(datumloop_io PUBLIC datumloop_core Qt6::Core)
target_compile_options(datumloop_io PRIVATE -Wall -Wextra -Wpedantic)

add_library(datumloop_ui STATIC
    src/datumloop/ui/DocumentViewModel.cpp
    src/datumloop/ui/PickingService.cpp
)
target_include_directories(datumloop_ui PUBLIC src)
target_link_libraries(datumloop_ui PUBLIC datumloop_core datumloop_io Qt6::Core Qt6::Gui)
target_compile_options(datumloop_ui PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(DatumLoopDS
    apps/datumloop-desktop/main.cpp
    src/datumloop/ui/ExactShapeGeometry.cpp
)
set_target_properties(DatumLoopDS PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/apps/datumloop-desktop"
)
qt_add_qml_module(DatumLoopDS
    URI DatumLoop.App
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/DesignSystem.qml
        qml/controls/DlButton.qml
        qml/controls/EditorHeading.qml
        qml/workspace/DatumRegion.qml
        qml/workspace/DatumEditor.qml
        qml/workspace/EditorHost.qml
        qml/workspace/DatumArea.qml
        qml/workspace/DatumWorkspace.qml
        qml/workspace/WorkspaceHeader.qml
        qml/editors/ViewportEditor.qml
        qml/editors/OutlinerEditor.qml
        qml/editors/PropertiesEditor.qml
        qml/panels/StatusBar.qml
)
target_sources(DatumLoopDS PRIVATE
    src/datumloop/ui/viewport/QtQuick3DRenderer.cpp
    src/datumloop/ui/workspace/DatumEditorType.hpp
)
target_include_directories(DatumLoopDS PRIVATE src src/datumloop/ui/workspace)
target_link_libraries(DatumLoopDS PRIVATE
    datumloop_ui
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Quick3D
    Qt6::Concurrent
)
target_compile_options(DatumLoopDS PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(datumloop-core-tests tests/native/test_core.cpp)
    target_link_libraries(datumloop-core-tests PRIVATE datumloop_core)
    add_test(NAME datumloop-core-tests COMMAND datumloop-core-tests)

    add_executable(datumloop-io-tests tests/native/test_io.cpp)
    target_link_libraries(datumloop-io-tests PRIVATE datumloop_io Qt6::Core)
    add_test(NAME datumloop-io-tests COMMAND datumloop-io-tests)

    add_executable(datumloop-ui-tests tests/native/test_ui_model.cpp)
    target_link_libraries(datumloop-ui-tests PRIVATE datumloop_ui)
    add_test(NAME datumloop-ui-tests COMMAND datumloop-ui-tests)
endif()
