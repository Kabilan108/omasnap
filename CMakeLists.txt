cmake_minimum_required(VERSION 3.24)
project(omasnap VERSION 1.17.1 LANGUAGES CXX)
include(GNUInstallDirs)
include(CTest)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core Gui Test Widgets)
find_package(LayerShellQt REQUIRED)

add_library(omasnap-core STATIC
  src/cli-path.cpp
  src/cli-path.hpp
  src/icons.cpp
  src/icons.hpp
  src/instance-lock.cpp
  src/instance-lock.hpp
  src/pin-file.cpp
  src/pin-file.hpp
  src/pin-layout.cpp
  src/pin-layout.hpp
  src/capture.cpp
  src/capture.hpp
  src/cut.cpp
  src/cut.hpp
  src/palette-config.cpp
  src/palette-config.hpp
  src/eyedropper.cpp
  src/eyedropper.hpp
  src/editor.cpp
  src/editor.hpp
)
qt_add_resources(omasnap-core "capture-fonts"
  PREFIX "/fonts"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/Neucha.ttf"
)

target_include_directories(omasnap-core PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(omasnap-core PUBLIC cxx_std_23)
target_compile_options(omasnap-core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(omasnap-core PUBLIC
  LayerShellQt::Interface
  Qt6::Concurrent
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
)

qt_add_executable(omasnap src/main.cpp src/pin.cpp src/pin.hpp)
target_compile_options(omasnap PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(omasnap PRIVATE OMASNAP_VERSION="${PROJECT_VERSION}")
set_target_properties(omasnap PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
target_link_libraries(omasnap PRIVATE omasnap-core)

if(BUILD_TESTING)
qt_add_executable(omasnap-smoke
  tests/editor-smoke.cpp
  tests/clipboard-smoke.cpp
  tests/clipboard-smoke.hpp
  tests/instance-lock-smoke.cpp
  tests/instance-lock-smoke.hpp
  tests/transform-smoke.cpp
  tests/transform-smoke.hpp
  tests/cut-smoke.cpp
  tests/cut-smoke.hpp
  tests/palette-config-smoke.cpp
  tests/palette-config-smoke.hpp
  tests/pin-lifecycle-smoke.cpp
  tests/pin-lifecycle-smoke.hpp
  tests/pin-layout-smoke.cpp
  tests/pin-layout-smoke.hpp
)
target_include_directories(omasnap-smoke PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/tests"
)
target_compile_options(omasnap-smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(omasnap-smoke PRIVATE omasnap-core Qt6::Test)
add_test(NAME omasnap-smoke COMMAND omasnap-smoke "${CMAKE_CURRENT_BINARY_DIR}/omasnap-smoke-output")
set_tests_properties(omasnap-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS omasnap
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES assets/OFL.txt
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omasnap"
  RENAME Neucha-OFL.txt
)
install(FILES LICENSE
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omasnap"
  RENAME MIT.txt
)
install(FILES omasnap.desktop
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)
