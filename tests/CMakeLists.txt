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

# ── test_pcap_writer ──────────────────────────────────────────────
add_executable(test_pcap_writer test_pcap_writer.cpp)
target_link_libraries(test_pcap_writer PRIVATE rbs_common)
add_test(NAME test_pcap_writer COMMAND test_pcap_writer)

# ── test_x2ap_link ────────────────────────────────────────────────
add_executable(test_x2ap_link test_x2ap_link.cpp)
target_link_libraries(test_x2ap_link PRIVATE rbs_lte)
add_test(NAME test_x2ap_link COMMAND test_x2ap_link)

# ── test_abis_link ────────────────────────────────────────────────
add_executable(test_abis_link test_abis_link.cpp)
target_link_libraries(test_abis_link PRIVATE rbs_gsm)
add_test(NAME test_abis_link COMMAND test_abis_link)

# ── test_abis_ipa_golden ──────────────────────────────────────────
add_executable(test_abis_ipa_golden test_abis_ipa_golden.cpp)
target_link_libraries(test_abis_ipa_golden PRIVATE rbs_gsm)
add_test(NAME test_abis_ipa_golden COMMAND test_abis_ipa_golden)

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

# ── test_csfb ─────────────────────────────────────────────────────
add_executable(test_csfb test_csfb.cpp)
target_link_libraries(test_csfb PRIVATE rbs_lte rbs_gsm rbs_oms)
add_test(NAME test_csfb COMMAND test_csfb)

# ── test_son ──────────────────────────────────────────────────────
add_executable(test_son test_son.cpp)
target_link_libraries(test_son PRIVATE rbs_common)
add_test(NAME test_son COMMAND test_son)

# ── test_nbap_dch ─────────────────────────────────────────────────
add_executable(test_nbap_dch test_nbap_dch.cpp)
target_link_libraries(test_nbap_dch PRIVATE rbs_umts)
add_test(NAME test_nbap_dch COMMAND test_nbap_dch)

# ── test_lte_ul_phy ───────────────────────────────────────────────
add_executable(test_lte_ul_phy test_lte_ul_phy.cpp)
target_link_libraries(test_lte_ul_phy PRIVATE rbs_lte)
add_test(NAME test_lte_ul_phy COMMAND test_lte_ul_phy)

# ── test_lte_measurement ──────────────────────────────────────────
add_executable(test_lte_measurement test_lte_measurement.cpp)
target_link_libraries(test_lte_measurement PRIVATE rbs_lte)
add_test(NAME test_lte_measurement COMMAND test_lte_measurement)

# ── test_nbap_edch ────────────────────────────────────────────────
add_executable(test_nbap_edch test_nbap_edch.cpp)
target_link_libraries(test_nbap_edch PRIVATE rbs_umts)
add_test(NAME test_nbap_edch COMMAND test_nbap_edch)

# ── test_umts_soft_ho ─────────────────────────────────────────────
add_executable(test_umts_soft_ho test_umts_soft_ho.cpp)
target_link_libraries(test_umts_soft_ho PRIVATE rbs_umts)
add_test(NAME test_umts_soft_ho COMMAND test_umts_soft_ho)

# ── test_lte_ca ───────────────────────────────────────────────────
add_executable(test_lte_ca test_lte_ca.cpp)
target_link_libraries(test_lte_ca PRIVATE rbs_lte)
add_test(NAME test_lte_ca COMMAND test_lte_ca)

# ── test_rohc ─────────────────────────────────────────────────────
add_executable(test_rohc test_rohc.cpp)
target_link_libraries(test_rohc PRIVATE rbs_lte)
add_test(NAME test_rohc COMMAND test_rohc)

# ── test_nr_phy ───────────────────────────────────────────────────
add_executable(test_nr_phy test_nr_phy.cpp)
target_link_libraries(test_nr_phy PRIVATE rbs_nr)
add_test(NAME test_nr_phy COMMAND test_nr_phy)

# ── test_f1ap_codec ───────────────────────────────────────────────
add_executable(test_f1ap_codec test_f1ap_codec.cpp)
target_link_libraries(test_f1ap_codec PRIVATE rbs_nr)
add_test(NAME test_f1ap_codec COMMAND test_f1ap_codec)

# ── test_ngap_codec ───────────────────────────────────────────────
add_executable(test_ngap_codec test_ngap_codec.cpp)
target_link_libraries(test_ngap_codec PRIVATE rbs_nr)
add_test(NAME test_ngap_codec COMMAND test_ngap_codec)

# ── test_nr_mac ───────────────────────────────────────────────────
add_executable(test_nr_mac test_nr_mac.cpp)
target_link_libraries(test_nr_mac PRIVATE rbs_nr)
add_test(NAME test_nr_mac COMMAND test_nr_mac)

# ── test_nr_stack ─────────────────────────────────────────────────
add_executable(test_nr_stack test_nr_stack.cpp)
target_link_libraries(test_nr_stack PRIVATE rbs_nr)
add_test(NAME test_nr_stack COMMAND test_nr_stack)

# ── test_slicing ──────────────────────────────────────────────────
add_executable(test_slicing test_slicing.cpp)
target_link_libraries(test_slicing PRIVATE rbs_nr)
add_test(NAME test_slicing COMMAND test_slicing)

# ── test_xnap ─────────────────────────────────────────────────────
add_executable(test_xnap test_xnap.cpp)
target_link_libraries(test_xnap PRIVATE rbs_nr)
add_test(NAME test_xnap COMMAND test_xnap)

# ── test_ng_xn_transport (p.29) ─────────────────────────────────
add_executable(test_ng_xn_transport test_ng_xn_transport.cpp)
target_link_libraries(test_ng_xn_transport PRIVATE rbs_nr)
add_test(NAME test_ng_xn_transport COMMAND test_ng_xn_transport)

# ── test_rest_api ─────────────────────────────────────────────────
add_executable(test_rest_api test_rest_api.cpp)
target_link_libraries(test_rest_api PRIVATE rbs_api)
add_test(NAME test_rest_api COMMAND test_rest_api)

# ── test_endc ─────────────────────────────────────────────────────
add_executable(test_endc test_endc.cpp)
target_link_libraries(test_endc PRIVATE rbs_lte rbs_nr)
add_test(NAME test_endc COMMAND test_endc)

# ── test_pdcp_security ────────────────────────────────────────────
add_executable(test_pdcp_security test_pdcp_security.cpp)
target_link_libraries(test_pdcp_security PRIVATE rbs_lte)
add_test(NAME test_pdcp_security COMMAND test_pdcp_security)

# ── test_pm_export ────────────────────────────────────────────────
add_executable(test_pm_export test_pm_export.cpp)
target_link_libraries(test_pm_export PRIVATE rbs_oms rbs_common)
add_test(NAME test_pm_export COMMAND test_pm_export)

# ── test_volte ───────────────────────────────────────────────────
add_executable(test_volte test_volte.cpp)
target_link_libraries(test_volte PRIVATE rbs_lte rbs_oms)
add_test(NAME test_volte COMMAND test_volte)

# ── test_multi_cell ──────────────────────────────────────────────
add_executable(test_multi_cell test_multi_cell.cpp)
target_link_libraries(test_multi_cell PRIVATE rbs_lte rbs_oms)
add_test(NAME test_multi_cell COMMAND test_multi_cell)
