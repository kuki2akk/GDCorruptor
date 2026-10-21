cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(GD_Corruptor VERSION 1.0.0)

file(GLOB_RECURSE MOD_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(${PROJECT_NAME} SHARED ${MOD_SOURCES})

if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map")
endif()

# Путь к Geode SDK: переменная GEODE_SDK (её задаёт geode CLI) или окружение
# (локальная сборка через build.ps1 задаёт $env:GEODE_SDK).
if(NOT GEODE_SDK)
    set(GEODE_SDK $ENV{GEODE_SDK} CACHE PATH "Path to the Geode SDK")
endif()
if(NOT EXISTS "${GEODE_SDK}/CMakeLists.txt")
    message(FATAL_ERROR "Geode SDK not found at ${GEODE_SDK}. Set GEODE_SDK to the SDK folder.")
endif()

add_subdirectory(${GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Линкует geode-sdk, читает mod.json, создаёт цель <mod>_PACKAGE,
# которая собирает .geode и устанавливает его в папку модов игры.
setup_geode_mod(${PROJECT_NAME})

# Локальная пост-сборка (только Windows): копирование .geode в папку модов
# GD и (если не запрещено) запуск игры. CORRUPTOR_NO_LAUNCH=1 отключает запуск.
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME}_PACKAGE POST_BUILD
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/postbuild.ps1"
            -BuildDir "${CMAKE_BINARY_DIR}"
            -ModName "${PROJECT_NAME}"
        COMMENT "Post-build: copy .geode to Geode mods folder + optional game launch"
        VERBATIM
    )
endif()
