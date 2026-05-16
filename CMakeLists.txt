cmake_minimum_required(VERSION 3.10)
project(BeatSaberBridgeAPI.CPP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Fetch external libraries
include(FetchContent)

# Fetch cpp-httplib
FetchContent_Declare(cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG master
)

# Fetch nlohmann/json
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG master
)

FetchContent_MakeAvailable(cpp_httplib json)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED libzip)

add_executable(BeatSaberBridgeAPI.CPP main.cpp)

set_target_properties(BeatSaberBridgeAPI.CPP PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

# Link the external libraries
target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE nlohmann_json::nlohmann_json ssl crypto)


# Add cpp-httplib include directory
target_include_directories(BeatSaberBridgeAPI.CPP PRIVATE ${cpp_httplib_SOURCE_DIR})

target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE ${LIBZIP_LIBRARIES})
target_include_directories(BeatSaberBridgeAPI.CPP PRIVATE ${LIBZIP_INCLUDE_DIRS})

if(WIN32)
    target_compile_definitions(BeatSaberBridgeAPI.CPP PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE ws2_32)
endif()

# Define some handy Social SDK variables
set(DISCORD_SDK_ROOT "${CMAKE_SOURCE_DIR}/lib/discord_social_sdk")
set(DISCORD_SDK_LIB_DIR "${DISCORD_SDK_ROOT}/lib/release")
set(DISCORD_SDK_BIN_DIR "${DISCORD_SDK_ROOT}/bin/release")
set(DISCORD_SDK_INCLUDE_DIR "${DISCORD_SDK_ROOT}/include")

# Include for Social SDK headers
target_include_directories(BeatSaberBridgeAPI.CPP PRIVATE ${DISCORD_SDK_INCLUDE_DIR})

# Platform-specific Social SDK library paths
if(WIN32)
    set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/discord_partner_sdk.lib")
    set(DISCORD_SHARED_LIB "${DISCORD_SDK_BIN_DIR}/discord_partner_sdk.dll")
elseif(APPLE)
    set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.dylib")
    set(DISCORD_SHARED_LIB "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.dylib")
elseif(UNIX)
    set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.so")
    set(DISCORD_SHARED_LIB "${DISCORD_SDK_LIB_DIR}/libdiscord_partner_sdk.so")
endif()

# Keep the shared library filename for post-build copy.
get_filename_component(DISCORD_SHARED_LIB_NAME "${DISCORD_SHARED_LIB}" NAME)

# Link the Social SDK library
target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE ${DISCORD_LIB_PATH})

# Set the runtime search path (RPATH) for Linux and macOS
if(UNIX)
    # Use RPATH when building
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    # Set the RPATH to use the lib directory relative to the executable
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
    if(APPLE)
        set(CMAKE_INSTALL_RPATH "@executable_path")
    endif()
endif()

# Copy Social SDK shared library to output directory, so it's available at runtime.
add_custom_command(TARGET BeatSaberBridgeAPI.CPP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${DISCORD_SHARED_LIB}"
    "$<TARGET_FILE_DIR:BeatSaberBridgeAPI.CPP>/${DISCORD_SHARED_LIB_NAME}"
)
