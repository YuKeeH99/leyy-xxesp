#ifndef _RADAR_COMMON_H_
#define _RADAR_COMMON_H_

#include <functional>
#include <cstdint>

// 人员状态枚举
enum class PersonState {
    NOBODY,         // 无人
    PERSON_COMING,  // 来人（从无人到有人的过渡状态）
    PERSON_PRESENT, // 有人（稳定存在）
    PERSON_LEAVING, // 人走（从有人到无人的过渡状态）
    SHORT_HOLD,     // 短暂保持状态
    LONG_SITTING    // 久坐状态
};

// 雷达状态回调函数类型定义（两个雷达模块共用）
typedef std::function<void(uint8_t target_state, uint16_t moving_distance, 
                          uint8_t moving_energy, uint16_t static_distance, 
                          uint8_t static_energy, uint16_t detection_distance)> RadarStatusCallback;

// 人员状态变化回调函数类型定义（两个雷达模块共用）
typedef std::function<void(PersonState state)> PersonStateCallback;

// 久坐检测回调函数类型定义
typedef std::function<void()> LongSittingCallback;

#endif // _RADAR_COMMON_H_