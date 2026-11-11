#include <esp_log.h>
#include <string.h>
#include <cJSON.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_app_format.h>
#include <esp_http.h>
#include <netdb.h>           // DNS解析函数
#include <sys/socket.h>      // socket相关函数
#include <arpa/inet.h>       // inet_ntoa函数
#include <esp_timer.h>       // 计时器函数
#include "board.h"
#include "wifi_station.h"
#include "system_info.h"     // Add this line for SystemInfo::GetMacAddress()
static const char *TAG = "HTTPS_TEST";

// 检查网络连接状态
bool is_network_connected() {
    auto& board = Board::GetInstance();
    
    // 通过板卡类型字符串判断网络类型
    std::string board_type = board.GetBoardType();
    
    // 检查网络状态图标来判断网络是否连接
    const char* network_icon = board.GetNetworkStateIcon();
    if (network_icon == nullptr) {
        return false;
    }
    
    // 如果网络状态图标不是离线状态，认为网络已连接
    return (strlen(network_icon) > 0);
}

// 更可靠的网络连通性测试
bool is_network_really_ready() {
    ESP_LOGI(TAG, "Testing network connectivity...");
    
    // 首先检查网络接口是否已启动
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        netif = esp_netif_get_handle_from_ifkey("PPP_DEF");
    }
    
    if (netif == nullptr) {
        ESP_LOGW(TAG, "No active network interface found");
        return false;
    }
    
    // 检查是否有IP地址
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get IP info");
        return false;
    }
    
    if (ip_info.ip.addr == 0) {
        ESP_LOGW(TAG, "No IP address assigned");
        return false;
    }
    
    ESP_LOGI(TAG, "IP address: " IPSTR, IP2STR(&ip_info.ip));
    
    // 等待一段时间确保TCP/IP栈稳定
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 使用与 ota.cc 相同的 HTTP 客户端进行简单的连通性测试
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return false;
    }
    
    // 设置超时时间
    http->SetTimeout(10000);
    
    // 设置请求头
    http->SetHeader("User-Agent", "ESP32-HTTPS-Test/1.0");
    http->SetHeader("Accept", "application/json");
    
    // 设置空的请求内容
    std::string empty_content = "";
    http->SetContent(std::move(empty_content));
    
    // 发送 GET 请求
    if (!http->Open("GET", "http://httpbin.org/get")) {
        ESP_LOGW(TAG, "Failed to open HTTP connection for connectivity test");
        delete http;
        return false;
    }
    
    int status_code = http->GetStatusCode();
    std::string response = http->ReadAll();
    http->Close();
    delete http;
    
    if (status_code == 200 && !response.empty()) {
        ESP_LOGI(TAG, "Network connectivity test passed");
        return true;
    } else {
        ESP_LOGW(TAG, "Network connectivity test failed: status=%d, response_len=%d", 
                status_code, response.length());
        return false;
    }
}

#ifdef __cplusplus
extern "C" {
#endif

// HTTPS GET测试函数
void test_https_get() {
    ESP_LOGI(TAG, "开始HTTPS GET测试...");
    
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return;
    }
    
    // 设置超时时间
    http->SetTimeout(10000);
    
    // 设置请求头
    http->SetHeader("User-Agent", "ESP32-HTTPS-Test/1.0");
    http->SetHeader("Accept", "application/json");
    
    // 设置空的请求内容
    std::string empty_content = "";
    http->SetContent(std::move(empty_content));
    
    // 发送 GET 请求
    if (!http->Open("GET", "https://api.contextai.pro/api/v1/me/get")) {
        ESP_LOGE(TAG, "Failed to open HTTPS connection");
        delete http;
        return;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code == 200) {
        std::string response = http->ReadAll();
        ESP_LOGI(TAG, "HTTPS GET Status = %d, content_length = %d", status_code, response.length());
        ESP_LOGI(TAG, "Response: %s", response.c_str());
    } else {
        ESP_LOGE(TAG, "HTTPS GET请求失败: status_code = %d", status_code);
    }
    
    http->Close();
    delete http;
}

// HTTPS POST测试函数
void test_https_post() {
    ESP_LOGI(TAG, "开始HTTPS POST测试...");
    
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return;
    }
    
    // 设置超时时间
    http->SetTimeout(10000);
    
    // 设置请求头
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("User-Agent", "ESP32-HTTPS-Test/1.0");
    http->SetHeader("Accept", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    //http->SetHeader("Client-Id", board.GetUuid());
    //http->SetHeader("User-Agent", std::string(BOARD_NAME "/") + app_desc->version);
    //http->SetHeader("Accept-Language", "zh-CN");
    http->SetHeader("Content-Type", "application/json");
    // 设置POST数据
    std::string post_data = "{\"message\":\"Hello from ESP32\",\"timestamp\":1234567890}";
 http->SetContent(std::move(post_data));
    // auto& board = Board::GetInstance();
//std::string data = board.GetJson();
   // std::string method = data.length() > 0 ? "POST" : "GET";
   // http->SetContent(std::move(data));

   std::string url = "http://voice.contextai.pro/api/v1/me/post";
    ESP_LOGI(TAG, "url = %s", url.c_str());
    
    // 发送 POST 请求
    if (!http->Open("POST", url.c_str())) {
        ESP_LOGE(TAG, "Failed to open HTTPS connection");
        delete http;
        return;
    }
    
    int status_code = http->GetStatusCode();
    if (status_code == 200) {
        std::string response = http->ReadAll();
        ESP_LOGI(TAG, "HTTPS POST Status = %d, content_length = %d", status_code, response.length());
        ESP_LOGI(TAG, "Response: %s", response.c_str());
    } else {
        ESP_LOGE(TAG, "HTTPS POST请求失败: status_code = %d", status_code);
    }
    
    http->Close();
    delete http;
}
// 添加OTA服务器测试函数
void test_ota_server_connection() {
    ESP_LOGI(TAG, "开始OTA服务器连接测试...");
    
    auto http = Board::GetInstance().CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return;
    }
    
    // 设置超时时间
    http->SetTimeout(15000);  // 增加超时时间
    
    // 使用与OTA相同的请求头配置
    auto& board = Board::GetInstance();
    //auto app_desc = esp_app_get_description();
    
    //http->SetHeader("Activation-Version", "1");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    //http->SetHeader("Client-Id", board.GetUuid());
    //http->SetHeader("User-Agent", std::string(BOARD_NAME "/") + app_desc->version);
    //http->SetHeader("Accept-Language", "zh-CN");
    http->SetHeader("Content-Type", "application/json");
    
    // 设置请求内容
    std::string data = board.GetJson();
    std::string method = data.length() > 0 ? "POST" : "GET";
    ESP_LOGI(TAG, "data = %s", data.c_str());
    http->SetContent(std::move(data));
     ESP_LOGI(TAG, "method = %s", method.c_str());
    ESP_LOGI(TAG, "data = %s", data.c_str());
    // 测试OTA服务器
    //std::string ota_url = "https://api.tenclass.net/xiaozhi/ota/";
    std::string ota_url = "http://iot.contextai.pro/xiaozhi/ota/";
    if (!http->Open(method, ota_url)) {
        ESP_LOGE(TAG, "Failed to open OTA server connection");
        delete http;
        return;
    }
    ESP_LOGI(TAG, "ota_url = %s", ota_url.c_str());
    int status_code = http->GetStatusCode();
    ESP_LOGI(TAG, "OTA服务器测试 Status = %d", status_code);
    
    if (status_code > 0) {
        std::string response = http->ReadAll();
        ESP_LOGI(TAG, "OTA服务器响应长度: %d", response.length());
        if (response.length() < 500) {  // 只打印较短的响应
            ESP_LOGI(TAG, "OTA服务器响应: %s", response.c_str());
        }
    }
    
    http->Close();
    delete http;
}
// 新增：使用ESP32原生HTTP客户端的OTA服务器测试函数
void test_ota_server_connection2() {
    ESP_LOGI(TAG, "开始OTA服务器连接测试2（使用ESP32原生HTTP客户端）...");
    
    // 直接使用ESP32原生HTTP客户端
    EspHttp* http = new EspHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create ESP HTTP client");
        return;
    }
    
    // 设置超时时间
    http->SetTimeout(15000);
    
    // 使用与OTA相同的请求头配置
    auto& board = Board::GetInstance();
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Content-Type", "application/json");
    
    // 设置请求内容
    std::string data = board.GetJson();
    std::string method = data.length() > 0 ? "POST" : "GET";
    ESP_LOGI(TAG, "Request method: %s", method.c_str());
    ESP_LOGI(TAG, "Request data length: %d", data.length());
    http->SetContent(std::move(data));
    
    // 测试OTA服务器
    std::string ota_url = "https://iot2.contextai.com.cn/xiaozhi/ota/";
    if (!http->Open(method, ota_url)) {
        ESP_LOGE(TAG, "Failed to open OTA server connection");
        delete http;
        return;
    }
    
    int status_code = http->GetStatusCode();
    ESP_LOGI(TAG, "OTA服务器测试2 Status = %d", status_code);
    
    if (status_code == 200) {
        size_t content_length = http->GetBodyLength();
        ESP_LOGI(TAG, "OTA服务器响应长度: %d", content_length);
        
        if (content_length > 0 && content_length < 2048) {
            std::string response = http->ReadAll();
            ESP_LOGI(TAG, "OTA服务器响应: %s", response.c_str());
        }
    } else if (status_code > 0) {
        size_t content_length = http->GetBodyLength();
        ESP_LOGI(TAG, "OTA服务器响应长度: %d", content_length);
        
        if (content_length > 0 && content_length < 2048) {
            std::string response = http->ReadAll();
            ESP_LOGI(TAG, "OTA服务器响应: %s", response.c_str());
        }
    }
    
    http->Close();
    delete http;
}

// DNS服务器信息测试
void test_dns_server_info() {
    ESP_LOGI(TAG, "获取DNS服务器信息...");
    
    // 获取当前网络接口
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        netif = esp_netif_get_handle_from_ifkey("PPP_DEF");
    }
    
    if (netif != nullptr) {
        esp_netif_dns_info_t dns_info;
        
        // 获取主DNS服务器
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            ESP_LOGI(TAG, "主DNS服务器: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }
        
        // 获取备用DNS服务器
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info) == ESP_OK) {
            ESP_LOGI(TAG, "备用DNS服务器: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }
        
        // 获取第三DNS服务器
        if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_FALLBACK, &dns_info) == ESP_OK) {
            ESP_LOGI(TAG, "第三DNS服务器: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }
    } else {
        ESP_LOGW(TAG, "无法获取网络接口信息");
    }
}

// DNS解析测试函数
void test_dns_resolution() {
    ESP_LOGI(TAG, "开始DNS解析测试...");
    
    // 测试的域名列表
    const char* test_domains[] = {
        "www.baidu.com",
        "api.contextai.pro",
        "voice.contextai.pro",
        "httpbin.org",
        "www.google.com",
        "github.com"
    };
    
    int domain_count = sizeof(test_domains) / sizeof(test_domains[0]);
    int success_count = 0;
    
    for (int i = 0; i < domain_count; i++) {
        const char* domain = test_domains[i];
        ESP_LOGI(TAG, "正在解析域名: %s", domain);
        
        // 方法1: 使用 gethostbyname (传统方法)
        struct hostent* host_entry = gethostbyname(domain);
        if (host_entry != NULL) {
            struct in_addr addr;
            addr.s_addr = *((unsigned long *)host_entry->h_addr_list[0]);
            ESP_LOGI(TAG, "✓ %s -> %s (gethostbyname)", domain, inet_ntoa(addr));
            success_count++;
        } else {
            ESP_LOGW(TAG, "✗ %s 解析失败 (gethostbyname)", domain);
        }
        
        // 方法2: 使用 getaddrinfo (推荐方法)
        struct addrinfo hints, *result;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;  // IPv4
        hints.ai_socktype = SOCK_STREAM;
        
        int ret = getaddrinfo(domain, NULL, &hints, &result);
        if (ret == 0 && result != NULL) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)result->ai_addr;
            ESP_LOGI(TAG, "✓ %s -> %s (getaddrinfo)", domain, inet_ntoa(addr_in->sin_addr));
            
            // 显示所有解析到的IP地址
            struct addrinfo* current = result;
            int ip_count = 0;
            while (current != NULL && ip_count < 5) {  // 最多显示5个IP
                if (current->ai_family == AF_INET) {
                    struct sockaddr_in* addr_in = (struct sockaddr_in*)current->ai_addr;
                    ESP_LOGI(TAG, "  IP[%d]: %s", ip_count, inet_ntoa(addr_in->sin_addr));
                    ip_count++;
                }
                current = current->ai_next;
            }
            
            freeaddrinfo(result);
        } else {
            // 修复：使用错误码而不是 gai_strerror
            const char* error_msg;
            switch (ret) {
                case EAI_NONAME:
                    error_msg = "域名不存在";
                    break;
                case EAI_AGAIN:
                    error_msg = "临时DNS解析失败";
                    break;
                case EAI_FAIL:
                    error_msg = "永久DNS解析失败";
                    break;
                case EAI_MEMORY:
                    error_msg = "内存不足";
                    break;
               /* case EAI_SYSTEM:
                    error_msg = "系统错误";
                    break;*/
                default:
                    error_msg = "未知错误";
                    break;
            }
            ESP_LOGW(TAG, "✗ %s 解析失败 (getaddrinfo): %s (错误码: %d)", domain, error_msg, ret);
        }
        
        // 添加延迟避免DNS查询过于频繁
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    ESP_LOGI(TAG, "DNS解析测试完成: %d/%d 成功", success_count, domain_count);
    
    // 额外测试：解析当前使用的DNS服务器
    test_dns_server_info();
}

// 高级DNS测试：测试DNS解析性能
void test_dns_performance() {
    ESP_LOGI(TAG, "开始DNS解析性能测试...");
    
    const char* test_domain = "www.baidu.com";
    const int test_count = 5;
    
    for (int i = 0; i < test_count; i++) {
        uint32_t start_time = esp_timer_get_time() / 1000;  // 转换为毫秒
        
        struct addrinfo hints, *result;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        int ret = getaddrinfo(test_domain, NULL, &hints, &result);
        
        uint32_t end_time = esp_timer_get_time() / 1000;
        uint32_t duration = end_time - start_time;
        
        if (ret == 0 && result != NULL) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)result->ai_addr;
            ESP_LOGI(TAG, "解析 #%d: %s -> %s (耗时: %lu ms)", 
                    i+1, test_domain, inet_ntoa(addr_in->sin_addr), duration);
            freeaddrinfo(result);
        } else {
            ESP_LOGW(TAG, "解析 #%d: 失败 (耗时: %lu ms, 错误码: %d)", i+1, duration, ret);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
// 主测试函数
void run_https_tests() {
    ESP_LOGI(TAG, "开始HTTPS测试程序...");
    
    // 等待网络连接建立，最多等待60秒
    int retry_count = 0;
    const int max_retries = 60;
    
    while (!is_network_connected() && retry_count < max_retries) {
        ESP_LOGI(TAG, "等待网络连接建立... (%d/%d)", retry_count + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry_count++;
    }
    
    if (!is_network_connected()) {
        ESP_LOGE(TAG, "网络连接超时，取消HTTPS测试");
        return;
    }
    
    ESP_LOGI(TAG, "网络连接已建立，进行连通性测试...");
    
    // 进行更可靠的网络连通性测试
 /*   if (!is_network_really_ready()) {
        ESP_LOGE(TAG, "网络连通性测试失败，取消HTTPS测试");
        return;
    }
    
    ESP_LOGI(TAG, "网络连通性测试通过，额外等待2秒确保连接稳定...");
    vTaskDelay(pdMS_TO_TICKS(2000));
  */  
    ESP_LOGI(TAG, "开始HTTPS测试，再等待3秒确保TCP/IP栈完全就绪...");
   // vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 执行GET测试
    //test_https_get();
    
    // 等待一段时间再执行下一个测试
   // vTaskDelay(pdMS_TO_TICKS(2000));
     // 运行DNS解析测试

    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 运行DNS性能测试

    vTaskDelay(pdMS_TO_TICKS(2000));

    // 执行POST测试
    test_https_post();
    



    // 等待一段时间再测试OTA服务器
    vTaskDelay(pdMS_TO_TICKS(2000));


    // 测试OTA服务器连接
    test_ota_server_connection();
    
    ESP_LOGI(TAG, "HTTPS测试程序完成");
}

#ifdef __cplusplus
}
#endif

