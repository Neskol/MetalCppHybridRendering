cmake_minimum_required(VERSION 3.10)
project(HybridRenderingPlayground LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(SOURCES
    main.mm
    Renderer/Mtl_Engine.cpp
        Renderer/Mtl_Engine.h
    Metal.hpp
)
set(SHADERS Renderer/Shaders.metal)
set(ASSETS Models/cornell-box.obj Models/cornell-box.mtl)
add_executable(${PROJECT_NAME} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE Renderer)

find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
find_library(MODELIO_FRAMEWORK ModelIO REQUIRED)
find_library(METALKIT_FRAMEWORK MetalKit REQUIRED)
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(METALFX_FRAMEWORK MetalFX REQUIRED)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    ${QUARTZCORE_FRAMEWORK}
    ${FOUNDATION_FRAMEWORK}
    ${COCOA_FRAMEWORK}
    ${MODELIO_FRAMEWORK}
    ${METALKIT_FRAMEWORK}
    ${METAL_FRAMEWORK}
    ${METALFX_FRAMEWORK}
)

configure_file(Models/cornell-box.obj ${CMAKE_CURRENT_BINARY_DIR}/cornell-box.obj COPYONLY)
configure_file(Models/cornell-box.mtl ${CMAKE_CURRENT_BINARY_DIR}/cornell-box.mtl COPYONLY)
configure_file(Renderer/Shaders.metal ${CMAKE_CURRENT_BINARY_DIR}/Shaders.metal COPYONLY)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/Models/cornell-box.obj $<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/cornell-box.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/Models/cornell-box.mtl $<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/cornell-box.mtl
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/Renderer/Shaders.metal $<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/Shaders.metal
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.HybridRenderingPlayground"
)