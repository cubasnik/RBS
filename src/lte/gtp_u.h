#pragma once
#include "../common/types.h"
#include <cstdint>

// GTP-U минимальный заголовок — TS 29.060 §6, 8 байт
// Используется для S1-U (eNB ↔ SGW, порт 2152)
// и X2-U (eNB source ↔ eNB target, порт 2152).

namespace rbs::lte {

// ─────────────────────────────────────────────────────────────────────────────
// Константы
// ─────────────────────────────────────────────────────────────────────────────

/// flags = 0x30  →  version=1, PT=1, E=0, S=0, PN=0
constexpr uint8_t GTPU_FLAGS_MINIMAL = 0x30;
/// msgType = 0xFF  →  T-PDU (user-plane data packet)
constexpr uint8_t GTPU_MSG_TPDU = 0xFF;
/// Стандартный UDP-порт GTP-U
constexpr uint16_t GTPU_PORT = 2152;
/// Размер минимального заголовка GTP-U
constexpr size_t GTPU_HEADER_SIZE = 8;

// ─────────────────────────────────────────────────────────────────────────────
// GTP-U API
// ─────────────────────────────────────────────────────────────────────────────

/// Сформировать UDP-датаграмму: 8-байтный GTP-U заголовок + payload.
///
/// Формат (big-endian, TS 29.060 §6.1):
///   octet 1   flags    = 0x30
///   octet 2   msgType  = 0xFF
///   octet 3-4 length   = payload length
///   octet 5-8 TEID     = tunnel endpoint identifier
ByteBuffer gtpuEncode(uint32_t teid, const ByteBuffer& payload);

/// Разобрать входящую датаграмму, проверить GTP-U заголовок,
/// извлечь TEID и полезную нагрузку.
/// Возвращает false при некорректном заголовке (слишком короткий, неверный флаг или тип).
bool gtpuDecode(const ByteBuffer& raw,
                uint32_t& teid,
                ByteBuffer& payload);

} // namespace rbs::lte
