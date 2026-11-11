#include "radar_module.h"
#include <esp_log.h>
#include "string.h"
#define TAG "RadarModule"
#define RADAR_BUFFER_SIZE 1024

// 在构造函数中初始化滤波器
RadarModule::RadarModule()
    : uart_num_(UART_NUM_1),
      task_handle_(nullptr),
      is_running_(false),
      short_hold_threshold_ms_(5000),  // 默认5秒
      short_distance_start_time_(0),
      is_short_distance_(false),
      filter_index_(0),
      filter_initialized_(false) {
    // 初始化滤波缓冲区
    memset(distance_filter_buffer_, 0, sizeof(distance_filter_buffer_));
}

RadarModule::~RadarModule() {
    Stop();
}

bool RadarModule::Init(uart_port_t uart_num, int tx_pin, int rx_pin, int baud_rate) {
    uart_num_ = uart_num;
    
    // 配置UART参数
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_APB,
    };
    
    // 安装UART驱动
    ESP_LOGI(TAG, "Installing UART driver for radar module");
    if (uart_driver_install(uart_num_, RADAR_BUFFER_SIZE * 2, RADAR_BUFFER_SIZE * 2, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver");
        return false;
    }
    
    // 配置UART参数
    if (uart_param_config(uart_num_, &uart_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters");
        return false;
    }
    
    // 设置UART引脚
    if (uart_set_pin(uart_num_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins");
        return false;
    }
    
    ESP_LOGI(TAG, "Radar module initialized successfully");
    return true;
}

void RadarModule::Start() {
    ESP_LOGW(TAG, "Radar start");
    if (is_running_) {
        ESP_LOGW(TAG, "Radar module is already running");
        return;
    }
    
    is_running_ = true;
    
    // 创建雷达数据处理任务
    xTaskCreate(RadarTask, "radar_task", 4096, this, 5, &task_handle_);
    
    ESP_LOGI(TAG, "Radar module started");
}

void RadarModule::Stop() {
    if (!is_running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping radar module...");
    is_running_ = false;
    
    // 等待任务自然结束，不要手动删除
    if (task_handle_ != nullptr) {
        // 等待任务自己退出，最多等待1秒
        for (int i = 0; i < 10; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            // 检查任务是否已经结束
            if (eTaskGetState(task_handle_) == eDeleted) {
                break;
            }
        }
        task_handle_ = nullptr;
    }
    
    // 卸载UART驱动
    uart_driver_delete(uart_num_);
    
    ESP_LOGI(TAG, "Radar module stopped");
}

bool RadarModule::SendCommand(const std::string& command) {
    if (!is_running_) {
        ESP_LOGE(TAG, "Radar module is not running");
        return false;
    }
    
    // 发送命令到雷达模块
    int len = uart_write_bytes(uart_num_, command.c_str(), command.length());
    if (len != command.length()) {
        ESP_LOGE(TAG, "Failed to send command to radar module");
        return false;
    }
    
    return true;
}

void RadarModule::SetEnabled(bool enabled) {
    if (enabled == is_enabled_) {
        return;  // 状态未变，不需要操作
    }
    
    is_enabled_ = enabled;
    ESP_LOGI(TAG, "Radar module %s", enabled ? "enabled" : "disabled");
    
    if (enabled) {
        // 如果雷达任务已停止，则重新启动
        if (!is_running_) {
            Start();
        }
    } else {
        // 如果雷达任务正在运行，则停止
        if (is_running_) {
            Stop();
        }
        
        // 重置人员检测和久坐检测状态
        target_detected_ = false;
        is_sitting_ = false;
        first_detection_time_ = 0;
        sitting_start_time_ = 0;
    }
}

void RadarModule::RadarTask(void* pvParameters) {
    RadarModule* radar = static_cast<RadarModule*>(pvParameters);
    uint8_t* data = (uint8_t*) malloc(RADAR_BUFFER_SIZE);
    
    if (data == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for radar data");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Radar task started, waiting for data...");
    
    while (radar->is_running_) {
        // 添加错误处理和重试机制
        int len = uart_read_bytes(radar->uart_num_, data, RADAR_BUFFER_SIZE, pdMS_TO_TICKS(100));
        
        if (len < 0) {
            ESP_LOGE(TAG, "UART read error: %d", len);
            // 清空UART缓冲区
            uart_flush(radar->uart_num_);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        if (len == 0) {
            // 没有数据，继续等待
            continue;
        }
        
        if (len > 0) {
            // 处理雷达数据
            std::string radar_data(reinterpret_cast<char*>(data), len);
            radar->latest_data_ = radar_data;
            
            // 解析雷达数据帧
            radar->ParseRadarFrame(data, len);
        }
        
        // 增加任务延时，降低CPU占用
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    free(data);
    ESP_LOGI(TAG, "Radar task ended");
    vTaskDelete(NULL);
}

// 解析雷达数据帧
void RadarModule::ParseRadarFrame(const uint8_t* bytes, size_t length) {
    // 添加空指针和最小长度检查
    if (!bytes || length < 23) {
        return;
    }
    
    // 查找完整数据帧
    for (size_t i = 0; i <= length - 23; i++) {
        // 检查帧头 F4 F3 F2 F1
        if (bytes[i] == 0xF4 && bytes[i+1] == 0xF3 && 
            bytes[i+2] == 0xF2 && bytes[i+3] == 0xF1) {
            
            // 确保有足够的字节读取长度字段
            if (i + 5 >= length) {
                break;
            }
            
            // 获取帧数据长度
            uint16_t data_length = bytes[i+4] | (bytes[i+5] << 8);
            
            // 添加数据长度合理性检查
            if (data_length > 1024 || data_length < 13) {
                continue;
            }
            
            // 检查是否有足够的数据和正确的帧尾
            size_t frame_end = i + 4 + 2 + data_length + 4;
            if (frame_end > length) {
                continue;
            }
            
            size_t tail_pos = i + 4 + 2 + data_length;
            if (bytes[tail_pos] == 0xF8 && bytes[tail_pos+1] == 0xF7 && 
                bytes[tail_pos+2] == 0xF6 && bytes[tail_pos+3] == 0xF5) {
                
                // 解析帧数据
                size_t data_start = i + 6; // 跳过帧头和长度
                
                // 确保有足够的数据进行解析
                if (data_start + 10 >= length) {
                    continue;
                }
                
                if (data_length >= 13) {
                    uint8_t data_type = bytes[data_start];
                    uint8_t head = bytes[data_start + 1];
                    
                    // 处理目标基本信息数据 (0x02) 或工程模式数据 (0x01)
                    if ((data_type == 0x02 || data_type == 0x01) && head == 0xAA) {
                        uint8_t target_state = bytes[data_start + 2];
                        
                        // 解析距离数据 (小端序)
                        uint16_t moving_distance = bytes[data_start + 3] | (bytes[data_start + 4] << 8);
                        uint8_t moving_energy = bytes[data_start + 5];
                        uint16_t static_distance = bytes[data_start + 6] | (bytes[data_start + 7] << 8);
                        uint8_t static_energy = bytes[data_start + 8];
                        uint16_t detection_distance = bytes[data_start + 9] | (bytes[data_start + 10] << 8);
                    
                        // 处理目标状态变化
                        HandleTargetStateChange(target_state, moving_distance, moving_energy, 
                                               static_distance, static_energy, detection_distance);
                        
                        // 找到有效帧后退出循环
                        break;
                    }
                }
            }
        }
    }
}

// 处理目标状态变化
void RadarModule::HandleTargetStateChange(uint8_t target_state, uint16_t moving_distance, uint8_t moving_energy, 
                                         uint16_t static_distance, uint8_t static_energy, uint16_t detection_distance) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 添加缺失的变量定义
    bool has_moving_target = (target_state == 0x01 || target_state == 0x03);
    bool has_static_target = (target_state == 0x02 || target_state == 0x03);
    
    // 添加雷达状态回调调用 - 在测试模式下显示雷达目标信息
    if (radar_status_callback_) {
        radar_status_callback_(target_state, moving_distance, moving_energy, 
                              static_distance, static_energy, detection_distance);
    }
    uint16_t filtered_distance = FilterDistance(moving_distance); 

    
    // 使用静态变量跟踪静态目标的持续时间
    static uint32_t static_only_start_time = 0;
    static uint32_t static_duration = 0;
    
    // 更新目标检测状态 - 现在以运动目标为主要判断依据
    bool previous_target_detected = target_detected_;
    
    // 判断目标检测的逻辑：
    // 1. 有运动目标且运动能量超过阈值，则认为有人
    // 2. 只有静态目标，但持续时间不长，也认为有人
    bool significant_movement = has_moving_target && moving_energy >= 20;
    
    // 更新静态持续时间计算
    if (has_static_target && !significant_movement) {
        // 只有静态目标或运动能量很低
        if (static_only_start_time == 0) {
            static_only_start_time = current_time;
        }
        static_duration = current_time - static_only_start_time;
    } else {
        // 有明显运动，重置静态计时
        static_only_start_time = 0;
        static_duration = 0;
    }
    
    // 静态目标持续时间超过10秒，且没有明显运动，认为可能是环境物体而非人
    bool likely_environment_object = static_duration > 10000 && !significant_movement;
    
    // 更新目标检测状态
    target_detected_ = significant_movement || (has_static_target && !likely_environment_object);
    
    // 人员状态维护逻辑
    if (target_detected_) {
        // 有目标检测到
        if (first_detection_time_ == 0) {
            // 首次检测到目标
            first_detection_time_ = current_time;
            last_detection_time_ = current_time;
            
            // 如果当前状态是无人，则转为来人状态
            if (current_person_state_ == PersonState::NOBODY) {
                current_person_state_ = PersonState::PERSON_COMING;
                ESP_LOGI(TAG, "Person state changed: NOBODY -> PERSON_COMING");
                
                // 触发人员状态回调
                if (person_state_callback_) {
                    person_state_callback_(current_person_state_);
                }
            }
        } else {
            // 更新最后检测时间
            last_detection_time_ = current_time;
            
            // 如果处于来人状态，且持续时间超过阈值，则转为有人状态
            if (current_person_state_ == PersonState::PERSON_COMING && 
                (current_time - first_detection_time_ >= coming_threshold_ms_)) {
                current_person_state_ = PersonState::PERSON_PRESENT;
                ESP_LOGI(TAG, "Person state changed: PERSON_COMING -> PERSON_PRESENT");
                
                // 触发人员状态回调
                if (person_state_callback_) {
                    person_state_callback_(current_person_state_);
                }
                
                // 开始久坐计时
                if (!is_sitting_) {
                    is_sitting_ = true;
                    sitting_start_time_ = current_time;
                    ESP_LOGI(TAG, "Started sitting time tracking");
                }
            }
            
            // 新增：如果处于人走状态，但检测到明显运动，则转回有人状态
            else if (current_person_state_ == PersonState::PERSON_LEAVING && significant_movement) {
                current_person_state_ = PersonState::PERSON_PRESENT;
                ESP_LOGI(TAG, "Person state changed: PERSON_LEAVING -> PERSON_PRESENT (movement detected)");
                
                // 触发人员状态回调
                if (person_state_callback_) {
                    person_state_callback_(current_person_state_);
                }
                
                // 开始久坐计时
                if (!is_sitting_) {
                    is_sitting_ = true;
                    sitting_start_time_ = current_time;
                    ESP_LOGI(TAG, "Started sitting time tracking");
                }
            }
        }
    } else {
        // 无目标检测
        if (previous_target_detected) {
            // 从有目标变为无目标
            if (current_person_state_ == PersonState::PERSON_PRESENT || 
                current_person_state_ == PersonState::PERSON_COMING) {
                current_person_state_ = PersonState::PERSON_LEAVING;
                ESP_LOGI(TAG, "Person state changed: %s -> PERSON_LEAVING", 
                         current_person_state_ == PersonState::PERSON_PRESENT ? "PERSON_PRESENT" : "PERSON_COMING");
                
                // 触发人员状态回调
                if (person_state_callback_) {
                    person_state_callback_(current_person_state_);
                }
            }
        } else if (current_person_state_ == PersonState::PERSON_LEAVING) {
            // 已经处于人走状态，检查是否超过阈值
            if (current_time - last_detection_time_ >= leaving_threshold_ms_) {
                current_person_state_ = PersonState::NOBODY;
                ESP_LOGI(TAG, "Person state changed: PERSON_LEAVING -> NOBODY");
                
                // 触发人员状态回调
                if (person_state_callback_) {
                    person_state_callback_(current_person_state_);
                }
                
                // 重置检测时间
                first_detection_time_ = 0;
                
                // 重置久坐状态
                is_sitting_ = false;
                sitting_start_time_ = 0;
            }
        }
    }
    
    // 久坐检测逻辑 - 修改为更精确地判断静态状态
    if (is_sitting_ && sitting_start_time_ > 0 && (current_person_state_ == PersonState::PERSON_PRESENT || current_person_state_ == PersonState::SHORT_HOLD)) {
    
        // 判断是否处于静态状态：只有静态目标，或者运动目标能量很低
        bool is_still_sitting = has_static_target && 
                              (!has_moving_target || moving_energy < 20);  // 降低运动能量阈值，更敏感地检测运动
        
        // 使用静态变量跟踪连续静坐的时间
        static uint32_t continuous_sitting_time = 0;
        static uint32_t last_sitting_check_time = 0;
        
        if (is_still_sitting) {
            // 累计静坐持续时间
            if (last_sitting_check_time > 0) {
                continuous_sitting_time += (current_time - last_sitting_check_time);
            }
            last_sitting_check_time = current_time;
            
            // 检查是否达到久坐阈值
            if (continuous_sitting_time >= long_sitting_threshold_ms_) {
                ESP_LOGI(TAG, "Long sitting detected for %" PRIu32 " ms", continuous_sitting_time);
                
                // 触发人员状态回调（久坐状态）
                if (person_state_callback_) {
                    person_state_callback_(PersonState::LONG_SITTING);
                }
                
                // 重置久坐计时，开始新一轮计时
                continuous_sitting_time = 0;
            }
        } else if (has_moving_target && moving_energy >= 30) {
            // 检测到明显运动，重置久坐计时
            //ESP_LOGI(TAG, "Significant movement detected, resetting sitting timer");
            continuous_sitting_time = 0;
            last_sitting_check_time = current_time;
        }
    }
    
    // SHORT_HOLD状态检测逻辑
    uint16_t current_distance = filtered_distance; // 使用检测距离作为判断依据
    if (current_distance > 0 && current_distance <= 72) { // 距离小于50cm (根据实际情况调整)
        if (!is_short_distance_) {
            // 开始短距离计时
            is_short_distance_ = true;
            short_distance_start_time_ = current_time;
        } else {
            // 检查是否达到SHORT_HOLD阈值
            uint32_t short_distance_duration = current_time - short_distance_start_time_;
            if (short_distance_duration >= short_hold_threshold_ms_) {
                // 触发SHORT_HOLD状态
                if (current_person_state_ != PersonState::SHORT_HOLD) {
                    current_person_state_ = PersonState::SHORT_HOLD;
                    ESP_LOGI(TAG, "Person state changed: -> SHORT_HOLD (distance < 50cm for %" PRIu32 " ms)", short_distance_duration);
                    
                    // 触发人员状态回调
                    if (person_state_callback_) {
                        person_state_callback_(current_person_state_);
                    }
                    short_distance_start_time_ = current_time;
                }
            }
        }
    } else {
        // 距离不满足条件，重置短距离状态
        is_short_distance_ = false;
        short_distance_start_time_ = 0;
        
        // 如果当前是SHORT_HOLD状态，需要根据其他条件转换状态
        if (current_person_state_ == PersonState::SHORT_HOLD) {
            if (target_detected_) {
                current_person_state_ = PersonState::PERSON_PRESENT;
                ESP_LOGI(TAG, "Person state changed: SHORT_HOLD -> PERSON_PRESENT");
            } else {
                current_person_state_ = PersonState::PERSON_LEAVING;
                ESP_LOGI(TAG, "Person state changed: SHORT_HOLD -> PERSON_LEAVING");
            }
            
            // 触发人员状态回调
            if (person_state_callback_) {
                person_state_callback_(current_person_state_);
            }
        }
    }
}

// LD2410B 协议帧格式辅助函数
static std::vector<uint8_t> CreateCommand(uint16_t command, const std::vector<uint8_t>& data = {}) {
    std::vector<uint8_t> frame;
    
    // 帧头: FD FC FB FA
    frame.push_back(0xFD);
    frame.push_back(0xFC);
    frame.push_back(0xFB);
    frame.push_back(0xFA);
    
    // 帧内数据长度 (2字节，小端序)
    uint16_t data_length = 2 + data.size(); // 命令字(2字节) + 数据
    frame.push_back(data_length & 0xFF);
    frame.push_back((data_length >> 8) & 0xFF);
    
    // 命令字 (2字节，小端序)
    frame.push_back(command & 0xFF);
    frame.push_back((command >> 8) & 0xFF);
    
    // 命令值
    frame.insert(frame.end(), data.begin(), data.end());
    
    // 帧尾: 04 03 02 01
    frame.push_back(0x04);
    frame.push_back(0x03);
    frame.push_back(0x02);
    frame.push_back(0x01);
    
    return frame;
}

bool RadarModule::EnableConfiguration() {
    if (!is_running_) {
        ESP_LOGE(TAG, "Radar module is not running");
        return false;
    }
    
    // 使能配置命令: 0x00FF, 命令值: 0x0001
    std::vector<uint8_t> data = {0x01, 0x00};
    auto command = CreateCommand(0x00FF, data);
    
    int len = uart_write_bytes(uart_num_, command.data(), command.size());
    if (len != command.size()) {
        ESP_LOGE(TAG, "Failed to send enable configuration command");
        return false;
    }
    
    ESP_LOGI(TAG, "Enable configuration command sent");
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待雷达响应
    return true;
}

bool RadarModule::EndConfiguration() {
    if (!is_running_) {
        ESP_LOGE(TAG, "Radar module is not running");
        return false;
    }
    
    // 结束配置命令: 0x00FE, 无命令值
    auto command = CreateCommand(0x00FE);
    
    int len = uart_write_bytes(uart_num_, command.data(), command.size());
    if (len != command.size()) {
        ESP_LOGE(TAG, "Failed to send end configuration command");
        return false;
    }
    
    ESP_LOGI(TAG, "End configuration command sent");
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待雷达响应
    return true;
}

bool RadarModule::SetMaxDistanceAndDuration(uint16_t max_motion_gate, uint16_t max_static_gate, uint16_t no_person_duration) {
    if (!is_running_) {
        ESP_LOGE(TAG, "Radar module is not running");
        return false;
    }
    
    // 限制参数范围
    if (max_motion_gate > 8 || max_static_gate > 8) {
        ESP_LOGE(TAG, "Distance gate must be 0-8");
        return false;
    }
    
    // 命令字: 0x0060
    // 命令值: 最大运动距离门(2字节) + 最大静止距离门(2字节) + 无人持续时间(2字节)
    std::vector<uint8_t> data;
    
    // 最大运动距离门 (小端序)
    data.push_back(max_motion_gate & 0xFF);
    data.push_back((max_motion_gate >> 8) & 0xFF);
    
    // 最大静止距离门 (小端序)
    data.push_back(max_static_gate & 0xFF);
    data.push_back((max_static_gate >> 8) & 0xFF);
    
    // 无人持续时间 (小端序)
    data.push_back(no_person_duration & 0xFF);
    data.push_back((no_person_duration >> 8) & 0xFF);
    
    auto command = CreateCommand(0x0060, data);
    
    int len = uart_write_bytes(uart_num_, command.data(), command.size());
    if (len != command.size()) {
        ESP_LOGE(TAG, "Failed to send max distance and duration command");
        return false;
    }
    
    ESP_LOGI(TAG, "Set max distance and duration: motion_gate=%d, static_gate=%d, duration=%d", 
             max_motion_gate, max_static_gate, no_person_duration);
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待雷达响应
    return true;
}

bool RadarModule::SetGateSensitivity(uint16_t gate, uint32_t motion_sensitivity, uint32_t static_sensitivity) {
    if (!is_running_) {
        ESP_LOGE(TAG, "Radar module is not running");
        return false;
    }
    
    // 限制参数范围
    if (gate > 8) {
        ESP_LOGE(TAG, "Distance gate must be 0-8");
        return false;
    }
    
    if (motion_sensitivity > 100 || static_sensitivity > 100) {
        ESP_LOGE(TAG, "Sensitivity must be 0-100");
        return false;
    }
    
    // 命令字: 0x0064
    // 命令值: 距离门(2字节) + 运动灵敏度(4字节) + 静止灵敏度(4字节)
    std::vector<uint8_t> data;
    
    // 距离门 (小端序)
    data.push_back(gate & 0xFF);
    data.push_back((gate >> 8) & 0xFF);
    
    // 运动灵敏度 (小端序，4字节)
    data.push_back(motion_sensitivity & 0xFF);
    data.push_back((motion_sensitivity >> 8) & 0xFF);
    data.push_back((motion_sensitivity >> 16) & 0xFF);
    data.push_back((motion_sensitivity >> 24) & 0xFF);
    
    // 静止灵敏度 (小端序，4字节)
    data.push_back(static_sensitivity & 0xFF);
    data.push_back((static_sensitivity >> 8) & 0xFF);
    data.push_back((static_sensitivity >> 16) & 0xFF);
    data.push_back((static_sensitivity >> 24) & 0xFF);
    
    auto command = CreateCommand(0x0064, data);
    
    int len = uart_write_bytes(uart_num_, command.data(), command.size());
    if (len != command.size()) {
        ESP_LOGE(TAG, "Failed to send gate sensitivity command");
        return false;
    }
    
    ESP_LOGI(TAG, "Set gate %d sensitivity: motion=%lu, static=%lu", 
             gate, motion_sensitivity, static_sensitivity);
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待雷达响应
    return true;
}

bool RadarModule::SetAllGatesSensitivity(uint32_t motion_sensitivity, uint32_t static_sensitivity) {
    if (!is_running_) {
        ESP_LOGE(TAG, "Radar module is not running");
        return false;
    }
    
    // 限制参数范围
    if (motion_sensitivity > 100 || static_sensitivity > 100) {
        ESP_LOGE(TAG, "Sensitivity must be 0-100");
        return false;
    }
    
    // 为所有距离门(0-8)设置相同的灵敏度
    bool success = true;
    for (uint16_t gate = 0; gate <= 8; gate++) {
        if (!SetGateSensitivity(gate, motion_sensitivity, static_sensitivity)) {
            success = false;
            ESP_LOGE(TAG, "Failed to set sensitivity for gate %d", gate);
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 命令间隔
    }
    
    return success;
}

bool RadarModule::ReadParameters() {
    if (!is_running_) {
        ESP_LOGE(TAG, "Radar module is not running");
        return false;
    }
    
    // 读取参数命令: 0x0061, 无命令值
    auto command = CreateCommand(0x0061);
    
    int len = uart_write_bytes(uart_num_, command.data(), command.size());
    if (len != command.size()) {
        ESP_LOGE(TAG, "Failed to send read parameters command");
        return false;
    }
    
    ESP_LOGI(TAG, "Read parameters command sent");
    vTaskDelay(pdMS_TO_TICKS(100)); // 等待雷达响应
    return true;
}

// 添加距离滤波函数实现
uint16_t RadarModule::FilterDistance(uint16_t raw_distance) {
    // 将新的距离值添加到滤波缓冲区
    distance_filter_buffer_[filter_index_] = raw_distance;
    filter_index_ = (filter_index_ + 1) % DISTANCE_FILTER_SIZE;
    
    // 如果滤波器还未完全填充，直接返回原始值
    if (!filter_initialized_) {
        if (filter_index_ == 0) {
            filter_initialized_ = true;
        }
        return raw_distance;
    }
    
    // 计算移动平均值
    uint32_t sum = 0;
    for (size_t i = 0; i < DISTANCE_FILTER_SIZE; i++) {
        sum += distance_filter_buffer_[i];
    }
    
    return static_cast<uint16_t>(sum / DISTANCE_FILTER_SIZE);
}

// ... existing code ...

void RadarModule::SetPersonState(PersonState state) {
    ESP_LOGI(TAG, "Setting person state to: %d", static_cast<int>(state));
    
    current_person_state_ = state;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 根据设置的状态更新相关时间戳
    switch (state) {
        case PersonState::NOBODY:
            target_detected_ = false;
            is_sitting_ = false;
            is_short_distance_ = false;
            first_detection_time_ = 0;
            last_detection_time_ = 0;
            sitting_start_time_ = 0;
            short_distance_start_time_ = 0;
            break;
            
        case PersonState::PERSON_COMING:
            target_detected_ = true;
            first_detection_time_ = current_time;
            last_detection_time_ = current_time;
            break;
            
        case PersonState::PERSON_PRESENT:
            target_detected_ = true;
            if (first_detection_time_ == 0) {
                first_detection_time_ = current_time;
            }
            last_detection_time_ = current_time;
            break;
            
        case PersonState::PERSON_LEAVING:
            target_detected_ = false;
            break;
            
        case PersonState::SHORT_HOLD:
            target_detected_ = true;
            is_short_distance_ = true;
            short_distance_start_time_ = current_time;
            last_detection_time_ = current_time;
            break;
            
        case PersonState::LONG_SITTING:
            target_detected_ = true;
            is_sitting_ = true;
            sitting_start_time_ = current_time;
            last_detection_time_ = current_time;
            break;
    }
    
    // 触发状态变化回调
    if (person_state_callback_) {
        person_state_callback_(current_person_state_);
    }
}

void RadarModule::ResetState() {
    ESP_LOGI(TAG, "Resetting radar state to initial values");
    SetPersonState(PersonState::NOBODY);
    
    // 重置滤波器
    filter_initialized_ = false;
    filter_index_ = 0;
    memset(distance_filter_buffer_, 0, sizeof(distance_filter_buffer_));
}