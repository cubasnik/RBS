cmake_minimum_required(VERSION 3.16)

# ─────────────────────────────────────────────────────────────────
# Unit tests  (no external framework needed – plain CTest)
# ─────────────────────────────────────────────────────────────────

# ── test_config ───────────────────────────────────────────────────
add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE rbs_common)
add_test(NAME test_config COMMAND test_config)

# ── test_gsm_phy ──────────────────────────────────────────────────
add_executable(test_gsm_phy test_gsm_phy.cpp)
target_link_libraries(test_gsm_phy PRIVATE rbs_gsm)
add_test(NAME test_gsm_phy COMMAND test_gsm_phy)

# ── test_lte_mac ──────────────────────────────────────────────────
add_executable(test_lte_mac test_lte_mac.cpp)
target_link_libraries(test_lte_mac PRIVATE rbs_lte)
add_test(NAME test_lte_mac COMMAND test_lte_mac)

# ── test_pdcp ─────────────────────────────────────────────────────
add_executable(test_pdcp test_pdcp.cpp)
target_link_libraries(test_pdcp PRIVATE rbs_lte)
add_test(NAME test_pdcp COMMAND test_pdcp)

# ── test_lte_phy ──────────────────────────────────────────────────
add_executable(test_lte_phy test_lte_phy.cpp)
target_link_libraries(test_lte_phy PRIVATE rbs_lte)
add_test(NAME test_lte_phy COMMAND test_lte_phy)

# ── test_lte_rlc ──────────────────────────────────────────────────
add_executable(test_lte_rlc test_lte_rlc.cpp)
target_link_libraries(test_lte_rlc PRIVATE rbs_lte)
add_test(NAME test_lte_rlc COMMAND test_lte_rlc)

# ── test_umts_mac ─────────────────────────────────────────────────
add_executable(test_umts_mac test_umts_mac.cpp)
target_link_libraries(test_umts_mac PRIVATE rbs_umts)
add_test(NAME test_umts_mac COMMAND test_umts_mac)

# ── test_s1ap_codec ───────────────────────────────────────────────
add_executable(test_s1ap_codec test_s1ap_codec.cpp)
target_link_libraries(test_s1ap_codec PRIVATE rbs_lte)
add_test(NAME test_s1ap_codec COMMAND test_s1ap_codec)

# ── test_x2ap_codec ───────────────────────────────────────────────
add_executable(test_x2ap_codec test_x2ap_codec.cpp)
target_link_libraries(test_x2ap_codec PRIVATE rbs_lte)
add_test(NAME test_x2ap_codec COMMAND test_x2ap_codec)

# ── test_nbap_codec ───────────────────────────────────────────────
add_executable(test_nbap_codec test_nbap_codec.cpp)
target_link_libraries(test_nbap_codec PRIVATE rbs_umts)
add_test(NAME test_nbap_codec COMMAND test_nbap_codec)

# ── test_gtp_u ────────────────────────────────────────────────────
add_executable(test_gtp_u test_gtp_u.cpp)
target_link_libraries(test_gtp_u PRIVATE rbs_lte)
add_test(NAME test_gtp_u COMMAND test_gtp_u)
