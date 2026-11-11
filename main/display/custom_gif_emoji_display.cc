#include "custom_gif_emoji_display.h"
#include <esp_log.h>
#include <cstring>

#define TAG "CustomGifEmojiDisplay"
/*
// 表情映射表（复用 electron-bot 的资源）
const EmotionMap CustomSpiGifEmojiDisplay::emotion_maps_[] = {
    {"staticstate", &staticstate},
    {"happy", &happy},
    {"sad", &sad},
    {"anger", &anger},
    {"scare", &scare},
    {"buxue", &buxue},
    {nullptr, nullptr}
};
*/
const EmotionMap CustomOledGifEmojiDisplay::emotion_maps_[] = {
    {"staticstate", &staticstate},
    {"happy", &happy},
    {"sad", &sad},
    {"anger", &anger},
    {"scare", &scare},
    {"idle", &idle},
    {"listening", &listening},
    {"sleepy", &sleepy},
    {"speaking", &speaking},
    {"speaking2", &speaking2},
    {"newmsg", &newmsg},
    // 添加新表情映射
    {"duoheshui", &duoheshui},

    {"dongyidong", &dongyidong},

    {"superhappy", &superhappy},  // 确保这里的符号名称正确
    {"ok", &ok},
    {nullptr, nullptr}
};
/*
// SPI LCD GIF 显示实现
void CustomSpiGifEmojiDisplay::SetupGifContainer() {
    if (emotion_gif_ == nullptr) {
        emotion_gif_ = lv_gif_create(lv_scr_act());
        lv_obj_set_size(emotion_gif_, width_, height_);
        lv_obj_center(emotion_gif_);
    }
}

void CustomSpiGifEmojiDisplay::SetEmotion(const char* emotion) {
    if (!emotion) return;
    
    DisplayLockGuard lock(this);
    
    // 查找对应的 GIF 资源
    for (int i = 0; emotion_maps_[i].name != nullptr; i++) {
        if (strcmp(emotion, emotion_maps_[i].name) == 0) {
            if (emotion_gif_) {
                lv_gif_set_src(emotion_gif_, emotion_maps_[i].image);
                ESP_LOGI(TAG, "Set emotion GIF: %s", emotion);
                return;
            }
        }
    }
    
    // 如果没找到对应的 GIF，回退到父类的字体图标实现
    ESP_LOGW(TAG, "GIF not found for emotion: %s, using font icon", emotion);
    SpiLcdDisplay::SetEmotion(emotion);
}
*/
// OLED GIF 显示实现
void CustomOledGifEmojiDisplay::SetupGifContainer() {
    if (emotion_gif_ == nullptr) {
        // 不使用lv_scr_act()，而是使用与OLED显示关联的屏幕
        lv_obj_t* oled_scr = lv_disp_get_scr_act(display_);
        emotion_gif_ = lv_gif_create(oled_scr);
        
        lv_obj_center(emotion_gif_);
    }
}

void CustomOledGifEmojiDisplay::SetEmotion(const char* emotion) {

    ESP_LOGI(TAG, "Oled SetEmotion: %s", emotion);

    if (!emotion) return;
    
    DisplayLockGuard lock(this);
    
    // 查找对应的 GIF 资源
    for (int i = 0; emotion_maps_[i].name != nullptr; i++) {
        if (strcmp(emotion, emotion_maps_[i].name) == 0) {
            if (emotion_gif_) {
                lv_gif_set_src(emotion_gif_, emotion_maps_[i].image);
                ESP_LOGI(TAG, "Set emotion GIF: %s", emotion);
                return;
            }
        }
    }
    
    // 如果没找到对应的 GIF，回退到父类的字体图标实现
    ESP_LOGW(TAG, "GIF not found for emotion: %s, using font icon", emotion);
    OledDisplay::SetEmotion(emotion);
}

// 在CustomOledGifEmojiDisplay类中添加一个方法来隐藏GIF
void CustomOledGifEmojiDisplay::HideGif() {
    if (emotion_gif_) {
        lv_obj_add_flag(emotion_gif_, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "GIF hidden");
    }
}

// 添加一个方法来显示GIF
void CustomOledGifEmojiDisplay::ShowGif() {
    if (emotion_gif_) {
        lv_obj_clear_flag(emotion_gif_, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "GIF shown");
    }
}
