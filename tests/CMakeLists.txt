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
