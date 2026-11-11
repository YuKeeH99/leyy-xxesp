#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_pm.h>

#include <string>

struct DisplayFonts {
    const lv_font_t* text_font = nullptr;
    const lv_font_t* icon_font = nullptr;
    const lv_font_t* emoji_font = nullptr;
};

class Display {
public:
    Display();
    virtual ~Display();

    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification, int duration_ms = 3000);
    virtual void ShowNotification(const std::string &notification, int duration_ms = 3000);
    virtual void SetEmotion(const char* emotion);
    virtual void SetChatMessage(const char* role, const char* content, int duration_seconds = 0);
    virtual void SetIcon(const char* icon);
    virtual void SetPreviewImage(const lv_img_dsc_t* image);
    virtual void SetTheme(const std::string& theme_name);
    virtual std::string GetTheme() { return current_theme_name_; }
    virtual void UpdateStatusBar(bool update_all = false);
    
    // 添加图片保护相关方法
    void SetProtectedImage(const lv_img_dsc_t* image, int protection_time_ms = 5000);
    bool IsImageProtected() const { return is_image_protected_; }
    void ClearImageProtection(bool hide_image = true);

    inline int width() const { return width_; }
    inline int height() const { return height_; }

protected:
    int width_ = 0;
    int height_ = 0;
    
    esp_pm_lock_handle_t pm_lock_ = nullptr;
    lv_display_t *display_ = nullptr;

    lv_obj_t *emotion_label_ = nullptr;
    lv_obj_t *network_label_ = nullptr;
    lv_obj_t *status_label_ = nullptr;
    lv_obj_t *notification_label_ = nullptr;
    lv_obj_t *mute_label_ = nullptr;
    lv_obj_t *battery_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    lv_obj_t* low_battery_popup_ = nullptr;
    lv_obj_t* low_battery_label_ = nullptr;
    
    const char* battery_icon_ = nullptr;
    const char* network_icon_ = nullptr;
    bool muted_ = false;
    std::string current_theme_name_;

    esp_timer_handle_t notification_timer_ = nullptr;
    
    // 添加图片保护相关成员变量
    bool is_image_protected_ = false;
    esp_timer_handle_t image_protection_timer_ = nullptr;
    
    // 添加图片保护定时器回调声明
    static void ImageProtectionTimerCallback(void* arg);

    friend class DisplayLockGuard;
    virtual bool Lock(int timeout_ms = 0) = 0;
    virtual void Unlock() = 0;
    
    // 添加获取display_的方法
    inline lv_display_t* GetLvglDisplay() { return display_; }

public:
    // 添加时钟相关的虚函数
    virtual bool IsClockDisplay() { return false; }
    virtual void UpdateClock(const char* time_str) {}

protected:
    // 添加聊天消息定时器相关成员变量
    esp_timer_handle_t chat_timer_ = nullptr;
    bool has_timed_chat_playing_ = false;
    
    // 添加聊天消息定时器回调声明
    static void ChatTimerCallback(void* arg);
};


class DisplayLockGuard {
public:
    DisplayLockGuard(Display *display) : display_(display) {
        if (!display_->Lock(30000)) {
            ESP_LOGE("Display", "Failed to lock display");
        }
    }
    ~DisplayLockGuard() {
        display_->Unlock();
    }

private:
    Display *display_;
};

class NoDisplay : public Display {
private:
    virtual bool Lock(int timeout_ms = 0) override {
        return true;
    }
    virtual void Unlock() override {}
};

#endif
