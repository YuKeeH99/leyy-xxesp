# 第三方组件与许可（ESP32-S3 版）

本仓库基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT License）二次开发。仓库本身不包含 `managed_components/`，首次编译时由 ESP-IDF Component Manager 按 `dependencies.lock` 下载，许可证随组件提供。

## 主要组件

| 组件 | 许可证 | 说明 |
| --- | --- | --- |
| [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) | MIT | 上游固件 |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 | 开发框架 |
| [LVGL](https://lvgl.io/) | MIT | 图形库 |
| [ESP-SR](https://github.com/espressif/esp-sr) | Espressif MIT | 唤醒词 / 语音处理 |
| [78/esp-ml307](https://components.espressif.com/) | Apache-2.0 | ML307 Cat.1 驱动 |
| 78/esp_lcd_nv3023 | Apache-2.0 | NV3023 LCD 驱动 |
| 78/esp-wifi-connect | 随组件提供 | Wi-Fi 配网组件 |
| 78/xiaozhi-fonts | 随组件提供 | 字体资源 |
| espressif/esp_codec_dev | Apache-2.0 | 音频 Codec |
| espressif/esp_lcd_* | Apache-2.0 | LCD / 触摸驱动 |
| espressif/esp32-camera | Apache-2.0 | 摄像头驱动 |
| espressif/esp_lvgl_port | Apache-2.0 | LVGL 适配 |
| espressif/esp-dsp | Apache-2.0 | DSP |
| espressif/button、knob、led_strip | Apache-2.0 | 外设组件 |
| tny-robotics/sh1106-esp-idf | MIT | OLED 驱动 |
| txp666/otto-emoji-gif-component | MIT | GIF 表情组件 |
| wvirgil123/esp_jpeg_simd、sscma_client | Apache-2.0 | JPEG / 摄像头组件 |
| waveshare/esp_lcd_* | Apache-2.0 | LCD 驱动 |
| Opus | BSD-3-Clause | 音频编解码 |

完整组件版本见 `dependencies.lock`；完整许可证见各组件目录下的 `LICENSE` / `license.txt`。

## 说明

- 本文件是摘要，不替代各组件的原始许可证。
- 如果你把固件用于商业产品，请保留上游 MIT 版权声明，并遵守各组件许可证。
- 字体、表情、提示音等资源如涉及第三方版权，请自行确认授权。
