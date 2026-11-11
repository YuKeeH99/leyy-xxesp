#include "radar_2410d.h"
#include <esp_log.h>
#include <string.h>
#include <algorithm>  // 添加这个头文件

#define TAG "Radar2410D"
#define RADAR_BUFFER_SIZE 1024

// 2410D协议帧头和帧尾
#define FRAME_HEADER_0 0xFD
#define FRAME_HEADER_1 0xFC
#define FRAME_HEADER_2 0xFB
#define FRAME_HEADER_3 0xFA
#define FRAME_TAIL_0 0x04
#define FRAME_TAIL_1 0x03
#define FRAME_TAIL_2 0x02
#define FRAME_TAIL_3 0x01

// 工程模式数据帧头
#define ENG_FRAME_HEADER_0 0xF4
#define ENG_FRAME_HEADER_1 0xF3
#define ENG_FRAME_HEADER_2 0xF2
#define ENG_FRAME_HEADER_3 0xF1
#define ENG_FRAME_TAIL_0 0xF8
#define ENG_FRAME_TAIL_1 0xF7
#define ENG_FRAME_TAIL_2 0xF6
#define ENG_FRAME_TAIL_3 0xF5

Radar2410D::Radar2410D()
    : uart_num_(UART_NUM_2),
      task_handle_(nullptr),
      is_running_(false),
      current_state_(Radar2410DState::NO_TARGET),
      latest_distance_(0),
      latest_energy_(0),
      motion_detected_(false),
      last_motion_time_(0),
      motion_timeout_ms_(5000),
      // 添加新状态变量的初始化
      current_person_state_(PersonState::NOBODY),
      coming_threshold_ms_(15000),
      leaving_threshold_ms_(15000),
      long_sitting_threshold_ms_(2 * 60 * 1000),
      first_detection_time_(0),
      last_detection_time_(0),
      no_target_start_time_(0),  // 添加初始化
      target_detected_(false),
      is_sitting_(false),
      sitting_start_time_(0),
      short_hold_threshold_ms_(3000),  // 默认5秒
      short_distance_start_time_(0),
      is_short_distance_(false),
      filter_index_(0),
      filter_initialized_(false) {
    // 初始化滤波缓冲区
    memset(distance_filter_buffer_, 0, sizeof(distance_filter_buffer_));
}

Radar2410D::~Radar2410D() {
    Stop();
}

bool Radar2410D::Init(uart_port_t uart_num, int tx_pin, int rx_pin, int baud_rate) {
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
    ESP_LOGI(TAG, "Installing UART driver for 2410D radar module");
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
    
    ESP_LOGI(TAG, "2410D radar module initialized successfully");



    return true;
}

void Radar2410D::Start() {
    ESP_LOGI(TAG, "Starting 2410D radar module");
    if (is_running_) {
        ESP_LOGW(TAG, "2410D radar module is already running");
        return;
    }
    
    is_running_ = true;
    
    // 创建雷达数据处理任务
    xTaskCreate(RadarTask, "radar_2410d_task", 8192, this, 5, &task_handle_);

    // ------------------- 新增：读取当前模式，判断是否需要配置 -------------------
 /*   OutputMode current_mode = ReadCurrentOutputMode();
    bool need_config = (current_mode != OUTPUT_MODE_ENGINEERING);
    
    if (!need_config) {
        ESP_LOGI(TAG, "Already in Engineering Mode, skip configuration");
        ESP_LOGI(TAG, "2410D radar module started");
        return; // 已在工程模式，直接跳过配置
    }
   */ 
    // 配置非工程模式（修正后流程）
    //vTaskDelay(pdMS_TO_TICKS(1000)); // 延长启动等待，确保模块就绪
    ESP_LOGI(TAG, "Configuring engineering mode...");
    for (int retry = 0; retry < 3; retry++) {
        ESP_LOGI(TAG, "Configuration attempt %d", retry + 1);
        
        if (EnableConfiguration()) {
            //vTaskDelay(pdMS_TO_TICKS(2000)); // 等待进入配置模式（至少2秒）
            
            // 步骤1：设置工程模式（修正后命令）
            if (SetDataOutputMode(false)) {
               // vTaskDelay(pdMS_TO_TICKS(1000)); // 等待模式切换完成
                
                // 步骤2：关键新增——保存参数（手册5.3节强制要求）
                if (SaveParameters()) {
                    ESP_LOGI(TAG, "Parameters saved successfully (engineering mode)");
                    //vTaskDelay(pdMS_TO_TICKS(1000));
                } else {
                    ESP_LOGE(TAG, "Failed to save parameters");
                    continue; // 保存失败则重试
                }
                
                // 步骤3：读取固件版本（可选，仅验证）
                if (ReadFirmwareVersion()) {
                   // vTaskDelay(pdMS_TO_TICKS(1000));
                }
                
                // 步骤4：结束配置模式
                if (EndConfiguration()) {
                    //vTaskDelay(pdMS_TO_TICKS(1000)); // 等待退出配置模式
                    ESP_LOGI(TAG, "Configuration completed successfully");
                    break; // 成功则退出重试
                } else {
                    ESP_LOGE(TAG, "Failed to end configuration");
                }
            } else {
                ESP_LOGE(TAG, "Failed to set engineering mode");
            }
        } else {
            ESP_LOGE(TAG, "Failed to enable configuration");
        }
        
        if (retry < 2) {
            ESP_LOGW(TAG, "Retrying configuration...");
            vTaskDelay(pdMS_TO_TICKS(1000)); // 重试间隔延长至3秒
        }
    }
    
    ESP_LOGI(TAG, "2410D radar module started");
}

void Radar2410D::Stop() {
    if (!is_running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping 2410D radar module...");
    is_running_ = false;
    
    // 等待任务自然结束
    if (task_handle_ != nullptr) {
        for (int i = 0; i < 10; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (eTaskGetState(task_handle_) == eDeleted) {
                break;
            }
        }
        task_handle_ = nullptr;
    }
    
    // 卸载UART驱动
    uart_driver_delete(uart_num_);
    
    ESP_LOGI(TAG, "2410D radar module stopped");
}

void Radar2410D::RadarTask(void* pvParameters) {
    Radar2410D* radar = static_cast<Radar2410D*>(pvParameters);
    uint8_t* data = (uint8_t*)malloc(RADAR_BUFFER_SIZE);
    
    ESP_LOGI(TAG, "2410D radar task started");
    
    while (radar->is_running_) {
        // 读取UART数据
        int len = uart_read_bytes(radar->uart_num_, data, RADAR_BUFFER_SIZE - 1, pdMS_TO_TICKS(150));
        
        if (len < 0) {
            ESP_LOGE(TAG, "UART read error: %d", len);
            uart_flush(radar->uart_num_);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        if (len == 0) {
            // 检查运动超时
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (radar->motion_detected_ && 
                (current_time - radar->last_motion_time_) > radar->motion_timeout_ms_) {
                radar->motion_detected_ = false;
                if (radar->motion_callback_) {
                    radar->motion_callback_(false, 0);
                }
            }
            continue;
        }
        
        if (len > 0) {
            // 添加数据接收日志
            //ESP_LOGI(TAG, "Received %d bytes from radar", len);
            // 解析雷达数据帧
            radar->ParseRadarFrame(data, len);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    free(data);
    ESP_LOGI(TAG, "2410D radar task ended");
    vTaskDelete(NULL);
}

void Radar2410D::ParseRadarFrame(const uint8_t* bytes, size_t length) {
    if (!bytes || length == 0) {
        ESP_LOGE(TAG, "ParseRadarFrame bytes is null or length is 0");
        return;
    }
    
    // 将新数据添加到不完整数据缓冲区
    incomplete_data_.append(reinterpret_cast<const char*>(bytes), length);
    
    // 处理完整的行数据（以\r\n结尾）
    size_t pos = 0;
    while ((pos = incomplete_data_.find("\r\n")) != std::string::npos) {
        std::string line = incomplete_data_.substr(0, pos);
        incomplete_data_.erase(0, pos + 2); // 移除已处理的行和\r\n
        
        // 处理这一行数据
        ProcessTextLine(line);
    }
    
    // 如果缓冲区太大，清空以防止内存泄漏和栈溢出
    if (incomplete_data_.size() > 512) {  // 减少从1024到512
        ESP_LOGW(TAG, "Incomplete data buffer too large (%d bytes), clearing", incomplete_data_.size());
        incomplete_data_.clear();
    }
    /*
    ESP_LOGI(TAG, "Raw data received (%d bytes):", length);
    for (size_t i = 0; i < length; i++) {
        printf("%02X ", bytes[i]);
    }
    printf("\n");
    
    // 同时打印ASCII格式（如果是文本）
    ESP_LOGI(TAG, "ASCII: %.*s", length, (char*)bytes);
    */
    // 如果数据长度足够大，可能是二进制数据，尝试解析二进制帧
    if (length >= 8) {
        // 查找工程模式数据帧 (F4 F3 F2 F1)
        for (size_t i = 0; i <= length - 8; i++) {
            if (bytes[i] == ENG_FRAME_HEADER_0 && bytes[i+1] == ENG_FRAME_HEADER_1 && 
                bytes[i+2] == ENG_FRAME_HEADER_2 && bytes[i+3] == ENG_FRAME_HEADER_3) {
                
                if (i + 5 >= length) break;
                
                // 获取帧数据长度 (小端序)
                uint16_t data_length = bytes[i+4] | (bytes[i+5] << 8);
                
                if (data_length > 1024 || data_length < 3) continue;
                
                size_t frame_end = i + 6 + data_length + 4;
                if (frame_end > length) continue;
                
                size_t tail_pos = i + 6 + data_length;
                if (bytes[tail_pos] == ENG_FRAME_TAIL_0 && bytes[tail_pos+1] == ENG_FRAME_TAIL_1 && 
                    bytes[tail_pos+2] == ENG_FRAME_TAIL_2 && bytes[tail_pos+3] == ENG_FRAME_TAIL_3) {
                    
                    HandleEngineeringModeData(&bytes[i+6], data_length);
                    break;
                }
            }
        }
        
        // 查找命令模式数据帧 (FD FC FB FA)
        for (size_t i = 0; i <= length - 8; i++) {
            if (bytes[i] == FRAME_HEADER_0 && bytes[i+1] == FRAME_HEADER_1 && 
                bytes[i+2] == FRAME_HEADER_2 && bytes[i+3] == FRAME_HEADER_3) {
                
                if (i + 5 >= length) break;
                
                uint16_t data_length = bytes[i+4] | (bytes[i+5] << 8);
                
                if (data_length > 1024) continue;
                
                size_t frame_end = i + 6 + data_length + 4;
                if (frame_end > length) continue;
                
                size_t tail_pos = i + 6 + data_length;
                if (bytes[tail_pos] == FRAME_TAIL_0 && bytes[tail_pos+1] == FRAME_TAIL_1 && 
                    bytes[tail_pos+2] == FRAME_TAIL_2 && bytes[tail_pos+3] == FRAME_TAIL_3) {
                    
                    // 处理ACK或其他命令响应
                    ESP_LOGI(TAG, "Received command response, length: %d", data_length);
                    // 解析8字节响应数据（以ReadFirmwareVersion为例，手册规定响应格式）
                    if (data_length == 8) {
                        // 响应数据格式：[命令字(2字节)][状态码(2字节)][固件版本(4字节)]
                        uint16_t cmd = bytes[i+6] | (bytes[i+7] << 8); // 小端解析命令字
                        uint16_t status = bytes[i+8] | (bytes[i+9] << 8); // 状态码（0x0000为成功）
                        uint32_t firmware_version = bytes[i+10] | (bytes[i+11] << 8) | (bytes[i+12] << 16) | (bytes[i+13] << 24);
                        
                        ESP_LOGI(TAG, "Response details - Command: 0x%04X, Status: 0x%04X (0=success), Firmware Version: 0x%08X", 
                                cmd, status, (unsigned int)firmware_version);
                        
                        // 验证状态：若状态非0，提示配置失败
                        if (status != 0x0000) {
                            ESP_LOGE(TAG, "Command failed! Status code: 0x%04X", status);
                        }
                    } else {
                        // 打印原始响应数据（用于调试其他命令）
                        char resp_debug[256] = {0};
                        for (size_t j = 0; j < data_length; j++) {
                            sprintf(resp_debug + j*3, "%02X ", bytes[i+6 + j]);
                        }
                        ESP_LOGD(TAG, "Response data: %s", resp_debug);
                    }
                    break;

                }
            }
        }
    }
}

void Radar2410D::HandleEngineeringModeData(const uint8_t* data, size_t length) {
    if (length < 3) {
        ESP_LOGW(TAG, "Engineering mode data too short: %d bytes", length);
        return;
    }
    
    uint8_t detection_result = data[0];
    uint16_t target_distance = data[1] | (data[2] << 8); // 小端序
    
    // 如果数据长度足够，解析能量值
    uint8_t energy = 0;
    if (length >= 4) {
        energy = data[3];
    }
    
    // 添加详细的工程模式数据日志
    ESP_LOGI(TAG, "[ENGINEERING MODE] Detection: 0x%02X, Distance: %d cm, Energy: %d, Data length: %d", 
             detection_result, target_distance, energy, length);
    
    Radar2410DState new_state;
    switch (detection_result) {
        case 0x00:
            new_state = Radar2410DState::NO_TARGET;
            break;
        case 0x01:
            new_state = Radar2410DState::MOVING_TARGET;
            break;
        case 0x02:
            new_state = Radar2410DState::STATIC_TARGET;
            break;
        /*case 0x03:
            new_state = Radar2410DState::BOTH_TARGETS;
            break;*/
        default:
            new_state = Radar2410DState::NO_TARGET;
            break;
    }
    
    // 更新当前状态
    current_state_ = new_state;
    latest_distance_ = target_distance;
    latest_energy_ = energy;
    
    // 运动检测逻辑
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool has_motion = (new_state == Radar2410DState::MOVING_TARGET || new_state == Radar2410DState::BOTH_TARGETS);
    
    if (has_motion) {
        last_motion_time_ = current_time;
        if (!motion_detected_) {
            motion_detected_ = true;
            if (motion_callback_) {
                motion_callback_(true, target_distance);
            }
        }
    }
    
    // 调用雷达状态回调
    if (radar_callback_) {
        radar_callback_(new_state, target_distance, energy);
    }
    
    ESP_LOGI(TAG, "Engineering mode data processed successfully");
}

std::vector<uint8_t> Radar2410D::CreateCommand(uint16_t command, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> frame;
    
    // 帧头
    frame.push_back(FRAME_HEADER_0);
    frame.push_back(FRAME_HEADER_1);
    frame.push_back(FRAME_HEADER_2);
    frame.push_back(FRAME_HEADER_3);
    
    // 数据长度 (小端序)
    uint16_t data_length = 2 + data.size(); // 命令字2字节 + 数据
    frame.push_back(data_length & 0xFF);
    frame.push_back((data_length >> 8) & 0xFF);
    
    // 命令字 (小端序)
    frame.push_back(command & 0xFF);
    frame.push_back((command >> 8) & 0xFF);
    
    // 数据
    frame.insert(frame.end(), data.begin(), data.end());
    
    // 帧尾
    frame.push_back(FRAME_TAIL_0);
    frame.push_back(FRAME_TAIL_1);
    frame.push_back(FRAME_TAIL_2);
    frame.push_back(FRAME_TAIL_3);
    
    return frame;
}

bool Radar2410D::SendCommand(const std::vector<uint8_t>& command) {
    if (!is_running_) {
        ESP_LOGE(TAG, "2410D radar module is not running");
        return false;
    }
    
    // 打印发送的命令内容（用于调试）
   /* ESP_LOGI(TAG, "Sending command frame (hex):");
    for (size_t i = 0; i < command.size(); i++) {
        printf("%02X ", command[i]);
        if ((i+1) % 8 == 0) printf("\n"); // 每8字节换行，方便查看
    }
    printf("\n");
    */
    int len = uart_write_bytes(uart_num_, command.data(), command.size());
    if (len != command.size()) {
        ESP_LOGE(TAG, "Failed to send command, expected %d bytes, sent %d bytes", command.size(), len);
        return false;
    }
    
    // 确保数据发送完成
    uart_wait_tx_done(uart_num_, pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "Command sent successfully");
    return true;
}

// 配置接口实现
bool Radar2410D::EnableConfiguration() {
    ESP_LOGI(TAG, "Enabling configuration mode");
    auto command = CreateCommand(0x00FF, {0x01, 0x00});
    return SendCommand(command);
}

bool Radar2410D::EndConfiguration() {
    ESP_LOGI(TAG, "Ending configuration mode");
    auto command = CreateCommand(0x00FE, {});
    return SendCommand(command);
}

bool Radar2410D::ReadFirmwareVersion() {
    ESP_LOGI(TAG, "Reading firmware version");
    auto command = CreateCommand(0x0000, {});
    return SendCommand(command);
}

bool Radar2410D::QueryAutoThresholdProgress() {
    ESP_LOGI(TAG, "Querying auto threshold progress");
    auto command = CreateCommand(0x000A, {});
    return SendCommand(command);
}

bool Radar2410D::SaveParameters() {
    ESP_LOGI(TAG, "Saving parameters");
    auto command = CreateCommand(0x00FD, {});
    return SendCommand(command);
}

bool Radar2410D::AutoGainAdjustment() {
    ESP_LOGI(TAG, "Starting auto gain adjustment");
    auto command = CreateCommand(0x00EE, {});
    return SendCommand(command);
}

bool Radar2410D::ReadCurrentDeviceParameters() {
    ESP_LOGI(TAG, "Reading current device parameters");
    auto command = CreateCommand(0x0008, {});
    return SendCommand(command);
}

bool Radar2410D::StartAutoThresholdGeneration(uint16_t trigger_coeff, uint16_t hold_coeff, uint16_t micro_motion_coeff) {
    ESP_LOGI(TAG, "Starting auto threshold generation");
    
    std::vector<uint8_t> data;
    // 触发门限生成系数 (小端序)
    data.push_back(trigger_coeff & 0xFF);
    data.push_back((trigger_coeff >> 8) & 0xFF);
    // 保持门限生成系数 (小端序)
    data.push_back(hold_coeff & 0xFF);
    data.push_back((hold_coeff >> 8) & 0xFF);
    // 微动门限生成系数 (小端序)
    data.push_back(micro_motion_coeff & 0xFF);
    data.push_back((micro_motion_coeff >> 8) & 0xFF);
    
    auto command = CreateCommand(0x0009, data);
    return SendCommand(command);
}
/*
// 在其他配置函数后添加
bool Radar2410D::SetDataOutputMode(bool engineering_mode) {
    ESP_LOGI(TAG, "Setting data output mode to %s", engineering_mode ? "engineering" : "normal");
    
    std::vector<uint8_t> data;
    if (engineering_mode) {
        // 工程模式：0x00000004 (小端序)
        data = {0x04, 0x00, 0x00, 0x00};
    } else {
        // 正常工作模式：0x00000064 (小端序)
        data = {0x64, 0x00, 0x00, 0x00};
    }
    
    auto command = CreateCommand(0x0012, data);
    return SendCommand(command);
}
*/
bool Radar2410D::SetDataOutputMode(bool engineering_mode) {
    ESP_LOGI(TAG, "Setting data output mode to %s", engineering_mode ? "engineering" : "normal");
    
    std::vector<uint8_t> data;
    // 1. 先添加“命令值0x0000”（2字节，手册5.2.8节强制要求）
    data.push_back(0x00); 
    data.push_back(0x00); 
    // 2. 再添加“参数值”（4字节，小端序）
    if (engineering_mode) {
        data.push_back(0x04); // 工程模式：0x00000004（小端）
        data.push_back(0x00);
        data.push_back(0x00);
        data.push_back(0x00);
    } else {
        data.push_back(0x64); // 正常模式：0x00000064（小端）
        data.push_back(0x00);
        data.push_back(0x00);
        data.push_back(0x00);
    }
    
    // 此时data.size()=6（2字节命令值+4字节参数值），数据长度=2（命令字）+6=8字节（符合手册）
    auto command = CreateCommand(0x0012, data); 
    return SendCommand(command);
}

void Radar2410D::ProcessTextLine(const std::string& line) {
    if (line.empty()) {
        return;
    }
    
    // 添加频率控制，减少日志输出
    static uint32_t last_log_time = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool should_log = (current_time - last_log_time) > 10000;
    
    if (should_log) {
#ifdef DEBUG_VERSON
        ESP_LOGI(TAG, "Processing text line: %s", line.c_str());
#endif
        last_log_time = current_time;
    }
    
    // 直接在原字符串上操作，避免创建临时对象
    const char* trimmed_start = line.c_str();
    size_t trimmed_len = line.length();
    
    // 跳过前导空白字符
    while (trimmed_len > 0 && (*trimmed_start == ' ' || *trimmed_start == '\t')) {
        trimmed_start++;
        trimmed_len--;
    }
    
    // 跳过尾随空白字符
    while (trimmed_len > 0 && (trimmed_start[trimmed_len-1] == ' ' || trimmed_start[trimmed_len-1] == '\t')) {
        trimmed_len--;
    }
    
    if (trimmed_len == 3 && strncmp(trimmed_start, "OFF", 3) == 0) {
        // 无目标检测
        if (should_log) 
        {
            ESP_LOGI(TAG, "#######No target detected (OFF)");
        }
        current_state_ = Radar2410DState::NO_TARGET;
        motion_detected_ = false;
        
        // 调用统一的状态处理函数
        HandleTargetStateChange(0x00, 0, 0, 0, 0, 0);
        return;
    }
    
    // 检查是否是distance格式
    if (trimmed_len > 9 && strncmp(trimmed_start, "distance:", 9) == 0) {
        // 提取距离值 - 避免创建子字符串
        const char* distance_str = trimmed_start + 9;
        char* endptr;
        long distance_val = strtol(distance_str, &endptr, 10);
        
        if (endptr != distance_str && distance_val >= 0 && distance_val <= 65535) {
            uint16_t distance = (uint16_t)distance_val;
            
            // 添加距离过滤逻辑：大于200cm（2米）的检测按无人处理
            if (distance > 260) {
                if (should_log) {
                    ESP_LOGI(TAG, "Target detected at distance %d cm (>2m), treating as no target", distance);
                }
                // 按无目标处理
                current_state_ = Radar2410DState::NO_TARGET;
                motion_detected_ = false;
                HandleTargetStateChange(0x00, 0, 0, 0, 0, 0);
                return;
            }
            
            if (should_log) {
                ESP_LOGI(TAG, "Target detected at distance: %d cm", distance);
            }
            
            current_state_ = Radar2410DState::MOVING_TARGET;
            latest_distance_ = distance;
            motion_detected_ = true;
            last_motion_time_ = current_time;
            
            // 调用统一的状态处理函数（假设为运动目标）
            HandleTargetStateChange(0x01, distance, 50, 0, 0, distance);
            
            // 保留旧的回调函数用于向后兼容
            if (radar_callback_) {
                radar_callback_(current_state_, distance, 0);
            }
            if (motion_callback_) {
                motion_callback_(true, distance);
            }
        } else {
            ESP_LOGW(TAG, "Failed to parse distance value");
        }
        return;
    }
}
/*
无人（NOBODY）
    ↓（首次检测到目标）
来人（PERSON_COMING）
    ↓（持续15秒）
在场（PERSON_PRESENT）
    ├→ 久坐（LONG_SITTING）（静坐2分钟）
    ├→ 近距离停留（SHORT_HOLD）（<20cm持续5秒）
    ↓（目标消失）
离开（PERSON_LEAVING）
    ↓（持续15秒无目标）
无人（NOBODY）

*/


void Radar2410D::HandleTargetStateChange(uint8_t target_state, uint16_t moving_distance, uint8_t moving_energy, 
                                        uint16_t static_distance, uint8_t static_energy, uint16_t detection_distance) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 判断目标类型
    bool has_moving_target = (target_state == 0x01 || target_state == 0x03);
    bool has_static_target = (target_state == 0x00);
    
    // 调用雷达状态回调
    if (radar_status_callback_) {
        radar_status_callback_(target_state, moving_distance, moving_energy, 
                              static_distance, static_energy, detection_distance);
    }
    
    // 使用静态变量跟踪静态目标的持续时间
    static uint32_t static_only_start_time = 0;
    static uint32_t static_duration = 0;
    
    // 更新目标检测状态
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
    
    // 静态目标持续时间超过5秒，且没有明显运动，认为可能是环境物体而非人
    bool likely_environment_object = static_duration > 2000 && !significant_movement;
    
    // 更新目标检测状态
    target_detected_ = significant_movement || (has_static_target && !likely_environment_object);
    
    // 人员状态维护逻辑
    if (target_detected_) {
        // 有目标检测到，重置无目标计时
        no_target_start_time_ = 0;
        
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
            
            // 如果处于人走状态，但检测到明显运动，则转回有人状态
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
            // 从有目标变为无目标，开始无目标计时
            no_target_start_time_ = current_time;
            
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
            if (no_target_start_time_ > 0 && (current_time - no_target_start_time_ >= leaving_threshold_ms_)) {
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
    
    // 久坐检测逻辑
    if (is_sitting_ && sitting_start_time_ > 0 && (current_person_state_ == PersonState::PERSON_PRESENT || current_person_state_ == PersonState::SHORT_HOLD)) {
        // 判断是否处于静态状态：只有静态目标，或者运动目标能量很低
        //bool is_still_sitting = has_static_target && 
        //                      (!has_moving_target || moving_energy < 20);
        bool is_still_sitting = true;
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
        }/* else if (has_moving_target && moving_energy >= 30) {
            // 检测到明显运动，重置久坐计时
            continuous_sitting_time = 0;
            last_sitting_check_time = current_time;
        }*/
    }
    
    //uint16_t filtered_distance = FilterDistance(detection_distance); // 滤波后的距离
    // SHORT_HOLD状态检测逻辑
    //uint16_t current_distance = filtered_distance; // 使用检测距离作为判断依据
    if (detection_distance > 0 && detection_distance <= 40) { // 距离小于20cm (单位可能是mm)
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
                    ESP_LOGI(TAG, "Person state changed: -> SHORT_HOLD (distance < 20cm for %" PRIu32 " ms)", short_distance_duration);
                    
                    // 触发人员状态回调
                    if (person_state_callback_) {
                        person_state_callback_(current_person_state_);
                    }
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

// 在文件末尾添加距离滤波函数实现
uint16_t Radar2410D::FilterDistance(uint16_t raw_distance) {
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
// 新增：读取当前数据输出模式
// Add this include at the top of the file if not already present
#include <inttypes.h>

OutputMode Radar2410D::ReadCurrentOutputMode() {
    ESP_LOGI(TAG, "Reading current output mode...");
    OutputMode current_mode = OUTPUT_MODE_UNKNOWN;
    
    // 1. 发送“读取当前设备参数”命令（0x0008）
    auto command = CreateCommand(0x0008, {});
    if (!SendCommand(command)) {
        ESP_LOGE(TAG, "Failed to send ReadCurrentDeviceParameters command");
        return current_mode;
    }
    
    // 2. 等待模块响应（手册建议等待500-1000ms，确保响应接收完整）
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 3. 读取UART接收缓冲区，解析响应（注：需临时读取，避免干扰主任务的ParseRadarFrame）
    uint8_t resp_buffer[256] = {0};
    int resp_len = uart_read_bytes(uart_num_, resp_buffer, sizeof(resp_buffer)-1, pdMS_TO_TICKS(500));
    
    if (resp_len <= 0) {
        ESP_LOGE(TAG, "No response for ReadCurrentDeviceParameters command");
        return current_mode;
    }
    
    // 4. 解析响应帧（需符合“命令响应帧格式”：FD FC FB FA 帧头 + 数据长度 + ... + 帧尾）
    for (size_t i = 0; i <= resp_len - 8; i++) {
        // 匹配命令响应帧头（FD FC FB FA）和帧尾（04 03 02 01）
        if (resp_buffer[i] == FRAME_HEADER_0 && resp_buffer[i+1] == FRAME_HEADER_1 && 
            resp_buffer[i+2] == FRAME_HEADER_2 && resp_buffer[i+3] == FRAME_HEADER_3) {
            
            // 解析数据长度（小端序）
            uint16_t data_length = resp_buffer[i+4] | (resp_buffer[i+5] << 8);
            size_t frame_end = i + 6 + data_length + 4; // 帧头(4) + 长度(2) + 数据(n) + 帧尾(4)
            if (frame_end > resp_len) continue;
            
            // 匹配帧尾
            size_t tail_pos = i + 6 + data_length;
            if (resp_buffer[tail_pos] == FRAME_TAIL_0 && resp_buffer[tail_pos+1] == FRAME_TAIL_1 && 
                resp_buffer[tail_pos+2] == FRAME_TAIL_2 && resp_buffer[tail_pos+3] == FRAME_TAIL_3) {
                
                // 5. 提取“数据输出模式”字段（响应数据第12-15字节，32位小端序）
                // 响应数据格式（手册P15）：[命令字(2)][状态码(2)][参数1(4)][参数2(4)]...[数据输出模式(4)]...
                if (data_length >= 16) { // 确保数据长度足够包含“数据输出模式”字段（第12-15字节）
                    uint32_t output_mode_val = resp_buffer[i+6 + 12] |    // 第12字节（最低位）
                                              (resp_buffer[i+6 + 13] << 8) |  // 第13字节
                                              (resp_buffer[i+6 + 14] << 16) | // 第14字节
                                              (resp_buffer[i+6 + 15] << 24); // 第15字节（最高位）
                    
                    ESP_LOGI(TAG, "Current output mode value: 0x%08" PRIx32, output_mode_val);
                    // 根据值判断模式
                    if (output_mode_val == 0x00000004) {
                        current_mode = OUTPUT_MODE_ENGINEERING;
                        ESP_LOGI(TAG, "Current mode: Engineering Mode");
                    } else if (output_mode_val == 0x00000064) {
                        current_mode = OUTPUT_MODE_NORMAL;
                        ESP_LOGI(TAG, "Current mode: Normal Mode");
                    } else {
                        ESP_LOGW(TAG, "Unknown output mode value: 0x%08" PRIx32, output_mode_val);
                    }
                } else {
                    ESP_LOGE(TAG, "Response data too short (length: %d) to get output mode", data_length);
                }
                break; // 找到一个完整响应帧即可
            }
        }
    }
    
    return current_mode;
}

// ... existing code ...

void Radar2410D::SetPersonState(PersonState state) {
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
            no_target_start_time_ = current_time;
            sitting_start_time_ = 0;
            short_distance_start_time_ = 0;
            break;
            
        case PersonState::PERSON_COMING:
            target_detected_ = true;
            first_detection_time_ = current_time;
            last_detection_time_ = current_time;
            no_target_start_time_ = 0;
            break;
            
        case PersonState::PERSON_PRESENT:
            target_detected_ = true;
            if (first_detection_time_ == 0) {
                first_detection_time_ = current_time;
            }
            last_detection_time_ = current_time;
            no_target_start_time_ = 0;
            break;
            
        case PersonState::PERSON_LEAVING:
            target_detected_ = false;
            no_target_start_time_ = current_time;
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

void Radar2410D::ResetState() {
    ESP_LOGI(TAG, "Resetting radar state to initial values");
    SetPersonState(PersonState::NOBODY);
    
    // 重置其他状态变量
    current_state_ = Radar2410DState::NO_TARGET;
    latest_distance_ = 0;
    latest_energy_ = 0;
    motion_detected_ = false;
    last_motion_time_ = 0;
    
    // 重置滤波器
    filter_initialized_ = false;
    filter_index_ = 0;
    memset(distance_filter_buffer_, 0, sizeof(distance_filter_buffer_));
}