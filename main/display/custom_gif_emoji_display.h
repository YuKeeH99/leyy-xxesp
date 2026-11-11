#ifndef CUSTOM_GIF_EMOJI_DISPLAY_H
#define CUSTOM_GIF_EMOJI_DISPLAY_H

#include "lcd_display.h"
#include "oled_display.h"
#include "libs/gif/lv_gif.h"

// 声明 GIF 资源（复用 electron-bot 的资源）
LV_IMAGE_DECLARE(staticstate);
LV_IMAGE_DECLARE(happy);
LV_IMAGE_DECLARE(sad);
LV_IMAGE_DECLARE(anger);
LV_IMAGE_DECLARE(scare);
LV_IMAGE_DECLARE(buxue);

LV_IMAGE_DECLARE(idle);
LV_IMAGE_DECLARE(listening);
LV_IMAGE_DECLARE(sleepy);
LV_IMAGE_DECLARE(speaking);
LV_IMAGE_DECLARE(speaking2);
LV_IMAGE_DECLARE(newmsg);
// 添加新表情声明
LV_IMAGE_DECLARE(duoheshui);

LV_IMAGE_DECLARE(dongyidong);

LV_IMAGE_DECLARE(superhappy);
LV_IMAGE_DECLARE(ok);

struct EmotionMap {
    const char* name;
    const lv_image_dsc_t* image;
};
/*
// 基于 SPI LCD 的 GIF 表情显示类
class CustomSpiGifEmojiDisplay : public SpiLcdDisplay {
public:
    CustomSpiGifEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                            int width, int height, int offset_x, int offset_y,
                            bool mirror_x, bool mirror_y, bool swap_xy,
                            const DisplayFonts& fonts)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy, fonts),
          emotion_gif_(nullptr) {

    ESP_LOGE("GIF", "############CustomSpiGifEmojiDisplay##############");
        SetupGifContainer();
    }

    void SetEmotion(const char* emotion) override;

private:
    lv_obj_t* emotion_gif_;
    void SetupGifContainer();
    static const EmotionMap emotion_maps_[];
};
*/
// 基于 OLED 的 GIF 表情显示类
class CustomOledGifEmojiDisplay : public OledDisplay {
public:
    // 在CustomOledGifEmojiDisplay类的public部分添加
    void HideGif();
    void ShowGif();
    CustomOledGifEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, bool mirror_x, bool mirror_y,
                             const DisplayFonts& fonts)
        : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y, fonts),
          emotion_gif_(nullptr) {
        SetupGifContainer();
    }

    void SetEmotion(const char* emotion) override;

private:
    lv_obj_t* emotion_gif_;
    void SetupGifContainer();
    static const EmotionMap emotion_maps_[];
};


#endif