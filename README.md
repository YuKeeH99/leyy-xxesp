# 小新 · AI 桌面客服搭子（ESP32-S3）

一个能听、能说、会看表情的桌面 AI 语音机器人固件。

主控 ESP32-S3，支持 Wi-Fi 和 ML307 Cat.1 4G 双网络，板载 1.28 寸 LCD、OLED、ES8311 / ES7210 音频和 2410D 毫米波雷达。设备通过 Wi-Fi 或 4G 连接后台，完成语音唤醒、录音上传、云端 ASR / LLM / TTS、音频播放、表情显示和 OTA 升级。

> **本仓库定位**：完整可编译的 ESP32-S3 固件 + 板级适配 + 开发文档。
> 买到硬件的用户可以直接烧录预编译固件；想二次开发的用户可以在本仓库改显示、语音、联网、雷达和 OTA。

> 在线路书：<https://yukeeh99.github.io/leyy-xxesp/>

> 卖家已确认：本仓库对应发货版本 2.0.4，固件、源码、板级配置、文档和校验值已核对。

## 它能做什么

| 能力 | 现在就能用 | 你可以自己扩展 |
| --- | --- | --- |
| **语音对话** | 唤醒词唤醒，录音上传，云端 ASR + LLM + TTS，Opus 播放 | 换唤醒词、换音色、接自己的 ASR / LLM / TTS |
| **双网络** | Wi-Fi 和 ML307 Cat.1 4G 自动切换 | 改 APN、改网络优先级、加 MQTT |
| **显示** | 128×128 LCD 表情 / 时钟，128×32 OLED 状态 | 改 UI、加页面、换字体、换表情 |
| **雷达** | 2410D 人体存在检测，影响休眠和问候 | 改检测距离、加自动亮屏 / 休眠逻辑 |
| **配网** | `MJ-XXXX` 热点 + `http://192.168.4.1` 页面 | 改页面、加自定义字段、改 OTA 地址 |
| **OTA** | 双分区 OTA，后台返回版本和 WebSocket 地址 | 接自己的 OTA 服务、加灰度 / 强制升级 |
| **MCP** | 设备端 MCP 工具，可通过后台控制音量、灯、GPIO | 加自己的 MCP 工具 |
| **工厂测试** | 按键进入工厂测试模式 | 加产测项、改测试流程 |

## 硬件规格

| 项目 | 参数 |
| --- | --- |
| 主控 | ESP32-S3，双核 240 MHz，Xtensa LX7 |
| Flash / PSRAM | 16 MB Flash / 8 MB Octal PSRAM |
| 无线 | Wi-Fi 2.4 GHz + ML307 Cat.1 4G |
| 主屏 | NV3023 128×128 LCD（SPI3） |
| 副屏 | SSD1306 OLED 128×32（I2C） |
| 音频 | ES8311 DAC + ES7210 ADC（含 AEC 回采） |
| 雷达 | 海凌科 2410D（UART2，115200） |
| 按键 | BOOT / 音量+（GPIO0）、音量−（GPIO46） |

完整引脚表见 [硬件说明](docs/board/hardware.md)。

## 3 分钟上手（只想使用）

1. 用 USB 连接设备，确认串口。
2. 安装 esptool：

   ```bash
   pip install esptool
   ```

3. 烧录整片固件：

   ```bash
   python -m esptool --chip esp32s3 -p COM3 -b 460800 write_flash 0x0 merged-binary.bin
   ```

4. 设备重启后连接热点 `MJ-XXXX`，打开 `http://192.168.4.1`。
5. 在「高级选项」填写 OTA 地址，回到「Wi-Fi」连接路由器，断电重启。

详细步骤见 [快速上手](docs/board/build.md) 和 [配网与 OTA 地址](docs/board/provisioning.md)。

## 自己编译（想开发）

```bash
# 1. 安装并激活 ESP-IDF 5.5.1
. $HOME/esp/esp-idf/export.sh

# 2. 编译
idf.py set-target esp32s3
idf.py build
```

仓库已附带验证过的 `sdkconfig`，默认板型 `meijing-xinsi`。如果 `set-target` 覆盖了配置，进入 `idf.py menuconfig` 选择：

```text
Xiaozhi Assistant → Board Type → 美好憧憬科技ESP32-S3开发板(新思版)
```

首次编译会从 ESP-IDF 组件仓库下载约 470 MB 依赖，需要联网。

烧录：

```bash
idf.py -p COM3 flash
```

## 卡住了先看这里

| 现象 | 检查方法 |
| --- | --- |
| 找不到串口 | 换数据线、换 USB 口，安装 CH340 / CP210x 驱动 |
| 烧录失败 | 降低波特率到 115200，确认串口没有被其他程序占用 |
| 配网页面打不开 | 确认连的是 `MJ-XXXX`，手动输入 `http://192.168.4.1` |
| OTA 不触发 | 版本号是否递增、后台是否发布、下载地址是否可达 |
| 升级后还是旧功能 | 是否上传了 `xiaozhi.bin`，是否写对 OTA 分区 |
| 设备反复升级 | 后台登记版本高于实际文件版本，核对 MD5 |
| 设备反复重启 | 串口看是否有 task watchdog，检查显示 / 内存 |

更多问题见 [常见问题](docs/board/faq.md)。

## 仓库里有什么

```text
.
├── CMakeLists.txt
├── sdkconfig                  # 本板验证过的编译配置
├── partitions/v1/16m.csv      # 16MB Flash 分区表
├── main/
│   ├── boards/meijing-xinsi/  # 引脚、显示、音频、雷达初始化
│   ├── display/               # LCD / OLED / 时钟 / GIF 表情
│   ├── audio_codecs/          # ES8311 / ES7210
│   ├── radar/                 # 2410D 雷达驱动
│   ├── protocols/             # WebSocket / MQTT
│   └── ota.cc                 # OTA 逻辑
├── scripts/                   # 语言资源、发布、图片转换工具
└── docs/board/                # 本板开发文档
```

**不在仓库里的**：`build/`、`managed_components/`（首次编译自动下载）、真实 Wi-Fi 密码、设备 NVS 备份、量产工具。

## 二次开发：改什么从哪里下手

| 想改什么 | 文件位置 |
| --- | --- |
| 引脚、外设 | `main/boards/meijing-xinsi/config.h` |
| 板级初始化 | `main/boards/meijing-xinsi/meijing_xinsi_board.cc` |
| 显示、表情、时钟 | `main/display/`、`main/gif/` |
| 音频编解码 | `main/audio_codecs/` |
| 雷达 | `main/radar/` |
| OTA | `main/ota.cc` |
| 配网页面 | `managed_components/78__esp-wifi-connect/` |
| 后台地址 | 配网「高级选项」或 `main/Kconfig.projbuild` |
| 设备绑定 / 二维码接口 | `main/application.h` 的 `base_url` |

## 文档

| 文档 | 内容 |
| --- | --- |
| [硬件说明](docs/board/hardware.md) | 外设、引脚、分区表 |
| [编译与烧录](docs/board/build.md) | 环境、编译、产物、烧录、发布 |
| [配网与 OTA 地址](docs/board/provisioning.md) | 配网页面、高级选项、地址优先级 |
| [OTA 接口协议](docs/board/ota.md) | 后台接口字段、版本比较、排查 |
| [常见问题](docs/board/faq.md) | OTA 不触发、反复升级、配网打不开等 |
| [构建环境](docs/board/verified-build.md) | 构建环境、版本、发货核对项 |

## 版本

当前固件版本：`2.0.4`（见 `CMakeLists.txt` 的 `PROJECT_VER`）。

## 许可与合规

本项目基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT License）二次开发，保留上游版权声明。本仓库新增的板级适配和业务代码同样以 MIT License 发布，详见 [LICENSE](LICENSE)。

- 第三方组件（ESP-IDF、ESP-SR、LVGL、Opus 等）遵循各自许可，详见 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。商用前请逐项确认。
- ESP32-S3、ESP-IDF、ESP-SR 等名称和商标归 Espressif 及相关权利人所有；本项目与 Espressif 无隶属关系，也不代表其官方背书。
- 产品外观、结构、图片、GIF、字体、唤醒词、提示音等素材由卖家确认已获授权；买家二次开发或再分发时需自行确认授权。
- 设备涉及无线电发射，销售和使用需遵守当地无线电管理、入网认证和数据合规要求。

## 致谢

- [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)：开源小智 AI 固件
- [ESP-IDF](https://github.com/espressif/esp-idf)
- [LVGL](https://lvgl.io/)
- [ESP-SR](https://github.com/espressif/esp-sr)
- [Opus](https://opus-codec.org/)

## 免责声明

本仓库按“现状”提供，不构成对二次开发、改装或特定用途的保证，也不构成法律意见。因错误烧录、改装、第三方授权或无线电合规问题造成的损失，作者不承担责任。
