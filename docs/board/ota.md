# OTA 接口协议

设备以 HTTP `POST`（无请求体时 `GET`）方式访问配网页面中配置的 OTA 地址，请求头包含：

| 请求头 | 含义 |
| --- | --- |
| `Device-Id` | 设备 MAC 地址 |
| `Client-Id` | 设备 UUID |
| `User-Agent` | `板型名/固件版本`，例如 `meijing-xinsi/2.0.4` |
| `Accept-Language` | 语言，例如 `zh-CN` |
| `Activation-Version` | 激活协议版本 |

接口需返回 HTTP 200，响应体为 JSON，示例：

```json
{
  "server_time": {
    "timestamp": 1794281143292,
    "timezone_offset": 480
  },
  "firmware": {
    "version": "2.0.4",
    "url": "http://your-server.com:7091/api/firmware/download/123",
    "force": 0
  },
  "websocket": {
    "version": 2,
    "url": "ws://your-server.com:7092/ws/xiaozhi/v1/",
    "token": ""
  }
}
```

字段说明：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `server_time.timestamp` | 否 | 毫秒时间戳，用于校准设备时间 |
| `server_time.timezone_offset` | 否 | 时区偏移，单位分钟 |
| `firmware.version` | 是 | 最新固件版本号，需为 `x.y.z` 形式 |
| `firmware.url` | 是 | 新固件的直接下载地址，必须能被设备访问 |
| `firmware.force` | 否 | 为 `1` 时无条件升级，忽略版本比较 |
| `websocket.version` | 是 | 协议版本，本仓库使用 `2` |
| `websocket.url` | 是 | 对话服务地址，`ws://` 或 `wss://` |
| `websocket.token` | 否 | 鉴权 token，留空表示不鉴权 |
| `mqtt.*` | 否 | 若返回该对象，其键值会被写入设备 NVS |

版本比较规则：

- 只有 `firmware.version` **大于**设备当前版本时才升级；
- 比较基于语义化版本号，逐段比较数字；
- 若你的后台没有可用固件，请返回与设备相同的版本号，或干脆不返回 `firmware` 对象；
- **不要**在未发布时返回 `"version": "1.0.0"` 之类的占位版本，否则设备每次都会尝试升级。

## 上传与发布 OTA

1. 把 `build/xiaozhi.bin` 上传到你的服务器，确保能通过 HTTP 直接下载；
2. 在后台固件表中新增记录：版本号、下载地址、已发布状态；
3. 确认 `GET /api/device/ota` 返回的 `firmware.version` 是新版本、`firmware.url` 是上面的下载地址；
4. 设备断电重启，开机流程会自动检查版本。

**未发布状态的固件不会被 OTA 接口返回**，这是最容易踩的坑。

## 版本一致性自查

OTA 出问题绝大多数不是设备端，而是「后台登记版本」和「实际文件」不一致。判断方法：

1. 请求 OTA 接口，记录返回的 `firmware.version` 和 `firmware.url`；
2. 直接下载 `firmware.url` 指向的文件；
3. 读取该文件内嵌版本，和本地构建产物比对：

   ```bash
   python -c "d=open('xiaozhi.bin','rb').read(256); i=d.find(b'2.0.'); print(d[i:i+5].decode())"
   ```

4. 同时比对 MD5：

   ```bash
   md5sum xiaozhi.bin
   ```

只有「接口版本号 == 文件内嵌版本号 == 你编译的版本号」三者一致，OTA 才是可靠的。

**典型故障**：接口登记 `2.0.4`，但下载地址里的文件实际是 `2.0.1`。设备升级后运行 2.0.1，下次开机又发现 2.0.4 更新，于是无限重复升级；如果那个旧包里还带着旧的绑定流程，还会表现为升级后卡在「连接中…」。

## 本地联调：OTA mock 服务

仓库提供 `tools/ota_mock_server.py`，只依赖 Python 标准库：

```bash
python3 tools/ota_mock_server.py \
  --firmware build/xiaozhi.bin \
  --version 2.0.4 \
  --port 8000 \
  --ws ws://192.168.1.10:8000/ws/xiaozhi/v1/
```

配网页面填：

```text
http://<电脑局域网 IP>:8000/api/device/ota
```

设备需要和电脑在同一局域网，防火墙放行端口。

这个服务只处理 OTA 版本检查和固件下载；WebSocket AI 服务需要另外提供。
