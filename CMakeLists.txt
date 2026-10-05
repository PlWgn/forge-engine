cmake_minimum_required(VERSION 3.24)
project(Forge VERSION 2.5.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(PYBIND11_FINDPYTHON ON)
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Embed)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glfw/CMakeLists.txt")
  message(FATAL_ERROR "Run python tools/dependencies.py first")
endif()
add_subdirectory(vendor/glfw EXCLUDE_FROM_ALL)
add_subdirectory(vendor/pybind11 EXCLUDE_FROM_ALL)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_COLLADA_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
if(APPLE)
  set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
else()
  set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
endif()
add_subdirectory(vendor/assimp EXCLUDE_FROM_ALL)
# Only the CPU rigid-body libraries are used; no Bullet demos, servers or Python bindings.
set(BULLET_PHYSICS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/bullet")
set(BULLET_VERSION 3.25)
set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
add_compile_definitions(BT_USE_DOUBLE_PRECISION)
add_subdirectory(vendor/bullet/src/LinearMath EXCLUDE_FROM_ALL)
add_subdirectory(vendor/bullet/src/BulletCollision EXCLUDE_FROM_ALL)
add_subdirectory(vendor/bullet/src/BulletDynamics EXCLUDE_FROM_ALL)
option(FORGE_WITH_EDITOR "Build the optional builtin ImGui editor shell" ON)
if(FORGE_WITH_EDITOR)
add_library(forge_editor_ui STATIC vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_tables.cpp vendor/imgui/imgui_widgets.cpp vendor/imgui/backends/imgui_impl_glfw.cpp vendor/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(forge_editor_ui PUBLIC vendor/imgui vendor/imgui/backends)
target_link_libraries(forge_editor_ui PRIVATE glfw OpenGL::GL)
endif()
add_executable(forge
  engine/src/main.cpp
  engine/src/config.cpp
  engine/src/log.cpp
  engine/src/documents.cpp
  engine/src/editor_session.cpp
  engine/src/world.cpp
  engine/src/render.cpp
  engine/src/runtime.cpp
  engine/src/game_loop.cpp
  engine/src/window.cpp
  engine/src/dispatch.cpp
  engine/src/file_watch.cpp
  engine/src/prefab.cpp
  engine/src/animation.cpp
  engine/src/animation_api.cpp
  engine/src/morph.cpp
  engine/src/audio.cpp
  engine/src/localization.cpp
  engine/src/platform.cpp
  engine/src/model.cpp
  engine/src/assets.cpp
  engine/src/features.cpp
  engine/src/physics.cpp
  engine/src/particles.cpp
  engine/src/hierarchy.cpp
  engine/src/material.cpp
  engine/src/geometry.cpp
  engine/src/particle_render.cpp
  engine/src/python_bridge.cpp
  engine/src/python_api.cpp
  engine/src/scene_runtime.cpp
  engine/src/reload.cpp
  engine/src/gpu_resources.cpp
  engine/src/text.cpp
  engine/src/media.cpp
)
target_include_directories(forge PRIVATE engine/include vendor vendor/glm)
target_include_directories(forge SYSTEM PRIVATE vendor/bullet/src)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS graphics/default.vert graphics/default.frag graphics/post.frag graphics/particle.vert graphics/particle-instance.vert graphics/particle.frag)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/graphics/default.vert" FORGE_VERTEX)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/graphics/default.frag" FORGE_FRAGMENT)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/graphics/post.frag" FORGE_POST)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/graphics/particle.vert" FORGE_PARTICLE_VERTEX)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/graphics/particle-instance.vert" FORGE_PARTICLE_INSTANCE)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/graphics/particle.frag" FORGE_PARTICLE_FRAGMENT)
configure_file(engine/resources/shaders.hpp.in generated/forge/shaders.hpp @ONLY)
target_include_directories(forge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(forge PRIVATE glfw pybind11::embed OpenGL::GL Threads::Threads assimp BulletDynamics BulletCollision LinearMath)
if(FORGE_WITH_EDITOR)
  target_sources(forge PRIVATE engine/src/editor.cpp engine/src/animation_editor.cpp)
  target_link_libraries(forge PRIVATE forge_editor_ui)
endif()
target_compile_definitions(forge PRIVATE FORGE_WITH_EDITOR=$<BOOL:${FORGE_WITH_EDITOR}>)
target_compile_definitions(forge PRIVATE FORGE_VERSION="${PROJECT_VERSION}" FORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" GL_SILENCE_DEPRECATION)
if(APPLE)
  target_link_libraries(forge PRIVATE "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation")
elseif(WIN32)
  target_link_libraries(forge PRIVATE winmm ole32 shell32)
  target_compile_definitions(forge PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
  target_compile_options(forge PRIVATE /W4 /utf-8 /bigobj)
else()
  target_compile_options(forge PRIVATE -Wall -Wextra -Wpedantic)
endif()
# Optional engine-level C++ modules. Python game code never requires this step.
if(FORGE_MODULE_SOURCES)
  target_sources(forge PRIVATE ${FORGE_MODULE_SOURCES})
endif()
set_target_properties(forge PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
include(CTest)
if(BUILD_TESTING)
  add_executable(numeric_tests tests/numeric.cpp)
  target_include_directories(numeric_tests PRIVATE engine/include)
  add_test(NAME numeric COMMAND numeric_tests)
  add_test(NAME packager_paths COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/packager_paths.py)
  add_test(NAME integration COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py $<TARGET_FILE:forge>)
  add_test(NAME features COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/features.py $<TARGET_FILE:forge>)
  add_test(NAME project_api COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/project_api.py $<TARGET_FILE:forge>)
  add_test(NAME authoring COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/authoring.py $<TARGET_FILE:forge>)
  add_test(NAME rendering COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering.py $<TARGET_FILE:forge>)
  add_test(NAME simulation COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/simulation.py $<TARGET_FILE:forge>)
endif()
