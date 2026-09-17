cmake_minimum_required(VERSION 3.24)
project(pvp_duel_client LANGUAGES C CXX)

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
        list(APPEND CMAKE_PREFIX_PATH
            "${BREW_PREFIX}"
            "${MSQUIC_BREW_PREFIX}"
            "${PROTOBUF_BREW_PREFIX}"
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
        message(FATAL_ERROR "MsQuic is missing. Run `make setup-msquic` from the repository root.")
    endif()
    list(GET MSQUIC_LIBRARIES 0 MSQUIC_LIBRARY)
endif()
get_filename_component(MSQUIC_LIBRARY_DIR "${MSQUIC_LIBRARY}" DIRECTORY)

find_package(Protobuf REQUIRED)

add_library(duel_protocol STATIC proto/game/v1/duel.pb.cc)
target_include_directories(duel_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/proto")
target_link_libraries(duel_protocol PUBLIC protobuf::libprotobuf)

add_library(duel_game STATIC game/prediction.cpp game/interpolation.cpp game/match_lifecycle.cpp)
target_include_directories(duel_game PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(duel_game PUBLIC duel_protocol)
target_compile_options(duel_game PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_api STATIC api/gateway_client.cpp)
target_include_directories(duel_api PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(duel_api PUBLIC httplib::httplib nlohmann_json::nlohmann_json)
target_compile_options(duel_api PRIVATE -Wall -Wextra -Wpedantic)

add_library(duel_net STATIC net/quic_client.cpp)
target_include_directories(duel_net PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(duel_net SYSTEM PRIVATE "${MSQUIC_INCLUDE_DIR}")
target_link_libraries(duel_net PUBLIC duel_protocol PRIVATE "${MSQUIC_LIBRARY}" ${XDP_LIBRARIES})
target_compile_options(duel_net PRIVATE -Wall -Wextra -Wpedantic)

add_executable(pvp_duel_client app/main.cpp)
target_link_libraries(pvp_duel_client PRIVATE raylib duel_api duel_game duel_net)
target_compile_options(pvp_duel_client PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(pvp_duel_client PROPERTIES BUILD_RPATH "${MSQUIC_LIBRARY_DIR}")
if(UNIX AND NOT APPLE)
    target_link_options(pvp_duel_client PRIVATE "-Wl,--disable-new-dtags")
endif()

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

add_executable(match_lifecycle_test tests/match_lifecycle_test.cpp)
target_link_libraries(match_lifecycle_test PRIVATE duel_game)
target_compile_options(match_lifecycle_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME match_lifecycle_test COMMAND match_lifecycle_test)
