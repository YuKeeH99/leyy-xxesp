import argparse

import numpy as np
import wave
import struct

def generate_wifi_config_audio(ssid, password, output):
    # 音频参数 - 与ESP32代码完全匹配
    SAMPLE_RATE = 6400  # 8kHz采样率
    MARK_FREQ = 1800    # 标记频率 (逻辑1)
    SPACE_FREQ = 1500   # 空格频率 (逻辑0)
    BIT_RATE = 100      # 比特率
    
    # 计算每个比特的采样数
    samples_per_bit = SAMPLE_RATE // BIT_RATE
    
    # WiFi凭据由命令行传入，避免把真实密码写进仓库
    wifi_data = f"{ssid}\n{password}"
    
    # 计算校验和（与ESP32代码匹配）
    checksum = sum(ord(c) for c in wifi_data) % 256
    complete_data = wifi_data + chr(checksum)
    
    print(f"WiFi数据: '{wifi_data}'")
    print(f"校验和: {checksum} (0x{checksum:02x})")
    print(f"完整数据: '{complete_data}'")
    
    # 转换为比特
    data_bits = []
    for char in complete_data:
        byte_val = ord(char)
        # LSB first (最低位优先)
        for i in range(8):
            data_bits.append((byte_val >> i) & 1)
    
    # 标识符（与ESP32代码匹配）
    start_identifier = [0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0]
    end_identifier = [0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0]
    
    # 构建完整的比特序列
    # 增加更长的前导码以确保同步
    preamble = [0] * 80  # 80个0作为前导码
    silence_bits = [0] * 40  # 40个0作为静音
    
    all_bits = preamble + start_identifier + data_bits + end_identifier + silence_bits
    
    print(f"总比特数: {len(all_bits)}")
    print(f"前导码: {preamble[:16]}...")
    print(f"开始标识符: {start_identifier}")
    print(f"数据比特数: {len(data_bits)}")
    print(f"结束标识符: {end_identifier}")
    
    # 生成音频信号
    audio_samples = []
    
    for bit in all_bits:
        freq = MARK_FREQ if bit == 1 else SPACE_FREQ
        
        # 生成正弦波
        for i in range(samples_per_bit):
            t = i / SAMPLE_RATE
            sample = 0.3 * np.sin(2 * np.pi * freq * t)  # 降低音量到0.3
            audio_samples.append(sample)
    
    # 转换为16位整数
    audio_data = []
    for sample in audio_samples:
        int_sample = int(sample * 32767)
        int_sample = max(-32768, min(32767, int_sample))
        audio_data.append(int_sample)
    
    # 保存为WAV文件
    filename = output
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)  # 单声道
        wav_file.setsampwidth(2)  # 16位
        wav_file.setframerate(SAMPLE_RATE)
        
        # 写入音频数据
        for sample in audio_data:
            wav_file.writeframes(struct.pack('<h', sample))
    
    print(f"音频文件已生成: {filename}")
    print(f"音频时长: {len(audio_data) / SAMPLE_RATE:.2f} 秒")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="生成声波配网 WAV 文件")
    parser.add_argument("--ssid", required=True, help="WiFi 名称")
    parser.add_argument("--password", required=True, help="WiFi 密码")
    parser.add_argument("--output", default="wifi_config.wav", help="输出 WAV 文件")
    args = parser.parse_args()
    generate_wifi_config_audio(args.ssid, args.password, args.output)