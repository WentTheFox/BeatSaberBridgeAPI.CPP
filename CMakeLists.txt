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

if(APPLE)
    set(RPATH_ORIGIN "@executable_path")
else()
    set(RPATH_ORIGIN "$ORIGIN")
endif()

if(WIN32)
    find_package(libzip CONFIG REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
endif()

find_package(OpenSSL REQUIRED)

add_executable(BeatSaberBridgeAPI.CPP main.cpp)

set_target_properties(BeatSaberBridgeAPI.CPP PROPERTIES
    OUTPUT_NAME "BeatSaberBridgeAPI"
    BUILD_RPATH "${RPATH_ORIGIN}"
    INSTALL_RPATH "${RPATH_ORIGIN}"
)

# Link the external libraries
target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE nlohmann_json::nlohmann_json OpenSSL::SSL OpenSSL::Crypto)


# Add cpp-httplib include directory
target_include_directories(BeatSaberBridgeAPI.CPP PRIVATE ${cpp_httplib_SOURCE_DIR})

if(WIN32)
    target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE libzip::zip)
else()
    target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE PkgConfig::LIBZIP)
endif()

if(WIN32)
    target_compile_definitions(BeatSaberBridgeAPI.CPP PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE ws2_32)
elseif(APPLE)
    target_link_libraries(BeatSaberBridgeAPI.CPP PRIVATE "-framework CoreFoundation" "-framework Security")
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


# Copy Social SDK shared library to output directory, so it's available at runtime.
add_custom_command(TARGET BeatSaberBridgeAPI.CPP POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${DISCORD_SHARED_LIB}"
    "$<TARGET_FILE_DIR:BeatSaberBridgeAPI.CPP>/${DISCORD_SHARED_LIB_NAME}"
)
