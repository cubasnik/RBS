#!/usr/bin/env python3
import argparse
import json
import signal
import socket
import threading
import time
from typing import Optional

IPA_FILTER_OML = 0x00
IPA_FILTER_RSL = 0x01

OML_OPSTART = 0x41
OML_OPSTART_ACK = 0x42
OML_GET_BTS_ATTR = 0x05

RSL_CHANNEL_ACTIVATION = 0x30
RSL_CHANNEL_ACTIVATION_ACK = 0x31
RSL_CHANNEL_RELEASE = 0x34
RSL_RF_CHANNEL_RELEASE_ACK = 0x36
RSL_PAGING_CMD = 0x51
RSL_DATA_CONFIRM = 0x12


class MockBscIpa:
    def __init__(self, host: str, port: int, stats_file: Optional[str] = None) -> None:
        self.host = host
        self.port = port
        self.stats_file = stats_file
        self.stop_evt = threading.Event()
        self.stats = {
            "connections": 0,
            "omlRx": 0,
            "omlTx": 0,
            "rslRx": 0,
            "rslTx": 0,
            "lastFrame": {},
        }

    @staticmethod
    def encode_frame(msg_filter: int, msg_type: int, payload: bytes) -> bytes:
        data_len = 2 + len(payload)
        if data_len > 0xFFFF:
            return b""
        return bytes((data_len & 0xFF, (data_len >> 8) & 0xFF, msg_filter & 0xFF, msg_type & 0xFF)) + payload

    @staticmethod
    def decode_frames(buf: bytearray):
        frames = []
        while True:
            if len(buf) < 4:
                break
            data_len = buf[0] | (buf[1] << 8)
            frame_size = 2 + data_len
            if len(buf) < frame_size:
                break
            msg_filter = buf[2]
            msg_type = buf[3]
            payload = bytes(buf[4:frame_size])
            del buf[:frame_size]
            frames.append((msg_filter, msg_type, payload))
        return frames

    def _write_stats(self) -> None:
        if not self.stats_file:
            return
        try:
            with open(self.stats_file, "w", encoding="utf-8") as f:
                json.dump(self.stats, f, indent=2)
        except Exception as ex:
            print(f"[mock-bsc] failed to write stats: {ex}")

    def _on_frame(self, conn: socket.socket, msg_filter: int, msg_type: int, payload: bytes) -> None:
        entity = payload[0] if payload else 0
        self.stats["lastFrame"] = {
            "filter": msg_filter,
            "type": msg_type,
            "entity": entity,
            "payloadLen": max(0, len(payload) - 1),
            "ts": int(time.time() * 1000),
        }

        if msg_filter == IPA_FILTER_OML:
            self.stats["omlRx"] += 1
            print(f"[mock-bsc] RX OML type=0x{msg_type:02X} entity=0x{entity:02X} len={max(0, len(payload)-1)}")
            if msg_type == OML_OPSTART:
                tx_payload = bytes((entity,))
                conn.sendall(self.encode_frame(IPA_FILTER_OML, OML_OPSTART_ACK, tx_payload))
                self.stats["omlTx"] += 1
                print("[mock-bsc] TX OML OPSTART_ACK")
            elif msg_type == OML_GET_BTS_ATTR:
                tx_payload = bytes((entity,))
                conn.sendall(self.encode_frame(IPA_FILTER_OML, OML_GET_BTS_ATTR, tx_payload))
                self.stats["omlTx"] += 1
                print("[mock-bsc] TX OML GET_BTS_ATTR(response)")

        elif msg_filter == IPA_FILTER_RSL:
            self.stats["rslRx"] += 1
            print(f"[mock-bsc] RX RSL type=0x{msg_type:02X} chan=0x{entity:02X} len={max(0, len(payload)-1)}")
            if msg_type == RSL_CHANNEL_ACTIVATION:
                tx_payload = bytes((entity,))
                conn.sendall(self.encode_frame(IPA_FILTER_RSL, RSL_CHANNEL_ACTIVATION_ACK, tx_payload))
                self.stats["rslTx"] += 1
                print("[mock-bsc] TX RSL CHANNEL_ACTIVATION_ACK")
            elif msg_type == RSL_CHANNEL_RELEASE:
                tx_payload = bytes((entity,))
                conn.sendall(self.encode_frame(IPA_FILTER_RSL, RSL_RF_CHANNEL_RELEASE_ACK, tx_payload))
                self.stats["rslTx"] += 1
                print("[mock-bsc] TX RSL RF_CHANNEL_RELEASE_ACK")
            elif msg_type == RSL_PAGING_CMD:
                tx_payload = bytes((entity,))
                conn.sendall(self.encode_frame(IPA_FILTER_RSL, RSL_DATA_CONFIRM, tx_payload))
                self.stats["rslTx"] += 1
                print("[mock-bsc] TX RSL DATA_CONFIRM")
        else:
            print(f"[mock-bsc] RX unknown filter=0x{msg_filter:02X} type=0x{msg_type:02X}")

    def serve(self) -> int:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((self.host, self.port))
        srv.listen(1)
        srv.settimeout(0.5)
        print(f"[mock-bsc] listening on {self.host}:{self.port}")

        conn = None
        rx_buf = bytearray()
        try:
            while not self.stop_evt.is_set():
                if conn is None:
                    try:
                        conn, addr = srv.accept()
                        conn.settimeout(0.5)
                        self.stats["connections"] += 1
                        print(f"[mock-bsc] client connected: {addr[0]}:{addr[1]}")
                    except socket.timeout:
                        continue
                try:
                    data = conn.recv(4096)
                    if not data:
                        print("[mock-bsc] client disconnected")
                        conn.close()
                        conn = None
                        continue
                    rx_buf.extend(data)
                    for msg_filter, msg_type, payload in self.decode_frames(rx_buf):
                        self._on_frame(conn, msg_filter, msg_type, payload)
                except socket.timeout:
                    continue
                except OSError:
                    if conn is not None:
                        conn.close()
                    conn = None
        finally:
            if conn is not None:
                conn.close()
            srv.close()
            self._write_stats()
            print("[mock-bsc] stopped")
            print("[mock-bsc] stats:")
            print(json.dumps(self.stats, indent=2))
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Mock BSC for Abis over IPA (Option D1)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=3002)
    parser.add_argument("--stats-file", default="")
    args = parser.parse_args()

    app = MockBscIpa(args.host, args.port, args.stats_file or None)

    def _stop(*_):
        app.stop_evt.set()

    signal.signal(signal.SIGINT, _stop)
    signal.signal(signal.SIGTERM, _stop)
    return app.serve()


if __name__ == "__main__":
    raise SystemExit(main())
