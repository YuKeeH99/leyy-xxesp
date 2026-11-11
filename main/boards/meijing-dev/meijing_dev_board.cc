//#include "wifi_board.h"
#include "dual_network_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "display/clock_display.h"  // 添加时钟显示头文件
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "iot/thing_manager.h"
#include "esp32_camera.h"
#include "display/custom_gif_emoji_display.h"  // 添加新的头文件
//#include "display/oled_display.h"
#include "settings.h"  // 添加 Settings 类的头文件
#include "assets/lang_config.h"  // 添加 Lang 命名空间的头文件
#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>  // 添加 FreeRTOS 头文件
#include <freertos/task.h>  // 添加 task 头文件用于 vTaskDelay
#include <lvgl.h>
#include <esp_lcd_nv3023.h>
// 添加SSD1306 OLED驱动头文件
#include <esp_lcd_panel_ssd1306.h>

#include "power_manager.h"
#include "power_save_timer.h"  // 添加这行来包含PowerSaveTimer头文件
#include "afsk_demod.h"



#define TAG "MeijingDevBoard"

// 定义OLED屏幕的I2C引脚
#define OLED_I2C_SDA_PIN GPIO_NUM_1  // 使用IO1作为SDA
#define OLED_I2C_SCL_PIN GPIO_NUM_2  // 使用IO2作为SCL
#define OLED_I2C_ADDR    0x3C        // SSD1306的I2C地址，通常是0x3C或0x3D
#define OLED_WIDTH       128
#define OLED_HEIGHT      32

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

LV_FONT_DECLARE(font_puhui_14_1);
LV_FONT_DECLARE(font_awesome_14_1);

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x01, 0x03);
        WriteReg(0x03, 0xf8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

static const nv3023_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xff, (uint8_t[]){0xa5}, 1, 0},
    {0x3E, (uint8_t[]){0x09}, 1, 0},
    {0x3A, (uint8_t[]){0x65}, 1, 0},
    {0x82, (uint8_t[]){0x00}, 1, 0},
    {0x98, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0x0f}, 1, 0},
    {0x64, (uint8_t[]){0x0f}, 1, 0},
    {0xB4, (uint8_t[]){0x34}, 1, 0},
    {0xB5, (uint8_t[]){0x30}, 1, 0},
    {0x83, (uint8_t[]){0x03}, 1, 0},
    {0x86, (uint8_t[]){0x04}, 1, 0},
    {0x87, (uint8_t[]){0x16}, 1, 0},
    {0x88, (uint8_t[]){0x0A}, 1, 0},
    {0x89, (uint8_t[]){0x27}, 1, 0},
    {0x93, (uint8_t[]){0x63}, 1, 0},
    {0x96, (uint8_t[]){0x81}, 1, 0},
    {0xC3, (uint8_t[]){0x10}, 1, 0},
    {0xE6, (uint8_t[]){0x00}, 1, 0},
    {0x99, (uint8_t[]){0x01}, 1, 0},
    {0x70, (uint8_t[]){0x09}, 1, 0},
    {0x71, (uint8_t[]){0x1D}, 1, 0},
    {0x72, (uint8_t[]){0x14}, 1, 0},
    {0x73, (uint8_t[]){0x0a}, 1, 0},
    {0x74, (uint8_t[]){0x11}, 1, 0},
    {0x75, (uint8_t[]){0x16}, 1, 0},
    {0x76, (uint8_t[]){0x38}, 1, 0},
    {0x77, (uint8_t[]){0x0B}, 1, 0},
    {0x78, (uint8_t[]){0x08}, 1, 0},
    {0x79, (uint8_t[]){0x3E}, 1, 0},
    {0x7a, (uint8_t[]){0x07}, 1, 0},
    {0x7b, (uint8_t[]){0x0D}, 1, 0},
    {0x7c, (uint8_t[]){0x16}, 1, 0},
    {0x7d, (uint8_t[]){0x0F}, 1, 0},
    {0x7e, (uint8_t[]){0x14}, 1, 0},
    {0x7f, (uint8_t[]){0x05}, 1, 0},
    {0xa0, (uint8_t[]){0x04}, 1, 0},
    {0xa1, (uint8_t[]){0x28}, 1, 0},
    {0xa2, (uint8_t[]){0x0c}, 1, 0},
    {0xa3, (uint8_t[]){0x11}, 1, 0},
    {0xa4, (uint8_t[]){0x0b}, 1, 0},
    {0xa5, (uint8_t[]){0x23}, 1, 0},
    {0xa6, (uint8_t[]){0x45}, 1, 0},
    {0xa7, (uint8_t[]){0x07}, 1, 0},
    {0xa8, (uint8_t[]){0x0a}, 1, 0},
    {0xa9, (uint8_t[]){0x3b}, 1, 0},
    {0xaa, (uint8_t[]){0x0d}, 1, 0},
    {0xab, (uint8_t[]){0x18}, 1, 0},
    {0xac, (uint8_t[]){0x14}, 1, 0},
    {0xad, (uint8_t[]){0x0F}, 1, 0},
    {0xae, (uint8_t[]){0x19}, 1, 0},
    {0xaf, (uint8_t[]){0x08}, 1, 0},
    {0xff, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 10}
};

class CustomAudioCodec : public BoxAudioCodec {
private:
    Pca9557* pca9557_;

public:
    CustomAudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557* pca9557) 
        : BoxAudioCodec(i2c_bus, 
                       AUDIO_INPUT_SAMPLE_RATE, 
                       AUDIO_OUTPUT_SAMPLE_RATE,
                       AUDIO_I2S_GPIO_MCLK, 
                       AUDIO_I2S_GPIO_BCLK, 
                       AUDIO_I2S_GPIO_WS, 
                       AUDIO_I2S_GPIO_DOUT, 
                       AUDIO_I2S_GPIO_DIN,
                       GPIO_NUM_NC, 
                       AUDIO_CODEC_ES8311_ADDR, 
                       AUDIO_CODEC_ES7210_ADDR, 
                       AUDIO_INPUT_REFERENCE),
          pca9557_(pca9557) {
    }

    virtual void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        if (enable) {
            pca9557_->SetOutputState(1, 1);
        } else {
            pca9557_->SetOutputState(1, 0);
        }
    }
};

class MeijingDevBoard : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    LcdDisplay* lcd_display_;      // 重命名为lcd_display_
    Display* oled_display_;    // 添加OLED显示屏
    //DualDisplay* dual_display_;    // 添加双显示屏
    Pca9557* pca9557_;
    Esp32Camera* camera_;

    PowerManager* power_manager_;
    PowerSaveTimer* power_save_timer_;  // 添加这行

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,  // 使用I2C端口0
            .sda_io_num = GPIO_NUM_1,   // 统一使用GPIO1作为SDA
            .scl_io_num = GPIO_NUM_2,   // 统一使用GPIO2作为SCL
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // Initialize PCA9557
        //pca9557_ = new Pca9557(i2c_bus_, 0x19);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_40;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_41;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            if (power_save_timer_) {
                power_save_timer_->WakeUp();  // 添加这行
            }
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
        // 添加长按 boot 按钮进入/退出工厂测试模式
        volume_up_button_.OnLongPress([this]() {
            if (power_save_timer_) {
                power_save_timer_->WakeUp();  // 添加这行
            }
            auto& app = Application::GetInstance();
            if (app.IsInFactoryTestingMode()) {
                // 如果已经在测试模式，则退出
                app.ExitFactoryTestingMode();
            } else {
                // 允许在idle状态、激活状态或保持连接状态时进入工厂测试模式
                if (app.GetDeviceState() == kDeviceStateIdle || 
                    app.GetDeviceState() == kDeviceStateActivating ||
                    app.GetDeviceState() == kDeviceStateKeepConnected) {
                    ESP_LOGI(TAG, "设备处于允许状态，允许进入工厂测试模式，当前状态: %d", app.GetDeviceState());
                    app.EnterFactoryTestingMode();
                } else {
                    ESP_LOGI(TAG, "设备未处于允许的状态，无法进入工厂测试模式，当前状态: %d", app.GetDeviceState());
                }
            }
        });
        /*volume_up_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            ESP_LOGI(TAG, "Volume up OnDoubleClick");

            //if (app.GetDeviceState() == kDeviceStateStarting || app.GetDeviceState() == kDeviceStateWifiConfiguring) {
                SwitchNetworkType();
           // }
        });*/
        // 音量增加按键
        volume_up_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            
            // 添加调试日志
            ESP_LOGI(TAG, "Volume up button clicked");
            ESP_LOGI(TAG, "Factory test mode: %s", app.IsInFactoryTestingMode() ? "YES" : "NO");
            
            // 如果在工厂测试模式下，用音量键切换测试
            if (app.IsInFactoryTestingMode()) {
                ESP_LOGI(TAG, "Calling HandleVolumeUpPress");
                app.HandleVolumeUpPress();
                return;
            }
            
            // 正常模式下调节音量
            auto* codec = GetAudioCodec();
            if (codec) {
                int current_volume = codec->output_volume();
                int new_volume = current_volume + 10;
                if (new_volume > 100) {
                    new_volume = 100;
                }
                codec->SetOutputVolume(new_volume);
                ESP_LOGI(TAG, "Volume up: %d -> %d", current_volume, new_volume);
            }
        });

    // 音量减小按键，同时具有WiFi配网功能
    volume_down_button_.OnClick([this]() {
        
        
        auto* codec = GetAudioCodec();
        if (codec) {
            int current_volume = codec->output_volume();
            int new_volume = current_volume - 10;
            // 限制音量范围在0-100之间
            if (new_volume < 0) {
                new_volume = 0;
            }
            codec->SetOutputVolume(new_volume);
            ESP_LOGI(TAG, "Volume down: %d -> %d", current_volume, new_volume);
        }
    });

    // 长按进入WiFi配网模式
    volume_down_button_.OnLongPress([this]() {
        if (power_save_timer_) {
            power_save_timer_->WakeUp();  // 添加这行
        }
        //if (!WifiStation::GetInstance().IsConnected()) 
       if (GetNetworkType() == NetworkType::WIFI)
        {
            ResetWifiConfiguration();
        }
        else{
            SwitchNetworkType();
        }


        
    });
/*
#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif
*/
    }
    //lcd 128x128
    void InitializeNv3023Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr; 
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = NV3023_PANEL_IO_SPI_CONFIG(LCD_DISPLAY_CS, GPIO_NUM_39, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        nv3023_vendor_config_t vendor_config = {  // Uncomment these lines if use custom initialization commands
            .init_cmds = lcd_init_cmds,
            .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(nv3023_lcd_init_cmd_t),
        };
        panel_config.reset_gpio_num = GPIO_NUM_NC;//DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = &vendor_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_nv3023(panel_io, &panel_config, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, false));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
     /*  
        lcd_display_ = new SpiLcdDisplay(panel_io, panel, LCD_DISPLAY_WIDTH, LCD_DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY, 
        {
            .text_font = &font_puhui_16_4,
            .icon_font = &font_awesome_16_4,
            .emoji_font = font_emoji_32_init(),
        });
*/
       lcd_display_ = new ClockDisplay(panel_io, panel,
                                    LCD_DISPLAY_WIDTH, LCD_DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_16_4,
                                        .icon_font = &font_awesome_16_4,
        #if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
        #else
                                        .emoji_font = font_emoji_64_init(),
        #endif
                                    });

    }

    // lcd 240*240
    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_NC;
        io_config.dc_gpio_num = GPIO_NUM_39;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        pca9557_->SetOutputState(0, 0);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        // 替换显示类实例化
        /*
        lcd_display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = font_emoji_64_init(),
#endif
                                    });

      */
     lcd_display_ = new ClockDisplay(panel_io, panel,
                            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                            {
                                .text_font = &font_puhui_20_4,
                                .icon_font = &font_awesome_20_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                .emoji_font = font_emoji_32_init(),
#else
                                .emoji_font = font_emoji_64_init(),
#endif
                            });
      

    }

	void InitializeTouch()
    {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
            .int_gpio_num = GPIO_NUM_NC, 
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 1,
                .mirror_x = 1,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        tp_io_config.scl_speed_hz = 400000;

        esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
        esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp);
        assert(tp);

        /* Add touch input (for selected screen) */
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(), 
            .handle = tp,
        };

        lvgl_port_add_touch(&touch_cfg);
    }

    void InitializeCamera() {
        // Open camera power
        pca9557_->SetOutputState(2, 0);

        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_2;  // LEDC通道选择  用于生成XCLK时钟 但是S3不用
        config.ledc_timer = LEDC_TIMER_2; // LEDC timer选择  用于生成XCLK时钟 但是S3不用
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = -1;   // 这里写-1 表示使用已经初始化的I2C接口
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 1;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

        camera_ = new Esp32Camera(config);
    }
    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = OLED_I2C_SDA_PIN,
            .scl_io_num = OLED_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // 使用同一个I2C总线
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = OLED_I2C_ADDR,  // 0x3C
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };
        
        // 添加错误处理，避免ESP_ERROR_CHECK导致的崩溃
        esp_err_t ret = esp_lcd_new_panel_io_i2c_v2(i2c_bus_, &io_config, &panel_io_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C panel IO: %s", esp_err_to_name(ret));
            oled_display_ = new NoDisplay();
            return;
        }

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(OLED_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ret = esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_);
#else
        ret = esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_);
#endif
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create SSD1306 panel: %s", esp_err_to_name(ret));
            oled_display_ = new NoDisplay();
            return;
        }
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ret = esp_lcd_panel_reset(panel_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset panel: %s", esp_err_to_name(ret));
            oled_display_ = new NoDisplay();
            return;
        }
        
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            oled_display_ = new NoDisplay();
            return;
        }
        
        ret = esp_lcd_panel_invert_color(panel_, false);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to invert color: %s", esp_err_to_name(ret));
        }

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ret = esp_lcd_panel_disp_on_off(panel_, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
            oled_display_ = new NoDisplay();
            return;
        }

        // 替换显示类实例化
        oled_display_ = new CustomOledGifEmojiDisplay(panel_io_, panel_, OLED_WIDTH, OLED_HEIGHT, false, false,
            {&font_puhui_14_1, &font_awesome_14_1});
        //oled_display_ = new OledDisplay(panel_io_, panel_, OLED_WIDTH, OLED_HEIGHT, true, true,
        //    {&font_puhui_14_1, &font_awesome_14_1});
    }
    
    void InitializeOledDisplay()
    {
        //InitializeDisplayI2c();
        InitializeSsd1306Display();
    }

public:
    MeijingDevBoard() : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, 4096),boot_button_(BOOT_BUTTON_GPIO),
    volume_up_button_(VOLUME_UP_BUTTON_GPIO),
    volume_down_button_(VOLUME_DOWN_BUTTON_GPIO){  
        InitializeI2c();
        InitializeSpi();
        
        // 先初始化LCD显示屏，再初始化OLED显示屏
        //InitializeSt7789Display();
        InitializeNv3023Display();

        //延迟1s，避免没插好
        vTaskDelay(pdMS_TO_TICKS(1000));

        InitializeOledDisplay();
        
        //InitializeTouch();
        InitializeButtons();
        //InitializeCamera();

        InitializePowerSaveTimer();

#if CONFIG_IOT_PROTOCOL_XIAOZHI
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
#endif
        GetBacklight()->RestoreBrightness();
    }
    /*DualNetwork 是关键词，用于切换网络时候匹配 */
    virtual std::string GetBoardType() override {
        std::string current_type = (GetNetworkType() == NetworkType::WIFI) ? "WiFi" : "ML307";
        return "MeijingDev-DualNetwork(" + current_type + ")";
    }

    virtual AudioCodec* GetAudioCodec() override {
       /* static CustomAudioCodec audio_codec(
            i2c_bus_, 
            pca9557_);
        return &audio_codec;*/

        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_47, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);

        // 启用音频输出
        //audio_codec.EnableOutput(true);
        // 设置初始音量
        //audio_codec.SetOutputVolume(70);
        return &audio_codec;
    }


    virtual Display* GetDisplay() override {
        return lcd_display_;  // 返回双显示屏对象
        //return oled_display_;  // 返回OLED显示屏对象
    }

    virtual Display* GetOledDisplay() override {
         return oled_display_;  // 返回OLED显示屏对象

    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override {
        //return camera_;
        return nullptr;
    }

    // 如果需要，覆盖SetPowerSaveMode方法
    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled && power_save_timer_) {
            //ESP_LOGI(TAG, "Wake up from sleep mode");
            power_save_timer_->WakeUp();
        }
        DualNetworkBoard::SetPowerSaveMode(enabled);
    }
    
    // 添加 ResetWifiConfiguration 方法
    void ResetWifiConfiguration() {
        // 如果当前是WiFi模式，则重置WiFi配置
        if (GetNetworkType() == NetworkType::WIFI) {
            // 设置标志并重启设备进入网络配置模式
            Settings settings("wifi", true);
            settings.SetInt("force_ap", 1);
            
            GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
            vTaskDelay(pdMS_TO_TICKS(1000));
            // 重启设备
            esp_restart();
        }
    }
/*
    // 添加 GetBatteryLevel 方法
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        level = power_manager_->GetBatteryLevel();
        return true;
    }
        */

    // 添加PowerSaveTimer初始化方法
    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("sleepy");
            
            auto& app = Application::GetInstance();
            if (app.CanEnterSleepMode()) {
                app.SetDeviceState(kDeviceStateSleeping);
                app.SwitchDisplayMode(true);
            }

            GetBacklight()->SetBrightness(10);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            ESP_LOGI(TAG, "Exiting sleep mode");
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("neutral");
            GetBacklight()->RestoreBrightness();
            
            auto& app = Application::GetInstance();
            app.SwitchDisplayMode(false);

            // 如果是ML307网络，给网络恢复一些时间
            if (GetNetworkType() == NetworkType::ML307) {
                ESP_LOGI(TAG, "ML307 network detected, allowing time for network recovery");
                // 延迟一段时间让ML307模块重新建立网络连接
                //在application.cc 的kDeviceStateConnecting 有进行唤醒
                //vTaskDelay(pdMS_TO_TICKS(2000)); // 等待2秒
            }
        });
        power_save_timer_->SetEnabled(true);
    }
};

DECLARE_BOARD(MeijingDevBoard);





