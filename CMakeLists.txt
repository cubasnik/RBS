cmake_minimum_required(VERSION 3.16)
project(RBS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Build options ──────────────────────────────────────────────
option(RBS_ENABLE_TESTS   "Build unit tests"       ON)
option(RBS_ENABLE_ASAN    "Enable AddressSanitizer" OFF)

# ── Compiler flags ─────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /WX- /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(RBS_ENABLE_ASAN)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# ─────────────────────────────────────────────────────────────────
# Library: common
# ─────────────────────────────────────────────────────────────────
add_library(rbs_common STATIC
    src/common/config.cpp
)
target_include_directories(rbs_common PUBLIC src)

# ─────────────────────────────────────────────────────────────────
# Library: HAL
# ─────────────────────────────────────────────────────────────────
add_library(rbs_hal STATIC
    src/hal/rf_hardware.cpp
)
target_link_libraries(rbs_hal PUBLIC rbs_common)

# ─────────────────────────────────────────────────────────────────
# Library: GSM stack
# ─────────────────────────────────────────────────────────────────
add_library(rbs_gsm STATIC
    src/gsm/gsm_phy.cpp
    src/gsm/gsm_mac.cpp
    src/gsm/gsm_stack.cpp
)
target_link_libraries(rbs_gsm PUBLIC rbs_hal)

# ─────────────────────────────────────────────────────────────────
# Library: UMTS stack
# ─────────────────────────────────────────────────────────────────
add_library(rbs_umts STATIC
    src/umts/umts_phy.cpp
    src/umts/umts_mac.cpp
    src/umts/umts_stack.cpp
)
target_link_libraries(rbs_umts PUBLIC rbs_hal)

# ─────────────────────────────────────────────────────────────────
# Library: LTE stack
# ─────────────────────────────────────────────────────────────────
add_library(rbs_lte STATIC
    src/lte/lte_phy.cpp
    src/lte/lte_mac.cpp
    src/lte/lte_pdcp.cpp
    src/lte/lte_stack.cpp
)
target_link_libraries(rbs_lte PUBLIC rbs_hal)

# ─────────────────────────────────────────────────────────────────
# Library: OMS
# ─────────────────────────────────────────────────────────────────
add_library(rbs_oms STATIC
    src/oms/oms.cpp
)
target_link_libraries(rbs_oms PUBLIC rbs_common)

# ─────────────────────────────────────────────────────────────────
# Executable: RBS node
# ─────────────────────────────────────────────────────────────────
add_executable(rbs_node src/main.cpp)
target_include_directories(rbs_node PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(rbs_node PRIVATE
    rbs_gsm
    rbs_umts
    rbs_lte
    rbs_oms
)

# Link pthreads on Linux/macOS
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(rbs_node PRIVATE Threads::Threads)
    target_link_libraries(rbs_gsm  PUBLIC  Threads::Threads)
    target_link_libraries(rbs_umts PUBLIC  Threads::Threads)
    target_link_libraries(rbs_lte  PUBLIC  Threads::Threads)
endif()

# ─────────────────────────────────────────────────────────────────
# Unit tests (optional – requires CTest, no external test framework)
# ─────────────────────────────────────────────────────────────────
if(RBS_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─────────────────────────────────────────────────────────────────
# Install
# ─────────────────────────────────────────────────────────────────
install(TARGETS rbs_node DESTINATION bin)
install(FILES rbs.conf   DESTINATION etc/rbs)
