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

# ── test_lte_stack ────────────────────────────────────────────────
add_executable(test_lte_stack test_lte_stack.cpp)
target_link_libraries(test_lte_stack PRIVATE rbs_lte)
add_test(NAME test_lte_stack COMMAND test_lte_stack)

# ── test_gsm_stack ────────────────────────────────────────────────
add_executable(test_gsm_stack test_gsm_stack.cpp)
target_link_libraries(test_gsm_stack PRIVATE rbs_gsm)
add_test(NAME test_gsm_stack COMMAND test_gsm_stack)

# ── test_umts_stack ───────────────────────────────────────────────
add_executable(test_umts_stack test_umts_stack.cpp)
target_link_libraries(test_umts_stack PRIVATE rbs_umts)
add_test(NAME test_umts_stack COMMAND test_umts_stack)

# ── test_oms ──────────────────────────────────────────────────────
add_executable(test_oms test_oms.cpp)
target_link_libraries(test_oms PRIVATE rbs_oms)
add_test(NAME test_oms COMMAND test_oms)

# ── test_gsm_rlc ──────────────────────────────────────────────────
add_executable(test_gsm_rlc test_gsm_rlc.cpp)
target_link_libraries(test_gsm_rlc PRIVATE rbs_gsm)
add_test(NAME test_gsm_rlc COMMAND test_gsm_rlc)

# ── test_gsm_rr ───────────────────────────────────────────────────
add_executable(test_gsm_rr test_gsm_rr.cpp)
target_link_libraries(test_gsm_rr PRIVATE rbs_gsm)
add_test(NAME test_gsm_rr COMMAND test_gsm_rr)

# ── test_umts_phy ─────────────────────────────────────────────────
add_executable(test_umts_phy test_umts_phy.cpp)
target_link_libraries(test_umts_phy PRIVATE rbs_umts)
add_test(NAME test_umts_phy COMMAND test_umts_phy)

# ── test_umts_rlc ─────────────────────────────────────────────────
add_executable(test_umts_rlc test_umts_rlc.cpp)
target_link_libraries(test_umts_rlc PRIVATE rbs_umts)
add_test(NAME test_umts_rlc COMMAND test_umts_rlc)

# ── test_umts_rrc ─────────────────────────────────────────────────
add_executable(test_umts_rrc test_umts_rrc.cpp)
target_link_libraries(test_umts_rrc PRIVATE rbs_umts)
add_test(NAME test_umts_rrc COMMAND test_umts_rrc)

# ── test_lte_rrc ──────────────────────────────────────────────────
add_executable(test_lte_rrc test_lte_rrc.cpp)
target_link_libraries(test_lte_rrc PRIVATE rbs_lte)
add_test(NAME test_lte_rrc COMMAND test_lte_rrc)

# ── test_s1ap_link ────────────────────────────────────────────────
add_executable(test_s1ap_link test_s1ap_link.cpp)
target_link_libraries(test_s1ap_link PRIVATE rbs_lte)
add_test(NAME test_s1ap_link COMMAND test_s1ap_link)

# ── test_x2ap_link ────────────────────────────────────────────────
add_executable(test_x2ap_link test_x2ap_link.cpp)
target_link_libraries(test_x2ap_link PRIVATE rbs_lte)
add_test(NAME test_x2ap_link COMMAND test_x2ap_link)

# ── test_abis_link ────────────────────────────────────────────────
add_executable(test_abis_link test_abis_link.cpp)
target_link_libraries(test_abis_link PRIVATE rbs_gsm)
add_test(NAME test_abis_link COMMAND test_abis_link)

# ── test_iub_link ─────────────────────────────────────────────────
add_executable(test_iub_link test_iub_link.cpp)
target_link_libraries(test_iub_link PRIVATE rbs_umts)
add_test(NAME test_iub_link COMMAND test_iub_link)

# ── test_integration ──────────────────────────────────────────────
add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration PRIVATE rbs_lte rbs_umts rbs_gsm rbs_oms)
add_test(NAME test_integration COMMAND test_integration)

# ── test_mobility ─────────────────────────────────────────────────
add_executable(test_mobility test_mobility.cpp)
target_link_libraries(test_mobility PRIVATE rbs_common rbs_oms)
add_test(NAME test_mobility COMMAND test_mobility)
