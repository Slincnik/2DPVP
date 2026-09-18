cmake_minimum_required(VERSION 3.24)
project(pvp_duel_client LANGUAGES C CXX)

set(PVP_DUEL_DEFAULT_GATEWAY_URL "http://localhost:8080" CACHE STRING
    "Gateway URL embedded as the client default")
set(PVP_DUEL_VERSION "0.0.0-dev" CACHE STRING
    "Client version embedded in the binary")
set(PVP_DUEL_UPDATE_MANIFEST_URL "" CACHE STRING
    "HTTPS URL of the latest client update manifest")

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.7
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "Require HTTPS support" FORCE)
FetchContent_MakeAvailable(raylib httplib nlohmann_json)

if(APPLE)
    # Homebrew uses /opt/homebrew on Apple Silicon and /usr/local on Intel.
    # Add formula prefixes explicitly so CMake also finds Protobuf/OpenSSL.
    find_program(BREW_EXECUTABLE brew)
    if(BREW_EXECUTABLE)
        execute_process(
            COMMAND "${BREW_EXECUTABLE}" --prefix
            OUTPUT_VARIABLE BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND "${BREW_EXECUTABLE}" --prefix libmsquic
            OUTPUT_VARIABLE MSQUIC_BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND "${BREW_EXECUTABLE}" --prefix protobuf
            OUTPUT_VARIABLE PROTOBUF_BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND "${BREW_EXECUTABLE}" --prefix abseil
            OUTPUT_VARIABLE ABSEIL_BREW_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        list(APPEND CMAKE_PREFIX_PATH
            "${BREW_PREFIX}"
            "${MSQUIC_BREW_PREFIX}"
            "${PROTOBUF_BREW_PREFIX}"
            "${ABSEIL_BREW_PREFIX}"
        )
    endif()
    find_path(MSQUIC_INCLUDE_DIR msquic.h
        HINTS
            "${MSQUIC_BREW_PREFIX}/include"
            /opt/homebrew/include
            /usr/local/include
    )
    find_library(MSQUIC_LIBRARY NAMES msquic
        HINTS
            "${MSQUIC_BREW_PREFIX}/lib"
            /opt/homebrew/lib
            /usr/local/lib
    )
    if(NOT MSQUIC_INCLUDE_DIR OR NOT MSQUIC_LIBRARY)
        message(FATAL_ERROR "MsQuic is missing. Run `brew install libmsquic`.")
    endif()
    set(XDP_LIBRARIES "")
else()
    set(MSQUIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/msquic")
    set(MSQUIC_INCLUDE_DIR "${MSQUIC_ROOT}/include")
    file(GLOB MSQUIC_LIBRARIES "${MSQUIC_ROOT}/usr/lib/*/libmsquic.so.2")
    file(GLOB XDP_LIBRARIES "${MSQUIC_ROOT}/usr/lib/*/libxdp.so.1")
    list(LENGTH MSQUIC_LIBRARIES MSQUIC_LIBRARY_COUNT)
    if(MSQUIC_LIBRARY_COUNT EQUAL 0 OR NOT EXISTS "${MSQUIC_INCLUDE_DIR}/msquic.h")
        message(FATAL_ERROR "MsQuic is missing. Run `make client` from the repository root, or install MsQuic manually.")
    endif()
    list(GET MSQUIC_LIBRARIES 0 MSQUIC_LIBRARY)
    # MsQuic declares its runtime dependencies (libnuma/libnl where needed)
    # in the shared object's NEEDED entries. Do not require Linux-only
    # libnl development files or link them directly from the client.
endif()
get_filename_component(MSQUIC_LIBRARY_DIR "${MSQUIC_LIBRARY}" DIRECTORY)

find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
if(APPLE)
    # New Homebrew Protobuf releases emit inline ABSL_DCHECK helpers. The
    # protobuf target does not expose this dependency reliably in all formula
    # revisions, so link the implementation explicitly.
    find_package(absl CONFIG REQUIRED)
    if(NOT TARGET absl::log_internal_check_op)
        message(FATAL_ERROR "Homebrew Abseil target absl::log_internal_check_op is missing")
    endif()
    set(PROTOBUF_PLATFORM_LIBS absl::log_internal_check_op)
endif()

add_library(duel_protocol STATIC proto/game/v1/duel.pb.cc)
target_include_directories(duel_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/proto")
target_link_libraries(duel_protocol PUBLIC protobuf::libprotobuf ${PROTOBUF_PLATFORM_LIBS})

add_library(duel_match_protocol STATIC protocol/match_protocol_adapter.cpp)
target_include_directories(duel_match_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(duel_match_protocol PRIVATE duel_protocol)
target_compile_options(duel_match_protocol PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_app STATIC app/application.cpp)
target_include_directories(duel_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(duel_app PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_input STATIC game/input/input.cpp)
target_include_directories(duel_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(duel_input PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_settings STATIC platform/settings_store.cpp)
target_include_directories(duel_settings PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(duel_settings PUBLIC duel_input PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(duel_settings PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_game STATIC
    game/arena/arena_catalog.cpp
    game/prediction.cpp
    game/interpolation.cpp
    game/match_lifecycle.cpp
    game/match/match_model.cpp
    game/match/match_controller.cpp
    game/presentation/match_presentation.cpp
)
target_include_directories(duel_game PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(duel_game PUBLIC duel_input)
target_compile_options(duel_game PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_api STATIC api/gateway_client.cpp auth/secure_storage.cpp auth/session_manager.cpp)
target_include_directories(duel_api PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(duel_api PUBLIC httplib::httplib nlohmann_json::nlohmann_json)
if(WIN32)
    target_sources(duel_api PRIVATE auth/secure_storage_windows.cpp)
    target_link_libraries(duel_api PRIVATE Advapi32)
elseif(APPLE)
    target_sources(duel_api PRIVATE auth/secure_storage_macos.cpp)
    target_link_libraries(duel_api PRIVATE "-framework Security")
elseif(UNIX)

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
    endif()
    if(LIBSECRET_FOUND)
        target_link_libraries(duel_api PRIVATE PkgConfig::LIBSECRET)
        target_compile_definitions(duel_api PRIVATE PVP_DUEL_HAS_LIBSECRET=1)
    else()
        message(WARNING "libsecret-1 development files not found: persistent login is disabled")
    endif()
endif()
target_compile_options(duel_api PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_net STATIC net/quic_client.cpp)
target_include_directories(duel_net PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(duel_net SYSTEM PRIVATE "${MSQUIC_INCLUDE_DIR}")
target_link_libraries(duel_net PUBLIC duel_match_protocol PRIVATE
    duel_protocol "${MSQUIC_LIBRARY}" ${XDP_LIBRARIES}
)
target_compile_options(duel_net PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_client_app STATIC
    app/application_runtime.cpp
    app/screens/client_screens.cpp
    platform/raylib_input.cpp
)
target_include_directories(duel_client_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(duel_client_app PUBLIC duel_app PRIVATE
    raylib duel_api duel_game duel_input duel_net duel_settings
)
target_compile_options(duel_client_app PRIVATE -Wall -Wextra -Wpedantic)

if(UNIX AND NOT APPLE)
    add_executable(pvp_duel_updater update/updater.cpp)
    target_link_libraries(pvp_duel_updater PRIVATE httplib::httplib OpenSSL::Crypto)
    target_compile_options(pvp_duel_updater PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(pvp_duel_client app/main.cpp update/client_updater.cpp)
target_link_libraries(pvp_duel_client PRIVATE duel_client_app httplib::httplib nlohmann_json::nlohmann_json)
target_compile_definitions(pvp_duel_client PRIVATE
    PVP_DUEL_DEFAULT_GATEWAY_URL="${PVP_DUEL_DEFAULT_GATEWAY_URL}"
    PVP_DUEL_VERSION="${PVP_DUEL_VERSION}"
    PVP_DUEL_UPDATE_MANIFEST_URL="${PVP_DUEL_UPDATE_MANIFEST_URL}"
)
target_compile_options(pvp_duel_client PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(pvp_duel_client PROPERTIES BUILD_RPATH "${MSQUIC_LIBRARY_DIR}")
if(UNIX AND NOT APPLE)
    target_link_options(pvp_duel_client PRIVATE "-Wl,--disable-new-dtags")
endif()

add_executable(application_test tests/application_test.cpp)
target_link_libraries(application_test PRIVATE duel_app)
target_compile_options(application_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME application_test COMMAND application_test)

add_executable(input_test tests/input_test.cpp)
target_link_libraries(input_test PRIVATE duel_input)
target_compile_options(input_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME input_test COMMAND input_test)

add_executable(settings_store_test tests/settings_store_test.cpp)
target_link_libraries(settings_store_test PRIVATE duel_settings)
target_compile_options(settings_store_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME settings_store_test COMMAND settings_store_test)

add_executable(arena_catalog_test tests/arena_catalog_test.cpp)
target_link_libraries(arena_catalog_test PRIVATE duel_game)
target_compile_options(arena_catalog_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME arena_catalog_test COMMAND arena_catalog_test)

add_executable(match_protocol_adapter_test tests/match_protocol_adapter_test.cpp)
target_link_libraries(match_protocol_adapter_test PRIVATE duel_match_protocol duel_protocol)
target_compile_options(match_protocol_adapter_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME match_protocol_adapter_test COMMAND match_protocol_adapter_test)

add_executable(match_controller_test tests/match_controller_test.cpp)
target_link_libraries(match_controller_test PRIVATE duel_game)
target_compile_options(match_controller_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME match_controller_test COMMAND match_controller_test)

add_executable(match_presentation_test tests/match_presentation_test.cpp)
target_link_libraries(match_presentation_test PRIVATE duel_game)
target_compile_options(match_presentation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME match_presentation_test COMMAND match_presentation_test)

add_executable(prediction_test tests/prediction_test.cpp)
target_link_libraries(prediction_test PRIVATE duel_game)
target_compile_options(prediction_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME prediction_test COMMAND prediction_test)

add_executable(interpolation_test tests/interpolation_test.cpp)
target_link_libraries(interpolation_test PRIVATE duel_game)
target_compile_options(interpolation_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME interpolation_test COMMAND interpolation_test)

add_executable(gateway_client_test tests/gateway_client_test.cpp)
target_link_libraries(gateway_client_test PRIVATE duel_api)
target_compile_options(gateway_client_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME gateway_client_test COMMAND gateway_client_test)

add_executable(session_manager_test tests/session_manager_test.cpp)
target_link_libraries(session_manager_test PRIVATE duel_api)
target_compile_options(session_manager_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME session_manager_test COMMAND session_manager_test)

add_executable(match_lifecycle_test tests/match_lifecycle_test.cpp)
target_link_libraries(match_lifecycle_test PRIVATE duel_game)
target_compile_options(match_lifecycle_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME match_lifecycle_test COMMAND match_lifecycle_test)
