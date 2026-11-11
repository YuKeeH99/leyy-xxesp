#include "background_task.h"

#include <esp_log.h>
#include <esp_task_wdt.h>

#define TAG "BackgroundTask"

BackgroundTask::BackgroundTask(uint32_t stack_size) {
    // 将背景任务固定在CPU0，避免与4G UART相关任务在CPU1上抢占导致RX饥饿
    xTaskCreatePinnedToCore([](void* arg) {
        BackgroundTask* task = (BackgroundTask*)arg;
        task->BackgroundTaskLoop();
    }, "background_task", stack_size, this, 4, &background_task_handle_, 0);
}

BackgroundTask::~BackgroundTask() {
    if (background_task_handle_ != nullptr) {
        vTaskDelete(background_task_handle_);
    }
}

void BackgroundTask::Schedule(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_tasks_ >= 30) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (free_sram < 10000) {
            ESP_LOGW(TAG, "active_tasks_ == %u, free_sram == %u", active_tasks_.load(), free_sram);
        }
    }
    active_tasks_++;
    background_tasks_.emplace_back([this, cb = std::move(callback)]() {
        cb();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_tasks_--;
            if (background_tasks_.empty() && active_tasks_ == 0) {
                condition_variable_.notify_all();
            }
        }
    });
    condition_variable_.notify_all();
}

void BackgroundTask::WaitForCompletion() {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_variable_.wait(lock, [this]() {
        return background_tasks_.empty() && active_tasks_ == 0;
    });
}

void BackgroundTask::BackgroundTaskLoop() {
    ESP_LOGI(TAG, "background_task started");
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_variable_.wait(lock, [this]() { return !background_tasks_.empty(); });
        
        std::list<std::function<void()>> tasks = std::move(background_tasks_);
        lock.unlock();

        for (auto& task : tasks) {
            task();
            // 让出CPU片刻以避免持续有任务时饥饿IDLE任务
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}
