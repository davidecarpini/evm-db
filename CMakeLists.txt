cmake_minimum_required(VERSION 3.20)
project(evmdb C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG -fsanitize=address,undefined")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# hiredis (Redis C client)
find_library(HIREDIS_LIB hiredis)
find_path(HIREDIS_INCLUDE hiredis/hiredis.h)
if(NOT HIREDIS_LIB)
    message(STATUS "hiredis not found, will fetch")
    include(FetchContent)
    FetchContent_Declare(
        hiredis
        GIT_REPOSITORY https://github.com/redis/hiredis.git
        GIT_TAG        v1.2.0
    )
    FetchContent_MakeAvailable(hiredis)
    set(HIREDIS_LIB hiredis)
    set(HIREDIS_INCLUDE ${hiredis_SOURCE_DIR})
endif()

# evmone — we use the EVMC C interface only
# evmone is fetched and built as a dependency
include(FetchContent)
FetchContent_Declare(
    evmc
    GIT_REPOSITORY https://github.com/ethereum/evmc.git
    GIT_TAG        v12.0.0
)
FetchContent_MakeAvailable(evmc)

# secp256k1 for ECDSA signature verification
find_library(SECP256K1_LIB secp256k1)
find_path(SECP256K1_INCLUDE secp256k1.h)
if(NOT SECP256K1_LIB)
    message(STATUS "secp256k1 not found, will fetch")
    FetchContent_Declare(
        secp256k1
        GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
        GIT_TAG        v0.5.1
    )
    set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(secp256k1)
    set(SECP256K1_LIB secp256k1)
    set(SECP256K1_INCLUDE ${secp256k1_SOURCE_DIR}/include)
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------

add_library(evmdb_core STATIC
    src/core/config.c
    src/core/log.c
    src/core/hex.c
    src/state/redis_state.c
    src/state/accounts.c
    src/evm/executor.c
    src/evm/gas.c
    src/block/producer.c
    src/block/tx.c
    src/rpc/server.c
    src/rpc/handlers.c
    src/rpc/json.c
    src/net/tcp.c
)

target_include_directories(evmdb_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${HIREDIS_INCLUDE}
    ${SECP256K1_INCLUDE}
)

target_link_libraries(evmdb_core PUBLIC
    ${HIREDIS_LIB}
    ${SECP256K1_LIB}
    evmc
    pthread
)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------

add_executable(evmdb src/main.c)
target_link_libraries(evmdb PRIVATE evmdb_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

add_executable(test_hex tests/test_hex.c)
target_link_libraries(test_hex PRIVATE evmdb_core)
add_test(NAME test_hex COMMAND test_hex)

add_executable(test_state tests/test_state.c)
target_link_libraries(test_state PRIVATE evmdb_core)
add_test(NAME test_state COMMAND test_state)

add_executable(test_tx tests/test_tx.c)
target_link_libraries(test_tx PRIVATE evmdb_core)
add_test(NAME test_tx COMMAND test_tx)

add_executable(test_rpc tests/test_rpc.c)
target_link_libraries(test_rpc PRIVATE evmdb_core)
add_test(NAME test_rpc COMMAND test_rpc)
