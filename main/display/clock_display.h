#ifndef CLOCK_DISPLAY_H
#define CLOCK_DISPLAY_H

#include "lcd_display.h"

class ClockDisplay : public SpiLcdDisplay {
public:
    ClockDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                int width, int height, int offset_x, int offset_y,
                bool mirror_x, bool mirror_y, bool swap_xy,
                const DisplayFonts& fonts);
    
    // 设置时钟显示界面
    void SetupClockUI();
    
    // 重写UpdateClock方法
    void UpdateClock(const char* time_str) override;
    
    // 重写IsClockDisplay方法
    bool IsClockDisplay() override { return true; }
    
    // 添加控制时钟显示的方法
    void ShowClock(bool show);
    
    // 重写SetChatMessage方法，确保聊天消息可见
    void SetChatMessage(const char* role, const char* content,int duration_seconds) override;

private:
    lv_obj_t* clock_container_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* date_label_ = nullptr;
    bool clock_visible_ = false;
};

#endif // CLOCK_DISPLAY_H