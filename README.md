# 小新 AI 语音机器人（ESP32-S3）

基于开源项目 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 二次开发的 ESP32-S3 AI 语音终端固件，适配「绿板小新 / 新思版」硬件（`meijing-xinsi`）。

设备通过 Wi-Fi 或板载 ML307 Cat.1 4G 连接后台，完成语音唤醒、录音上传、云端 ASR / LLM / TTS、音频播放、表情显示和 OTA 升级。仓库包含完整可编译源码、板级配置、开发文档和 OTA 接口说明。

## 硬件规格

| 项目 | 参数 |
| --- | --- |
| 主控 | ESP32-S3，双核 240 MHz，Xtensa LX7 |
| Flash | 16 MB |
| PSRAM | 8 MB（Octal） |
| 无线 | Wi-Fi 2.4 GHz + ML307 Cat.1 4G（双网络可切换） |
| 主显示屏 | NV3023，128×128，RGB565，SPI3 |
| 副显示屏 | SSD1306 OLED，128×32，I2C |
| 音频 | ES8311 DAC + ES7210 ADC（双通道，含 AEC 回采） |
| 雷达 | 海凌科 2410D 毫米波雷达，UART2，115200 |
| 按键 | BOOT / 音量+（GPIO0）、音量−（GPIO46） |
| 供电 | USB / 电池（电池检测电路预留，当前固件未启用） |

### 引脚

| 外设 | 引脚 |
| --- | --- |
| LCD | CS=GPIO21，DC=GPIO39，MOSI=GPIO40，SCLK=GPIO41 |
| OLED / 音频 I2C | SDA=GPIO1，SCL=GPIO2 |
| 背光 | GPIO42（PWM，低电平有效） |
| 音频 I2S | MCLK=GPIO38，WS=GPIO13，BCLK=GPIO14，DIN=GPIO12，DOUT=GPIO45 |
| 功放使能 | GPIO47 |
| 雷达 | TX=GPIO15，RX=GPIO16 |
| ML307 4G | 模组 TX→GPIO17，模组 RX→GPIO18 |
| 状态灯 | GPIO48 |

> 摄像头（DVP）和触摸屏（FT5x06）的引脚已在 `main/boards/meijing-xinsi/config.h` 中定义，当前固件未启用。

## 功能

- Wi-Fi / ML307 Cat.1 4G 双网络
- 离线语音唤醒（ESP-SR）
- WebSocket 流式语音交互（ASR + LLM + TTS）
- Opus 音频编解码
- 128×128 LCD 表情 / 时钟显示 + 128×32 OLED 状态显示
- 2410D 毫米波雷达人体存在检测
- 配网页面（AP 热点）与自定义 OTA 地址
- OTA 双分区升级
- 工厂测试模式
- MCP 设备控制

## 目录结构

```text
.
├── CMakeLists.txt
├── sdkconfig                  # 本板验证过的编译配置
├── sdkconfig.defaults
├── partitions/v1/16m.csv      # 16MB Flash 分区表
├── main/
│   ├── boards/meijing-xinsi/  # 本板引脚、显示、音频、雷达初始化
│   ├── display/               # LCD / OLED / 时钟 / GIF 表情
│   ├── audio_codecs/          # ES8311 / ES7210
│   ├── radar/                 # 2410D 雷达驱动
│   ├── protocols/             # WebSocket / MQTT
│   └── ota.cc                 # OTA 逻辑
├── scripts/                   # 语言资源、发布、图片转换工具
└── docs/board/                # 本板开发文档
```

## 快速开始

### 1. 准备环境

- ESP-IDF **5.5.1**（5.4 及以上一般也可）
- Python 3.8+
- Windows / Linux / macOS 均可

```bash
# Linux / macOS / WSL
. $HOME/esp/esp-idf/export.sh

# Windows PowerShell
C:\Espressif\frameworks\esp-idf-v5.5\export.ps1
```

### 2. 编译

```bash
idf.py set-target esp32s3
idf.py build
```

仓库已经附带验证过的 `sdkconfig`，默认板型是 `meijing-xinsi`。如果 `set-target` 覆盖了配置，进入：

```bash
idf.py menuconfig
```

选择：

```text
Xiaozhi Assistant → Board Type → 美好憧憬科技ESP32-S3开发板(新思版)
```

对应配置项 `CONFIG_BOARD_TYPE_MEIJING_XINSI`。

首次编译会从 ESP-IDF 组件仓库下载依赖（约 470 MB），需要联网。

### 3. 烧录

```bash
idf.py -p <串口> flash
```

或使用合并镜像整片烧录：

```bash
python -m esptool --chip esp32s3 -p <串口> -b 460800 write_flash 0x0 build/merged-binary.bin
```

### 4. 配网

1. 设备上电后 60 秒内连不上 Wi-Fi，自动进入配网模式；Wi-Fi 模式下长按「音量−」也可进入。
2. 连接热点 `MJ-XXXX`（无密码）。
3. 浏览器打开 `http://192.168.4.1`。
4. 在「高级选项」里填写自己的 OTA 地址，保存后连接 Wi-Fi，断电重启。

详细步骤见 [配网与 OTA 地址](docs/board/provisioning.md)。

## OTA

- OTA 只上传 `build/xiaozhi.bin`，**不要**上传 `merged-binary.bin`。
- 每次发布前递增 `CMakeLists.txt` 中的 `PROJECT_VER`。
- 后台 OTA 接口需要同时返回 `firmware` 和 `websocket` 字段。

接口格式和排查步骤见 [OTA 接口协议](docs/board/ota.md)。

## 一键发布

```bash
python scripts/release.py meijing-xinsi
```

产物位于 `releases/v<版本号>_meijing-xinsi.zip`，内含 `merged-binary.bin`。

## 二次开发

- 修改引脚：`main/boards/meijing-xinsi/config.h`
- 修改板级初始化：`main/boards/meijing-xinsi/meijing_xinsi_board.cc`
- 修改唤醒词/提示音：`main/assets/` 与 `scripts/gen_lang.py`
- 修改显示/表情：`main/display/` 与 `main/gif/`
- 修改后台地址：配网页面「高级选项」或 `main/Kconfig.projbuild` 中的 `CONFIG_OTA_URL`
- 设备绑定 / 二维码接口地址：`main/application.h` 中的 `base_url`（自建后台时需要一并修改）

## 文档

| 文档 | 内容 |
| --- | --- |
| [硬件说明](docs/board/hardware.md) | 外设、引脚、分区表 |
| [编译与烧录](docs/board/build.md) | 环境、编译、烧录、发布 |
| [配网与 OTA 地址](docs/board/provisioning.md) | 配网页面、高级选项、地址优先级 |
| [OTA 接口协议](docs/board/ota.md) | 后台接口字段、版本比较、排查 |
| [常见问题](docs/board/faq.md) | OTA 不触发、反复升级、配网打不开等 |

## 版本

当前固件版本：`2.0.4`（见 `CMakeLists.txt` 的 `PROJECT_VER`）。

## 许可

本项目基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT License）二次开发，保留上游版权声明。本仓库新增的板级适配和业务代码同样以 MIT License 发布，详见 [LICENSE](LICENSE)。

## 致谢

- [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)：开源小智 AI 固件
- [ESP-IDF](https://github.com/espressif/esp-idf)
- [LVGL](https://lvgl.io/)
- [ESP-SR](https://github.com/espressif/esp-sr)
- [Opus](https://opus-codec.org/)

## 免责声明

本仓库仅供学习、开发和自有硬件使用。开发者需自行确认所在地区关于无线电发射、数据合规、语音数据采集和第三方素材授权的要求。因使用本固件造成的任何损失，作者不承担责任。
