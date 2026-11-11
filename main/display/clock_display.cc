#include "clock_display.h"
#include <esp_log.h>
#include <cstring>

// 添加字体声明
extern const lv_font_t font_puhui_16_4;

#define TAG "ClockDisplay"

ClockDisplay::ClockDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, int offset_x, int offset_y,
                         bool mirror_x, bool mirror_y, bool swap_xy,
                         const DisplayFonts& fonts)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy, fonts) {
    
    ESP_LOGI(TAG, "Creating ClockDisplay instance");
    // 在构造函数中调用SetupClockUI
    SetupClockUI();
}

void ClockDisplay::SetupClockUI() {
    DisplayLockGuard lock(this);
    
    // 获取与LCD显示关联的屏幕
    lv_obj_t* lcd_scr = lv_disp_get_scr_act(display_);
    
    // 创建时钟容器
    clock_container_ = lv_obj_create(lcd_scr);

    // 默认隐藏时钟，只有在明确调用ShowClock(true)时才显示
    lv_obj_add_flag(clock_container_, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_set_size(clock_container_, width_, height_);
    lv_obj_center(clock_container_);
    // 强制设置黑色背景，不随主题变换
    lv_obj_set_style_bg_color(clock_container_, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(clock_container_, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(clock_container_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // 设置圆角为0，使其变为直角
    lv_obj_set_style_radius(clock_container_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 创建时钟标签，使用更大的字体模拟加粗效果
    clock_label_ = lv_label_create(clock_container_);
    // 直接使用字体，不需要额外的变量
    lv_obj_set_style_text_font(clock_label_, &font_puhui_16_4, LV_PART_MAIN | LV_STATE_DEFAULT);
    // 强制设置白色字体，不随主题变换
    lv_obj_set_style_text_color(clock_label_, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(clock_label_, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 注释掉或删除以下阴影效果设置
    /*
    lv_obj_set_style_shadow_color(clock_label_, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(clock_label_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(clock_label_, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(clock_label_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(clock_label_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    */
    
    lv_label_set_text(clock_label_, "00:00:00");
    lv_obj_align(clock_label_, LV_ALIGN_CENTER, 0, -20);
    
    // 创建日期标签
    date_label_ = lv_label_create(clock_container_);
    // 使用相同的字体
    lv_obj_set_style_text_font(date_label_, &font_puhui_16_4, LV_PART_MAIN | LV_STATE_DEFAULT);
    // 强制设置白色字体，不随主题变换
    lv_obj_set_style_text_color(date_label_, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(date_label_, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // 注释掉或删除以下日期标签的阴影效果设置
    /*
    lv_obj_set_style_shadow_color(date_label_, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(date_label_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(date_label_, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(date_label_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(date_label_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    */
    
    lv_label_set_text(date_label_, "2023-01-01");
    lv_obj_align(date_label_, LV_ALIGN_CENTER, 0, 20);
    
    ESP_LOGI(TAG, "Clock UI setup complete with bold-like styling");
}

// 添加ShowClock方法实现
void ClockDisplay::ShowClock(bool show) {
    DisplayLockGuard lock(this);
    
    clock_visible_ = show;
    if (show) {
        lv_obj_clear_flag(clock_container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(clock_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

// 重写SetChatMessage方法
void ClockDisplay::SetChatMessage(const char* role, const char* content,int duration_seconds) {
    // 先调用父类方法添加聊天消息
    SpiLcdDisplay::SetChatMessage(role, content,duration_seconds);
    
    // 如果当前是聊天模式，确保时钟隐藏
    if (!clock_visible_ && clock_container_) {
        lv_obj_add_flag(clock_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

// 修改UpdateClock方法
void ClockDisplay::UpdateClock(const char* time_str) {
    if (!time_str) return;
    
    DisplayLockGuard lock(this);
    
    // 确保时钟容器可见（如果处于时钟模式）
    if (clock_visible_ && clock_container_) {
        lv_obj_clear_flag(clock_container_, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (clock_label_) {
        // 直接使用传入的时间字符串
        lv_label_set_text(clock_label_, time_str);
        
        // 获取当前日期并确保时间已经正确设置
        time_t now = time(NULL);
        
        // 检查时间是否有效（不是1970年）
        struct tm *timeinfo = localtime(&now);
        if (timeinfo->tm_year > 70) { // 如果年份大于1970年
            char date_str[64];
            strftime(date_str, sizeof(date_str), "%Y-%m-%d", timeinfo);
            
            if (date_label_) {
                lv_label_set_text(date_label_, date_str);
            }
        } else {
            // 如果时间无效，显示提示信息
            if (date_label_) {
                lv_label_set_text(date_label_, "等待时间同步...");
            }
        }
    }
}