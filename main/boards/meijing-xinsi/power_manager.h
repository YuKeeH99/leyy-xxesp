#pragma once
#include <vector>
#include <functional>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>

class PowerManager {
private:
    esp_timer_handle_t timer_handle_;
    std::function<void(bool)> on_charging_status_changed_;
    std::function<void(bool)> on_low_battery_status_changed_;

    gpio_num_t charging_pin_ = GPIO_NUM_NC;
    gpio_num_t battery_adc_pin_ = GPIO_NUM_NC;
    std::vector<uint16_t> adc_values_;
    uint32_t battery_level_ = 0;
    bool is_charging_ = false;
    bool is_low_battery_ = false;
    int ticks_ = 0;
    const int kBatteryAdcInterval = 60;
    const int kBatteryAdcDataCount = 3;
    const int kLowBatteryLevel = 20;

    adc_oneshot_unit_handle_t adc_handle_;
    adc_channel_t adc_channel_;
    // 新增：电压分压系数（ADC端电压*ratio ≈ 电池端电压），默认无分压
    float battery_divider_ratio_ = 1.0f;

    void CheckBatteryStatus() {
        // Get charging status if charging pin is configured
        if (charging_pin_ != GPIO_NUM_NC) {
            bool new_charging_status = gpio_get_level(charging_pin_) == 1;
            if (new_charging_status != is_charging_) {
                is_charging_ = new_charging_status;
                if (on_charging_status_changed_) {
                    on_charging_status_changed_(is_charging_);
                }
                ReadBatteryAdcData();
                return;
            }
        }

        // 如果电池电量数据不足，则读取电池电量数据
        if (adc_values_.size() < kBatteryAdcDataCount) {
            ReadBatteryAdcData();
            return;
        }

        // 如果电池电量数据充足，则每 kBatteryAdcInterval 个 tick 读取一次电池电量数据
        ticks_++;
        if (ticks_ % kBatteryAdcInterval == 0) {
            ReadBatteryAdcData();
        }
    }

    void ReadBatteryAdcData() {
        if (battery_adc_pin_ == GPIO_NUM_NC) {
            // 如果没有配置ADC引脚，设置默认电量
            battery_level_ = 50; // 默认50%电量
            return;
        }

        // 读取 ADC 值
        int adc_value;
        esp_err_t ret = adc_oneshot_read(adc_handle_, adc_channel_, &adc_value);
        if (ret != ESP_OK) {
            ESP_LOGE("PowerManager", "ADC read failed: %s", esp_err_to_name(ret));
            return;
        }
       
        // 将 ADC 值添加到队列中
        adc_values_.push_back(adc_value);
        if (adc_values_.size() > kBatteryAdcDataCount) {
            adc_values_.erase(adc_values_.begin());
        }
        uint32_t average_adc = 0;
        for (auto value : adc_values_) {
            average_adc += value;
        }
        average_adc /= adc_values_.size();

        // 使用用户提供的电压阈值做线性映射（单位：mV）
        const int EMPTY_BATTERY_VOLTAGE = 2270; // 2.27V 亏电
        const int FULL_BATTERY_VOLTAGE  = 3130; // 3.13V 满电

        // 近似将 raw(0..4095) 换算成 mV（12位ADC + 12dB 衰减满量程约 3.6V）
        int voltage_mv = static_cast<int>(average_adc * 3600 / 4095);

        // 应用分压系数，换算为电池端电压
        int battery_voltage_mv = static_cast<int>(voltage_mv * battery_divider_ratio_);

        // 夹逼到阈值区间
        if (battery_voltage_mv < EMPTY_BATTERY_VOLTAGE) battery_voltage_mv = EMPTY_BATTERY_VOLTAGE;
        if (battery_voltage_mv > FULL_BATTERY_VOLTAGE)  battery_voltage_mv = FULL_BATTERY_VOLTAGE;

        // 线性比例计算电量百分比
        battery_level_ = (battery_voltage_mv - EMPTY_BATTERY_VOLTAGE) * 100 / (FULL_BATTERY_VOLTAGE - EMPTY_BATTERY_VOLTAGE);
        
        // 检查是否达到低电量阈值
        if (adc_values_.size() >= kBatteryAdcDataCount) {
            bool new_low_battery_status = battery_level_ <= kLowBatteryLevel;
            if (new_low_battery_status != is_low_battery_) {
                is_low_battery_ = new_low_battery_status;
                if (on_low_battery_status_changed_) {
                    on_low_battery_status_changed_(is_low_battery_);
                }
            }
        }

        ESP_LOGI("PowerManager", "ADC avg raw: %ld -> %d mV (ratio=%d/100) -> level: %ld%%", average_adc, battery_voltage_mv, (int)(battery_divider_ratio_ * 100), battery_level_);
    }

public:
    PowerManager(gpio_num_t charging_pin = GPIO_NUM_NC, gpio_num_t battery_adc_pin = GPIO_NUM_NC, adc_channel_t adc_channel = ADC_CHANNEL_0, float battery_divider_ratio = 1.0f) 
        : charging_pin_(charging_pin), battery_adc_pin_(battery_adc_pin), adc_channel_(adc_channel), battery_divider_ratio_(battery_divider_ratio) {
        
        // 初始化充电引脚
        if (charging_pin_ != GPIO_NUM_NC) {
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_INPUT;
            io_conf.pin_bit_mask = (1ULL << charging_pin_);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; 
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     
            gpio_config(&io_conf);
        }

        // 创建电池电量检查定时器
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                PowerManager* self = static_cast<PowerManager*>(arg);
                self->CheckBatteryStatus();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_check_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000)); // 每秒检查一次

        // 初始化 ADC
        if (battery_adc_pin_ != GPIO_NUM_NC) {
            adc_oneshot_unit_init_cfg_t init_config = {
                .unit_id = ADC_UNIT_1,
                .ulp_mode = ADC_ULP_MODE_DISABLE,
            };
            ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle_));
            
            adc_oneshot_chan_cfg_t chan_config = {
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_12,
            };
            ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, adc_channel_, &chan_config));
        }
    }

    ~PowerManager() {
        if (timer_handle_) {
            esp_timer_stop(timer_handle_);
            esp_timer_delete(timer_handle_);
        }
        if (adc_handle_) {
            adc_oneshot_del_unit(adc_handle_);
        }
    }

    uint32_t GetBatteryLevel() {
        return battery_level_;
    }

    bool IsCharging() {
        if (battery_level_ == 100) {
            return false;
        }
        return is_charging_;
    }

    bool IsDischarging() {
        return !is_charging_;
    }

    bool IsLowBattery() {
        return is_low_battery_;
    }

    void SetOnChargingStatusChanged(std::function<void(bool)> callback) {
        on_charging_status_changed_ = callback;
    }

    void SetOnLowBatteryStatusChanged(std::function<void(bool)> callback) {
        on_low_battery_status_changed_ = callback;
    }
};