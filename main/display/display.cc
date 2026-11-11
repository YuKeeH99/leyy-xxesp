#include <esp_log.h>
#include <esp_err.h>
#include <string>
#include <cstdlib>
#include <cstring>

#include "display.h"
#include "board.h"
#include "application.h"
#include "font_awesome_symbols.h"
#include "audio_codec.h"
#include "settings.h"
#include "assets/lang_config.h"

#define TAG "Display"

Display::Display() {
    // Notification timer
    esp_timer_create_args_t notification_timer_args = {
        .callback = [](void *arg) {
            Display *display = static_cast<Display*>(arg);
            DisplayLockGuard lock(display);
            lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(display->status_label_, LV_OBJ_FLAG_HIDDEN);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // Create a power management lock
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        ESP_ERROR_CHECK(ret);
    }
    
    // 创建图片保护定时器
    esp_timer_create_args_t protection_timer_args = {
        .callback = ImageProtectionTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "image_protection_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&protection_timer_args, &image_protection_timer_));
}

Display::~Display() {
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }

    if (image_protection_timer_ != nullptr) {
        esp_timer_stop(image_protection_timer_);
        esp_timer_delete(image_protection_timer_);
    }
    
    // 添加聊天定时器清理
    if (chat_timer_ != nullptr) {
        esp_timer_stop(chat_timer_);
        esp_timer_delete(chat_timer_);
    }

    if (network_label_ != nullptr) {
        lv_obj_del(network_label_);
        lv_obj_del(notification_label_);
        lv_obj_del(status_label_);
        lv_obj_del(mute_label_);
        lv_obj_del(battery_label_);
        lv_obj_del(emotion_label_);
    }
    if( low_battery_popup_ != nullptr ) {
        lv_obj_del(low_battery_popup_);
    }
    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
}

void Display::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    if (status_label_ == nullptr) {
        return;
    }
    lv_label_set_text(status_label_, status);
    lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
}

void Display::ShowNotification(const std::string &notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void Display::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard lock(this);
    if (notification_label_ == nullptr) {
        return;
    }
    lv_label_set_text(notification_label_, notification);
    lv_obj_clear_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);

    esp_timer_stop(notification_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
}

void Display::UpdateStatusBar(bool update_all) {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();

    {
        DisplayLockGuard lock(this);
        if (mute_label_ == nullptr) {
            return;
        }

        // 如果静音状态改变，则更新图标
        if (codec->output_volume() == 0 && !muted_) {
            muted_ = true;
            lv_label_set_text(mute_label_, FONT_AWESOME_VOLUME_MUTE);
        } else if (codec->output_volume() > 0 && muted_) {
            muted_ = false;
            lv_label_set_text(mute_label_, "");
        }
    }

    esp_pm_lock_acquire(pm_lock_);
    // 更新电池图标
    int battery_level;
    bool charging, discharging;
    const char* icon = nullptr;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        if (charging) {
            icon = FONT_AWESOME_BATTERY_CHARGING;
        } else {
            const char* levels[] = {
                FONT_AWESOME_BATTERY_EMPTY, // 0-19%
                FONT_AWESOME_BATTERY_1,    // 20-39%
                FONT_AWESOME_BATTERY_2,    // 40-59%
                FONT_AWESOME_BATTERY_3,    // 60-79%
                FONT_AWESOME_BATTERY_FULL, // 80-99%
                FONT_AWESOME_BATTERY_FULL, // 100%
            };
            icon = levels[battery_level / 20];
        }
        DisplayLockGuard lock(this);
        if (battery_label_ != nullptr && battery_icon_ != icon) {
            battery_icon_ = icon;
            lv_label_set_text(battery_label_, battery_icon_);
        }

        if (low_battery_popup_ != nullptr) {
            if (strcmp(icon, FONT_AWESOME_BATTERY_EMPTY) == 0 && discharging) {
                if (lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) { // 如果低电量提示框隐藏，则显示
                    lv_obj_clear_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                    auto& app = Application::GetInstance();
                    app.PlaySound(Lang::Sounds::P3_LOW_BATTERY);
                }
            } else {
                // Hide the low battery popup when the battery is not empty
                if (!lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) { // 如果低电量提示框显示，则隐藏
                    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }

    // 每 10 秒更新一次网络图标
    static int seconds_counter = 0;
    if (update_all || seconds_counter++ % 10 == 0) {
        // 升级固件时，不读取 4G 网络状态，避免占用 UART 资源
        auto device_state = Application::GetInstance().GetDeviceState();
        static const std::vector<DeviceState> allowed_states = {
            kDeviceStateIdle,
            kDeviceStateStarting,
            kDeviceStateWifiConfiguring,
            kDeviceStateListening,
            kDeviceStateActivating,
            kDeviceStateKeepConnected,  // 添加这一行
            kDeviceStateSpeaking

        };
        if (std::find(allowed_states.begin(), allowed_states.end(), device_state) != allowed_states.end()) {
            icon = board.GetNetworkStateIcon();
            if (network_label_ != nullptr && icon != nullptr && network_icon_ != icon) {
                DisplayLockGuard lock(this);
                network_icon_ = icon;
                lv_label_set_text(network_label_, network_icon_);
            }
        }
    }

    esp_pm_lock_release(pm_lock_);
}


void Display::SetEmotion(const char* emotion) {
    // 检查图片是否受保护
    if (is_image_protected_) {
        ESP_LOGI("Display", "SetEmotion blocked - image is protected: %s", emotion);
        return;
    }
    
    ESP_LOGI(TAG, "SetEmotion: %s", emotion);
    struct Emotion {
        const char* icon;
        const char* text;
    };

    static const std::vector<Emotion> emotions = {
        {FONT_AWESOME_EMOJI_NEUTRAL, "neutral"},
        {FONT_AWESOME_EMOJI_HAPPY, "happy"},
        {FONT_AWESOME_EMOJI_LAUGHING, "laughing"},
        {FONT_AWESOME_EMOJI_FUNNY, "funny"},
        {FONT_AWESOME_EMOJI_SAD, "sad"},
        {FONT_AWESOME_EMOJI_ANGRY, "angry"},
        {FONT_AWESOME_EMOJI_CRYING, "crying"},
        {FONT_AWESOME_EMOJI_LOVING, "loving"},
        {FONT_AWESOME_EMOJI_EMBARRASSED, "embarrassed"},
        {FONT_AWESOME_EMOJI_SURPRISED, "surprised"},
        {FONT_AWESOME_EMOJI_SHOCKED, "shocked"},
        {FONT_AWESOME_EMOJI_THINKING, "thinking"},
        {FONT_AWESOME_EMOJI_WINKING, "winking"},
        {FONT_AWESOME_EMOJI_COOL, "cool"},
        {FONT_AWESOME_EMOJI_RELAXED, "relaxed"},
        {FONT_AWESOME_EMOJI_DELICIOUS, "delicious"},
        {FONT_AWESOME_EMOJI_KISSY, "kissy"},
        {FONT_AWESOME_EMOJI_CONFIDENT, "confident"},
        {FONT_AWESOME_EMOJI_SLEEPY, "sleepy"},
        {FONT_AWESOME_EMOJI_SILLY, "silly"},
        {FONT_AWESOME_EMOJI_CONFUSED, "confused"}
    };
    
    // 查找匹配的表情
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
        [&emotion_view](const Emotion& e) { return e.text == emotion_view; });
    
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }

    // 如果找到匹配的表情就显示对应图标，否则显示默认的neutral表情
    if (it != emotions.end()) {
        lv_label_set_text(emotion_label_, it->icon);
    } else {
        lv_label_set_text(emotion_label_, FONT_AWESOME_EMOJI_NEUTRAL);
    }
}

void Display::SetIcon(const char* icon) {
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    lv_label_set_text(emotion_label_, icon);
}

void Display::SetPreviewImage(const lv_img_dsc_t* image) {
    // Do nothing
}

void Display::SetChatMessage(const char* role, const char* content, int duration_seconds) {
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }
    
    // 如果有定时聊天消息正在播放，且当前请求没有时间参数，则阻止播放
    if (has_timed_chat_playing_ && duration_seconds == 0) {
        ESP_LOGD(TAG, "Blocking chat message without duration while timed chat is playing");
        return;
    }
    
    // 停止并清理之前的聊天定时器
    if (chat_timer_ != nullptr) {
        esp_timer_stop(chat_timer_);
        esp_timer_delete(chat_timer_);
        chat_timer_ = nullptr;
        has_timed_chat_playing_ = false;
    }
    
    // 设置聊天消息内容
    lv_label_set_text(chat_message_label_, content);
    
    // 如果指定了持续时间，创建定时器
    if (duration_seconds > 0) {
        has_timed_chat_playing_ = true;
        
        esp_timer_create_args_t timer_args = {
            .callback = ChatTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "chat_timer"
        };
        
        esp_err_t err = esp_timer_create(&timer_args, &chat_timer_);
        if (err == ESP_OK) {
            esp_timer_start_once(chat_timer_, duration_seconds * 1000000ULL); // 转换为微秒
            ESP_LOGD(TAG, "Chat message timer started for %d seconds", duration_seconds);
        } else {
            ESP_LOGE(TAG, "Failed to create chat timer: %s", esp_err_to_name(err));
            has_timed_chat_playing_ = false;
        }
    }
}

void Display::SetTheme(const std::string& theme_name) {
    current_theme_name_ = theme_name;
    Settings settings("display", true);
    settings.SetString("theme", theme_name);
}


void Display::SetProtectedImage(const lv_img_dsc_t* image, int protection_time_ms) {
    // 设置图片
    SetPreviewImage(image);
    
    // 启动保护状态
    is_image_protected_ = true;
    
    // 启动保护定时器
    esp_timer_stop(image_protection_timer_);
    esp_timer_start_once(image_protection_timer_, protection_time_ms * 1000); // 转换为微秒
    
    ESP_LOGI("Display", "Image protection enabled for %d ms", protection_time_ms);
}

void Display::ClearImageProtection(bool hide_image) {
    esp_timer_stop(image_protection_timer_);
    is_image_protected_ = false;
    if (hide_image) {
        SetPreviewImage(nullptr);
    }
    ESP_LOGI("Display", "Image protection cleared");
}

void Display::ImageProtectionTimerCallback(void* arg) {
    Display* display = static_cast<Display*>(arg);
    display->is_image_protected_ = false;
    ESP_LOGI("Display", "Image protection disabled");
}

void Display::ChatTimerCallback(void* arg) {
    Display* display = static_cast<Display*>(arg);
    
    // 创建任务来处理UI更新，避免在定时器回调中直接操作LVGL
    auto clear_task = [](void* param) {
        /*
        // 清除聊天消息
    if (display->chat_message_label_ != nullptr) {
        lv_label_set_text(display->chat_message_label_, "");
    }
    
    // 清理定时器状态
    display->has_timed_chat_playing_ = false;
    if (display->chat_timer_ != nullptr) {
        esp_timer_delete(display->chat_timer_);
        display->chat_timer_ = nullptr;
    }
    
    ESP_LOGD(TAG, "Chat message timer expired, message cleared");
        */
        Display* disp = static_cast<Display*>(param);
        
        // 在任务上下文中安全地更新UI
        if (disp->chat_message_label_ != nullptr) {
            DisplayLockGuard lock(disp);
            lv_label_set_text(disp->chat_message_label_, "");
        }
        
        // 清理定时器状态
        disp->has_timed_chat_playing_ = false;
        if (disp->chat_timer_ != nullptr) {
            esp_timer_delete(disp->chat_timer_);
            disp->chat_timer_ = nullptr;
        }
        
        ESP_LOGD(TAG, "Chat message timer expired, message cleared");
        vTaskDelete(NULL);
    };
    
    // 创建一次性任务来处理UI更新
    xTaskCreate(clear_task, "chat_clear_task", 2048, display, 5, NULL);
}
