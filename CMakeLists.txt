cmake_minimum_required(VERSION 3.24)
project(DatumLoopDS VERSION 0.1.0 LANGUAGES CXX)

option(DATUMLOOP_ENABLE_MFEM "Build the optional MFEM finite-element adapter when available" ON)
set(DATUMLOOP_MUJOCO_PYTHON "" CACHE FILEPATH "Python interpreter with the MuJoCo package installed")

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
    src/datumloop/core/DatumFeatureGraph.cpp
    src/datumloop/core/DatumUuid.cpp
    src/datumloop/core/DatumTopologyReference.cpp
    src/datumloop/engineering/DatumEngineeringModel.cpp
    src/datumloop/engineering/StructuralAnalysis.cpp
    src/datumloop/engineering/PartDesign.cpp
    src/datumloop/knowledge/EquationRegistry.cpp
    src/datumloop/materials/MaterialModel.cpp
    src/datumloop/electronics/ElectronicsModel.cpp
    src/datumloop/topology/DatumTopologyResolver.cpp
    src/datumloop/geometry/OcctKernel.cpp
    src/datumloop/geometry/PcbShapeBuilder.cpp
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
    src/datumloop/io/ElectronicsProjectCodec.cpp
    src/datumloop/io/CircuitJsonAdapter.cpp
    src/datumloop/io/KiCadPcbAdapter.cpp
    src/datumloop/io/PartRequirementsCodec.cpp
)
target_include_directories(datumloop_io PUBLIC src)
target_link_libraries(datumloop_io PUBLIC datumloop_core Qt6::Core)
target_compile_options(datumloop_io PRIVATE -Wall -Wextra -Wpedantic)

add_library(datumloop_solver STATIC
    src/datumloop/solver/PhysicsSolver.cpp
    src/datumloop/solver/NgspiceProcessCircuitSolver.cpp
)
target_include_directories(datumloop_solver PUBLIC src)
target_link_libraries(datumloop_solver PUBLIC datumloop_core Qt6::Core)
target_compile_options(datumloop_solver PRIVATE -Wall -Wextra -Wpedantic)

add_executable(datumloop-ngspice-worker apps/ngspice-worker/main.cpp)
target_link_libraries(datumloop-ngspice-worker PRIVATE Qt6::Core ${CMAKE_DL_LIBS})
target_compile_options(datumloop-ngspice-worker PRIVATE -Wall -Wextra -Wpedantic)
find_library(DATUMLOOP_NGSPICE_LIBRARY NAMES ngspice libngspice.so.0)

add_library(datumloop_numerics STATIC
    src/datumloop/numerics/CantileverBeamFEM.cpp
    src/datumloop/numerics/FemSolver.cpp
    src/datumloop/numerics/AnalysisMesh.cpp
    src/datumloop/numerics/StructuralReducedModel.cpp
)
target_include_directories(datumloop_numerics PUBLIC src)
target_link_libraries(datumloop_numerics PUBLIC datumloop_core)
target_compile_options(datumloop_numerics PRIVATE -Wall -Wextra -Wpedantic)

add_library(datumloop_digitaltwin STATIC
    src/datumloop/digitaltwin/ModelEvidence.cpp
)
target_include_directories(datumloop_digitaltwin PUBLIC src)
target_link_libraries(datumloop_digitaltwin PUBLIC datumloop_core)
target_compile_options(datumloop_digitaltwin PRIVATE -Wall -Wextra -Wpedantic)

add_library(datumloop_science STATIC
    src/datumloop/science/ScientificAgents.cpp
)
target_include_directories(datumloop_science PUBLIC src)
target_link_libraries(datumloop_science PUBLIC datumloop_solver)
target_compile_options(datumloop_science PRIVATE -Wall -Wextra -Wpedantic)

if(DATUMLOOP_ENABLE_MFEM)
    find_package(MFEM 4.10 QUIET CONFIG)
endif()
if(DATUMLOOP_ENABLE_MFEM AND MFEM_FOUND)
    add_library(datumloop_mfem_backend STATIC
        src/datumloop/numerics/MfemFemSolver.cpp
        src/datumloop/numerics/MfemLinearElasticitySolver.cpp
    )
    target_include_directories(datumloop_mfem_backend PUBLIC src)
    target_link_libraries(datumloop_mfem_backend PUBLIC datumloop_numerics datumloop_solver mfem)
    target_compile_options(datumloop_mfem_backend PRIVATE -Wall -Wextra -Wpedantic)
    message(STATUS "DatumLoop MFEM adapter enabled (MFEM ${MFEM_VERSION})")
elseif(DATUMLOOP_ENABLE_MFEM)
    message(STATUS "MFEM 4.10 or newer not found; optional DatumLoop MFEM adapter disabled")
endif()

add_library(datumloop_robotics STATIC
    src/datumloop/robotics/RobotLearning.cpp
)
target_include_directories(datumloop_robotics PUBLIC src)
target_link_libraries(datumloop_robotics PUBLIC datumloop_core datumloop_numerics)
target_compile_options(datumloop_robotics PRIVATE -Wall -Wextra -Wpedantic)

add_library(datumloop_ui STATIC
    src/datumloop/ui/AnalysisResultDisplay.cpp
    src/datumloop/ui/DocumentViewModel.cpp
    src/datumloop/ui/PickingService.cpp
)
target_include_directories(datumloop_ui PUBLIC src)
target_link_libraries(datumloop_ui PUBLIC datumloop_core datumloop_io datumloop_numerics Qt6::Core Qt6::Gui)
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

    add_executable(datumloop-part-design-tests tests/native/test_part_design.cpp)
    target_link_libraries(datumloop-part-design-tests PRIVATE datumloop_io Qt6::Core)
    target_compile_definitions(datumloop-part-design-tests PRIVATE
        DATUMLOOP_PART_SOURCE="${CMAKE_SOURCE_DIR}/examples/parts/maxon-ecx-flat-22-l/requirements.json")
    add_test(NAME datumloop-part-design-tests COMMAND datumloop-part-design-tests)

    add_executable(datumloop-electronics-tests tests/native/test_electronics.cpp)
    target_link_libraries(datumloop-electronics-tests PRIVATE datumloop_io datumloop_solver Qt6::Core)
    add_dependencies(datumloop-electronics-tests datumloop-ngspice-worker)
    if(DATUMLOOP_NGSPICE_LIBRARY)
        target_compile_definitions(datumloop-electronics-tests PRIVATE DATUMLOOP_HAS_NGSPICE=1)
    endif()
    target_compile_definitions(datumloop-electronics-tests PRIVATE
        DATUMLOOP_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/electronics")
    add_test(NAME datumloop-electronics-tests COMMAND datumloop-electronics-tests)

    add_executable(datumloop-knowledge-tests tests/native/test_knowledge.cpp)
    target_link_libraries(datumloop-knowledge-tests PRIVATE datumloop_core datumloop_solver datumloop_numerics)
    add_test(NAME datumloop-knowledge-tests COMMAND datumloop-knowledge-tests)

    add_executable(datumloop-model-evidence-tests tests/native/test_model_evidence.cpp)
    target_link_libraries(datumloop-model-evidence-tests PRIVATE datumloop_digitaltwin)
    add_test(NAME datumloop-model-evidence-tests COMMAND datumloop-model-evidence-tests)

    add_executable(datumloop-scientific-agent-tests tests/native/test_scientific_agents.cpp)
    target_link_libraries(datumloop-scientific-agent-tests PRIVATE datumloop_science)
    add_test(NAME datumloop-scientific-agent-tests COMMAND datumloop-scientific-agent-tests)

    add_executable(datumloop-robotics-tests tests/native/test_robotics.cpp)
    target_link_libraries(datumloop-robotics-tests PRIVATE datumloop_robotics Qt6::Core)
    add_test(NAME datumloop-robotics-tests COMMAND datumloop-robotics-tests)
    set(DATUMLOOP_MJCF_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mujoco-runtime/one-axis-fixture.xml")
    set_tests_properties(datumloop-robotics-tests PROPERTIES
        ENVIRONMENT "DATUMLOOP_MJCF_OUTPUT=${DATUMLOOP_MJCF_OUTPUT}")

    if(DATUMLOOP_ENABLE_MFEM AND MFEM_FOUND)
        add_executable(datumloop-mfem-tests tests/native/test_mfem_solver.cpp)
        target_link_libraries(datumloop-mfem-tests PRIVATE datumloop_mfem_backend)
        add_test(NAME datumloop-mfem-tests COMMAND datumloop-mfem-tests)

        add_executable(datumloop-mfem-elasticity-tests tests/native/test_mfem_3d_elasticity.cpp)
        target_link_libraries(datumloop-mfem-elasticity-tests PRIVATE datumloop_mfem_backend datumloop_io datumloop_robotics Qt6::Core)
        set(DATUMLOOP_MUJOCO_RUNTIME_REPORT "${CMAKE_CURRENT_BINARY_DIR}/reports/mujoco-runtime.json")
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/reports")
        configure_file(
            "${CMAKE_SOURCE_DIR}/reports/validation/mujoco-runtime.json"
            "${DATUMLOOP_MUJOCO_RUNTIME_REPORT}"
            COPYONLY)
        target_compile_definitions(datumloop-mfem-elasticity-tests PRIVATE
            DATUMLOOP_MUJOCO_RUNTIME_REPORT="${DATUMLOOP_MUJOCO_RUNTIME_REPORT}")
        add_test(NAME datumloop-mfem-elasticity-tests COMMAND datumloop-mfem-elasticity-tests)
    else()
        foreach(test_name IN ITEMS datumloop-mfem-tests datumloop-mfem-elasticity-tests)
            add_test(NAME ${test_name}
                COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: MFEM 4.10 is unavailable or disabled")
            set_tests_properties(${test_name} PROPERTIES SKIP_REGULAR_EXPRESSION "^SKIPPED:")
        endforeach()
    endif()

    set(DATUMLOOP_MUJOCO_RUNTIME_LIVE OFF)
    if(DATUMLOOP_MUJOCO_PYTHON)
        execute_process(
            COMMAND "${DATUMLOOP_MUJOCO_PYTHON}" -c "import mujoco"
            RESULT_VARIABLE DATUMLOOP_MUJOCO_IMPORT_STATUS
            OUTPUT_QUIET ERROR_QUIET
        )
        if(DATUMLOOP_MUJOCO_IMPORT_STATUS EQUAL 0)
            set(DATUMLOOP_MUJOCO_RUNTIME_LIVE ON)
            add_test(NAME datumloop-mujoco-runtime
                COMMAND "${DATUMLOOP_MUJOCO_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_mujoco_runtime.py"
                        "${DATUMLOOP_MJCF_OUTPUT}" --report "${CMAKE_CURRENT_BINARY_DIR}/reports/mujoco-runtime.json")
            set_tests_properties(datumloop-mujoco-runtime PROPERTIES DEPENDS datumloop-robotics-tests)
            if(DATUMLOOP_ENABLE_MFEM AND MFEM_FOUND)
                set_tests_properties(datumloop-mfem-elasticity-tests PROPERTIES DEPENDS datumloop-mujoco-runtime)
            endif()
        else()
            message(WARNING "DATUMLOOP_MUJOCO_PYTHON cannot import mujoco; runtime test is disabled")
            add_test(NAME datumloop-mujoco-runtime
                COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: configured Python cannot import MuJoCo")
            set_tests_properties(datumloop-mujoco-runtime PROPERTIES SKIP_REGULAR_EXPRESSION "^SKIPPED:")
        endif()
    else()
        add_test(NAME datumloop-mujoco-runtime
            COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: no Python interpreter configured with MuJoCo")
        set_tests_properties(datumloop-mujoco-runtime PROPERTIES SKIP_REGULAR_EXPRESSION "^SKIPPED:")
    endif()

    if(DATUMLOOP_ENABLE_MFEM AND MFEM_FOUND)
        target_compile_definitions(datumloop-mfem-elasticity-tests PRIVATE
            DATUMLOOP_MUJOCO_RUNTIME_LIVE=$<BOOL:${DATUMLOOP_MUJOCO_RUNTIME_LIVE}>)
    endif()

    add_executable(datumloop-ui-tests tests/native/test_ui_model.cpp)
    target_link_libraries(datumloop-ui-tests PRIVATE datumloop_ui)
    add_test(NAME datumloop-ui-tests COMMAND datumloop-ui-tests)
endif()
