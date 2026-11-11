/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <algorithm>
#include <cstring>
#include <esp_pthread.h>

#include "application.h"
#include "display.h"
#include "board.h"
#include "settings.h"  // 添加这行
#include "boards/common/dual_network_board.h"  // 添加这行
#include <algorithm>  // 添加这行用于std::transform
#include "oled_emotion_task.h"
#define TAG "MCP"

#define DEFAULT_TOOLCALL_STACK_SIZE 6144

McpServer::McpServer() {
}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    AddTool("self.get_device_status",
        "Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n"
        "Use this tool for: \n"
        "1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n"
        "2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            return board.GetDeviceStatusJson();
        });

    // 添加退出聊天工具（英文版）
    // 在 AddCommonTools 方法中修改退出工具
    AddTool("self.chat.exit",
        "Exit the current chat session and return the device to idle state.\n"
        "Use this tool when:\n"
        "1. User explicitly asks to end the conversation (e.g., '退下','goodbye', 'stop talking', 'exit chat')\n"
        "2. User wants to stop the current interaction\n"
        "3. Need to terminate the chat session for any reason\n"
        "This will stop listening, abort speaking if in progress, close audio channel, and set device to idle state.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            ESP_LOGI("MCP", "Executing chat exit, current device state: %d", app.GetDeviceState());
            
            // 延迟执行退出，确保响应能够发送
            auto exit_task = [](void* param) {
                vTaskDelay(pdMS_TO_TICKS(500));  // 等待响应发送完成
                Application::GetInstance().ExitChat();
                auto& oled = OledEmotionTask::GetInstance();
                oled.SendEmotion("ok",5);
                vTaskDelete(NULL);
            };
            
            xTaskCreate(exit_task, "exit_task", 2048, NULL, 5, NULL);
            
            ESP_LOGI("MCP", "Chat exit scheduled");
            return "Chat session will exit shortly. Device will return to idle state.";
        });
    
    // 中文版也需要同样修改
    AddTool("退出聊天",
        "退出当前聊天会话并将设备返回到空闲状态。\n"
        "使用场景：\n"
        "1. 用户明确要求结束对话（如'退下'、'再见'、'停止说话'、'退出聊天'）\n"
        "2. 用户想要停止当前交互\n"
        "3. 需要因任何原因终止聊天会话\n"
        "这将停止监听，中止正在进行的说话，关闭音频通道，并将设备设置为空闲状态。",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            ESP_LOGI("MCP", "执行聊天退出，当前设备状态: %d", app.GetDeviceState());
            
            // 延迟执行退出，确保响应能够发送
            auto exit_task = [](void* param) {
                vTaskDelay(pdMS_TO_TICKS(500));  // 等待响应发送完成
                Application::GetInstance().ExitChat();
                auto& oled = OledEmotionTask::GetInstance();
                oled.SendEmotion("ok",5);
                vTaskDelete(NULL);
            };
            
            xTaskCreate(exit_task, "exit_task", 2048, NULL, 5, NULL);
            
            ESP_LOGI("MCP", "聊天退出已安排");
            return "聊天会话即将退出。设备将返回空闲状态。";
        });

    AddTool("self.get_device_number",
        "Get the device registration number from flash storage.\n"
        "Returns the device number if registered, or indicates if device is not registered.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            Settings settings("device", false);
            int device_num = settings.GetInt("device_num", -1);
            
            if (device_num == -1) {
                return "设备未注册";
            }
            
            return "设备编码是:"+std::to_string(device_num);
        });
    
    AddTool("self.audio_speaker.set_volume", 
        "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }), 
        [&board](const PropertyList& properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());

            auto& oled = OledEmotionTask::GetInstance();
            oled.SendEmotion("ok",5);
            return true;
        });
    
    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness",
            "Set the brightness of the screen.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [backlight](const PropertyList& properties) -> ReturnValue {
                uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>());
                backlight->SetBrightness(brightness, true);
                auto& oled = OledEmotionTask::GetInstance();
                oled.SendEmotion("ok",5);
                return true;
            });
    }

    auto display = board.GetDisplay();
    if (display && !display->GetTheme().empty()) {
        AddTool("self.screen.set_theme",
            "Set the theme of the screen. The theme can be `light` or `dark`.",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                display->SetTheme(properties["theme"].value<std::string>().c_str());
                return true;
            });
    }

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
            "Take a photo and explain it. Use this tool after the user asks you to see something.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                if (!camera->Capture()) {
                    return "{\"success\": false, \"message\": \"Failed to capture photo\"}";
                }
                auto question = properties["question"].value<std::string>();
                return camera->Explain(question);
            });
    }

    // 添加网络切换工具（在退出工具之后添加）
    // 通过板子类型字符串检查是否支持网络切换
    std::string board_type = board.GetBoardType();
    bool supports_network_switch = (board_type.find("ml307") != std::string::npos || 
                                   board_type.find("dual") != std::string::npos ||
                                   board_type.find("meijing") != std::string::npos);
    
    // 替换现有的网络切换工具实现
    if (supports_network_switch) {
    AddTool("self.network.switch",
        "Function:\nSwitch the network type\n"
        "Use when:User requests to switch network type",
        PropertyList(),  // 移除所有参数
        [&board](const PropertyList& properties) -> ReturnValue {

            
            // 尝试转换为 DualNetworkBoard
            DualNetworkBoard* dual_board = static_cast<DualNetworkBoard*>(&board);
            if (!dual_board) {
                return "网络切换功能不可用。";
            }
            
            // 获取当前网络类型
            NetworkType current_type = dual_board->GetNetworkType();
            std::string current_network = (current_type == NetworkType::WIFI) ? "WiFi" : "4G";
            std::string target_network = (current_type == NetworkType::WIFI) ? "4G" : "WiFi";
            
            ESP_LOGI("MCP", "Network switch: %s -> %s", current_network.c_str(), target_network.c_str());
            
            // 创建延迟任务来执行网络切换，确保响应能够发送
            auto switch_task = [](void* param) {
                vTaskDelay(pdMS_TO_TICKS(1000));  // 等待1秒确保响应发送
                DualNetworkBoard* board_ptr = static_cast<DualNetworkBoard*>(param);
                board_ptr->SwitchNetworkType();

                auto& oled = OledEmotionTask::GetInstance();
                oled.SendEmotion("ok",5);

                vTaskDelete(NULL);
            };
            
            xTaskCreate(switch_task, "network_switch_task", 2048, dual_board, 5, NULL);
            
            return "网络切换成功！正在从" + current_network + "切换到" + target_network + "，设备将在2秒后重启以应用更改。";
        });
    
    }
    
    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); }) != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s", tool->name().c_str());
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
        }
        GetToolsList(id_int, cursor_str);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        auto stack_size = cJSON_GetObjectItem(params, "stackSize");
        if (stack_size != nullptr && !cJSON_IsNumber(stack_size)) {
            ESP_LOGE(TAG, "tools/call: Invalid stackSize");
            ReplyError(id_int, "Invalid stackSize");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments, stack_size ? stack_size->valueint : DEFAULT_TOOLCALL_STACK_SIZE);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";

    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }
        
        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;
        ++it;
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments, int stack_size) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Start a task to receive data with stack size
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.thread_name = "tool_call";
    cfg.stack_size = stack_size;
    cfg.prio = 1;
    esp_pthread_set_cfg(&cfg);

    // Use a thread to call the tool to avoid blocking the main thread
    tool_call_thread_ = std::thread([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
    tool_call_thread_.detach();
}