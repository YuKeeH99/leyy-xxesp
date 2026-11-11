#!/usr/bin/env python3
"""小智 ESP32 OTA 本地联调服务（仅使用 Python 标准库）。

用法：
    python3 tools/ota_mock_server.py --firmware build/xiaozhi.bin --version 2.0.4

然后在配网页面「高级选项」里填写：
    http://<电脑局域网 IP>:8000/api/device/ota

注意：这个服务只负责 OTA 版本检查和固件下载，不包含 WebSocket AI 服务。
"""

import argparse
import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class OtaHandler(BaseHTTPRequestHandler):
    firmware_path = ""
    firmware_version = "2.0.4"
    websocket_url = "ws://127.0.0.1:8000/ws/xiaozhi/v1/"

    def _ota_body(self):
        host = self.headers.get("Host") or "127.0.0.1:%d" % self.server.server_port
        filename = os.path.basename(self.firmware_path)
        return {
            "server_time": {
                "timestamp": int(time.time() * 1000),
                "timezone_offset": 480,
            },
            "firmware": {
                "version": self.firmware_version,
                "url": "http://%s/firmware/%s" % (host, filename),
                "force": 0,
            },
            "websocket": {
                "version": 2,
                "url": self.websocket_url,
                "token": "",
            },
        }

    def _send_json(self, body):
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _send_firmware(self):
        if not os.path.isfile(self.firmware_path):
            self.send_error(404, "firmware not found")
            return
        size = os.path.getsize(self.firmware_path)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.end_headers()
        with open(self.firmware_path, "rb") as f:
            while True:
                chunk = f.read(64 * 1024)
                if not chunk:
                    break
                self.wfile.write(chunk)

    def _log_request(self):
        print(
            "[OTA] %s %s Device-Id=%s Client-Id=%s UA=%s"
            % (
                self.command,
                self.path,
                self.headers.get("Device-Id", "-"),
                self.headers.get("Client-Id", "-"),
                self.headers.get("User-Agent", "-"),
            )
        )

    def do_GET(self):
        self._log_request()
        if self.path.startswith("/api/device/ota") or self.path.startswith("/xiaozhi/ota"):
            self._send_json(self._ota_body())
        elif self.path.startswith("/firmware/"):
            self._send_firmware()
        else:
            self.send_error(404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        if length:
            self.rfile.read(length)
        self._log_request()
        if self.path.startswith("/api/device/ota") or self.path.startswith("/xiaozhi/ota"):
            self._send_json(self._ota_body())
        else:
            self.send_error(404)

    def log_message(self, fmt, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description="小智 ESP32 OTA 本地联调服务")
    parser.add_argument("--firmware", default="build/xiaozhi.bin", help="OTA 应用镜像路径")
    parser.add_argument("--version", default="2.0.4", help="返回给设备的固件版本号")
    parser.add_argument("--port", type=int, default=8000, help="监听端口")
    parser.add_argument("--host", default="0.0.0.0", help="监听地址")
    parser.add_argument("--ws", default=None, help="返回给设备的 WebSocket 地址")
    args = parser.parse_args()

    if not os.path.isfile(args.firmware):
        print("固件不存在：%s" % args.firmware)
        print("请先执行 idf.py build，或把 --firmware 指向 build/xiaozhi.bin")
        return 1

    OtaHandler.firmware_path = os.path.abspath(args.firmware)
    OtaHandler.firmware_version = args.version
    OtaHandler.websocket_url = args.ws or "ws://127.0.0.1:%d/ws/xiaozhi/v1/" % args.port

    server = ThreadingHTTPServer((args.host, args.port), OtaHandler)
    print("OTA 服务已启动：")
    print("  OTA 地址：http://<电脑局域网 IP>:%d/api/device/ota" % args.port)
    print("  固件版本：%s" % args.version)
    print("  固件文件：%s" % OtaHandler.firmware_path)
    print("  WebSocket：%s" % OtaHandler.websocket_url)
    print("按 Ctrl+C 停止。")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
