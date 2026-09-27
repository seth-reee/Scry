cmake_minimum_required(VERSION 3.21)
project(Scry VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)

add_executable(scry src/main.cpp src/mesh.cpp src/mesh.h)
qt_add_resources(scry "assets" PREFIX "/" FILES LICENSE Scry.png)
target_compile_definitions(scry PRIVATE SCRY_VERSION="${PROJECT_VERSION}")
target_link_libraries(scry PRIVATE Qt6::Widgets Qt6::OpenGLWidgets PkgConfig::LIBZIP)
target_compile_options(scry PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS scry RUNTIME DESTINATION bin)
install(FILES Scry.png DESTINATION share/pixmaps RENAME scry.png)
install(FILES packaging/scry.desktop DESTINATION share/applications)
install(FILES LICENSE DESTINATION share/licenses/scry)
