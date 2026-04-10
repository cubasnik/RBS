cmake_minimum_required(VERSION 3.16)
project(RBS VERSION 1.0.0 LANGUAGES C CXX)

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
    src/common/udp_socket.cpp
    src/common/mobility_manager.cpp
    src/common/son.cpp
)
target_include_directories(rbs_common PUBLIC src)
# Winsock2 требуется для UdpSocket на Windows
if(WIN32)
    target_link_libraries(rbs_common PUBLIC ws2_32)
endif()

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
    src/gsm/abis_link.cpp
    src/gsm/gsm_rr.cpp
    src/gsm/gsm_rlc.cpp
)
target_link_libraries(rbs_gsm PUBLIC rbs_hal)

# ─────────────────────────────────────────────────────────────────
# Library: UMTS stack
# ─────────────────────────────────────────────────────────────────
add_library(rbs_umts STATIC
    src/umts/umts_phy.cpp
    src/umts/umts_mac.cpp
    src/umts/umts_stack.cpp
    src/umts/umts_rrc.cpp
    src/umts/umts_rlc.cpp
    src/umts/iub_link.cpp
    src/umts/nbap_codec.cpp
)
target_link_libraries(rbs_umts PUBLIC rbs_hal rbs_asn1_nbap)

# ─────────────────────────────────────────────────────────────────
# Library: LTE stack
# ─────────────────────────────────────────────────────────────────
add_library(rbs_lte STATIC
    src/lte/lte_phy.cpp
    src/lte/lte_mac.cpp
    src/lte/lte_pdcp.cpp
    src/lte/lte_stack.cpp
    src/lte/s1ap_codec.cpp
    src/lte/s1ap_link.cpp
    src/lte/x2ap_codec.cpp
    src/lte/x2ap_link.cpp
    src/lte/gtp_u.cpp
    src/lte/lte_rrc.cpp
    src/lte/lte_rlc.cpp
)
target_link_libraries(rbs_lte PUBLIC rbs_hal rbs_asn1_s1ap rbs_asn1_x2ap)

# ─────────────────────────────────────────────────────────────────
# Library: OMS
# ─────────────────────────────────────────────────────────────────
add_library(rbs_oms STATIC
    src/oms/oms.cpp
)
target_link_libraries(rbs_oms PUBLIC rbs_common)

# ─────────────────────────────────────────────────────────────────
# Generated ASN.1 codec libraries (asn1c-mouse, APER-only)
#   Warnings suppressed — generated code, not maintained by hand.
# ─────────────────────────────────────────────────────────────────
file(GLOB S1AP_SOURCES "${CMAKE_SOURCE_DIR}/src/generated/s1ap/*.c")
add_library(rbs_asn1_s1ap STATIC ${S1AP_SOURCES})
set_target_properties(rbs_asn1_s1ap PROPERTIES C_STANDARD 11)
target_compile_definitions(rbs_asn1_s1ap PUBLIC
    ASN_DISABLE_OER_SUPPORT
    ASN_DISABLE_BER_SUPPORT
    ASN_DISABLE_XER_SUPPORT
    ASN_DISABLE_JER_SUPPORT
    ASN_DISABLE_CBOR_SUPPORT
    ASN_DISABLE_UPER_SUPPORT
    ASN1C_NO_UNSUFFIXED_PDU_ALIAS)
target_include_directories(rbs_asn1_s1ap PUBLIC
    "${CMAKE_SOURCE_DIR}/src/generated/s1ap"
)
if(MSVC)
    target_compile_options(rbs_asn1_s1ap PRIVATE /W0 /wd4244 /wd4267 /wd4996 /Ze /std:c11)
else()
    target_compile_options(rbs_asn1_s1ap PRIVATE -w)
endif()

file(GLOB X2AP_SOURCES "${CMAKE_SOURCE_DIR}/src/generated/x2ap/*.c")
add_library(rbs_asn1_x2ap STATIC ${X2AP_SOURCES})
set_target_properties(rbs_asn1_x2ap PROPERTIES C_STANDARD 11)
target_compile_definitions(rbs_asn1_x2ap PUBLIC
    ASN_DISABLE_OER_SUPPORT
    ASN_DISABLE_BER_SUPPORT
    ASN_DISABLE_XER_SUPPORT
    ASN_DISABLE_JER_SUPPORT
    ASN_DISABLE_CBOR_SUPPORT
    ASN_DISABLE_UPER_SUPPORT
    ASN1C_NO_UNSUFFIXED_PDU_ALIAS)
target_include_directories(rbs_asn1_x2ap PUBLIC
    "${CMAKE_SOURCE_DIR}/src/generated/x2ap"
)
if(MSVC)
    target_compile_options(rbs_asn1_x2ap PRIVATE /W0 /wd4244 /wd4267 /wd4996 /Ze /std:c11)
else()
    target_compile_options(rbs_asn1_x2ap PRIVATE -w)
endif()

file(GLOB NBAP_SOURCES "${CMAKE_SOURCE_DIR}/src/generated/nbap/*.c")
add_library(rbs_asn1_nbap STATIC ${NBAP_SOURCES})
set_target_properties(rbs_asn1_nbap PROPERTIES C_STANDARD 11)
target_compile_definitions(rbs_asn1_nbap PUBLIC
    ASN_DISABLE_OER_SUPPORT
    ASN_DISABLE_BER_SUPPORT
    ASN_DISABLE_XER_SUPPORT
    ASN_DISABLE_JER_SUPPORT
    ASN_DISABLE_CBOR_SUPPORT
    ASN_DISABLE_UPER_SUPPORT
    ASN1C_NO_UNSUFFIXED_PDU_ALIAS)
target_include_directories(rbs_asn1_nbap PUBLIC
    "${CMAKE_SOURCE_DIR}/src/generated/nbap"
)
if(MSVC)
    target_compile_options(rbs_asn1_nbap PRIVATE /W0 /wd4244 /wd4267 /wd4996 /Ze /std:c11)
else()
    target_compile_options(rbs_asn1_nbap PRIVATE -w)
endif()

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
