# 已验证环境与构建记录

## 1. 最近一次成功构建

| 项目 | 值 |
| --- | --- |
| 目标芯片 | ESP32-S3 |
| ESP-IDF | v5.5.1 |
| 板型 | `meijing-xinsi` |
| 固件版本 | 2.0.4 |
| 构建主机 | Windows |
| 应用镜像 | `build/mj-xinsi-release/xiaozhi.bin`，约 4.7 MB |
| 版本标记 | `project_description.json` 和镜像内均为 `2.0.4` |

## 2. 2026-11-11 静态核对

在整理发布仓库时做了以下核对：

- 按上次成功构建的 `compile_commands.json` 检查，66 个参与编译的源文件全部存在。
- 补齐了 `main/main.cc`、`main/boards/common/axp2101.cc`、`main/iot/things/battery.cc`。
- 补齐了 `main/radar/radar.h`、`main/https_test.h`。
- 从 `build/huanxing.p3.S` 还原了 `main/assets/common/huanxing.p3`（826 字节）。
- 补回 `partitions/v1/16m.csv`、`CMakeLists.txt`、`main/boards/meijing-xinsi/config.json`。
- 确认 `sdkconfig` 中板型为 `CONFIG_BOARD_TYPE_MEIJING_XINSI=y`，Flash 16MB。

## 3. 尚未验证

- 没有在 macOS 上安装 ESP-IDF 做一次干净的 `idf.py build`。
- 买家资料包里的 `merged-binary.bin` 是按 2.0.4 构建产物合并的，尚未在实机上做整片烧录验证。
- 摄像头、触摸、电池检测在当前固件中未启用，未做功能验证。

## 4. 复现构建

```bash
# 1. 安装并激活 ESP-IDF 5.5.1
. $HOME/esp/esp-idf/export.sh

# 2. 进入仓库
cd leyy-xxesp

# 3. 编译
idf.py set-target esp32s3
idf.py build
```

如果 `set-target` 覆盖了配置，执行 `idf.py menuconfig` 重新选择：

```text
Xiaozhi Assistant → Board Type → 美好憧憬科技ESP32-S3开发板(新思版)
```

## 5. 建议

正式发给买家前，建议在一台干净的电脑上完整走一遍：

1. 安装 ESP-IDF 5.5.1。
2. `idf.py set-target esp32s3 && idf.py build`。
3. 烧录 `build/merged-binary.bin`。
4. 配网、连接后台、完成一次语音对话和一次 OTA。

把结果补充到本文件，后续买家就能看到“已验证”的环境和步骤。
