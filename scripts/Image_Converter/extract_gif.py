#!/usr/bin/env python3
"""
提取LVGL C数组中的GIF数据并保存为GIF文件

用法:
    python extract_gif.py [c文件路径] [输出目录]
    
示例:
    python extract_gif.py ../managed_components/txp666__otto-emoji-gif-component/src/happy.c ./gifs
    python extract_gif.py ../managed_components/txp666__otto-emoji-gif-component/src/ ./gifs
"""

import os
import re
import sys
import glob

def extract_gif_data(c_file_path):
    """
    从C文件中提取GIF数据数组
    """
    try:
        with open(c_file_path, 'r', encoding='utf-8') as f:
            content = f.read()
    except UnicodeDecodeError:
        # 尝试其他编码
        try:
            with open(c_file_path, 'r', encoding='latin-1') as f:
                content = f.read()
        except Exception as e:
            print(f"无法读取文件 {c_file_path}: {e}")
            return None, None
    
    # 提取数组名称
    array_name_match = re.search(r'uint8_t\s+([a-zA-Z0-9_]+)_map\[\]\s*=\s*\{', content)
    if not array_name_match:
        print(f"在文件 {c_file_path} 中找不到GIF数据数组")
        return None, None
    
    array_name = array_name_match.group(1)
    
    # 提取数组数据
    array_data_match = re.search(r'uint8_t\s+[a-zA-Z0-9_]+_map\[\]\s*=\s*\{([^}]+)\};', content, re.DOTALL)
    if not array_data_match:
        print(f"在文件 {c_file_path} 中找不到完整的GIF数据")
        return None, None
    
    # 处理数组数据
    array_data_str = array_data_match.group(1)
    # 移除注释
    array_data_str = re.sub(r'//.*?\n', '\n', array_data_str)
    # 移除换行符和空格
    array_data_str = re.sub(r'\s+', '', array_data_str)
    # 分割为单独的十六进制值
    hex_values = array_data_str.split(',')
    # 移除空值
    hex_values = [v for v in hex_values if v]
    # 转换为字节
    try:
        data_bytes = bytes([int(v, 16) for v in hex_values])
        return array_name, data_bytes
    except ValueError as e:
        print(f"转换数据时出错: {e}")
        return None, None

def save_gif_file(data_bytes, output_path):
    """
    将字节数据保存为GIF文件
    """
    with open(output_path, 'wb') as f:
        f.write(data_bytes)
    print(f"GIF文件已保存到: {output_path}")

def process_file(c_file_path, output_dir):
    """
    处理单个C文件
    """
    array_name, data_bytes = extract_gif_data(c_file_path)
    if array_name and data_bytes:
        # 创建输出目录
        os.makedirs(output_dir, exist_ok=True)
        
        # 保存GIF文件
        output_path = os.path.join(output_dir, f"{array_name}.gif")
        save_gif_file(data_bytes, output_path)
        return True
    return False

def process_directory(dir_path, output_dir):
    """
    处理目录中的所有.c文件
    """
    c_files = glob.glob(os.path.join(dir_path, "*.c"))
    success_count = 0
    
    for c_file in c_files:
        if process_file(c_file, output_dir):
            success_count += 1
    
    return success_count

def main():
    # 设置默认路径
    default_src_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 
                               "managed_components", "txp666__otto-emoji-gif-component", "src")
    default_output_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "extracted_gifs")
    
    # 解析命令行参数
    if len(sys.argv) > 1:
        src_path = sys.argv[1]
    else:
        src_path = default_src_path
        print(f"使用默认源路径: {src_path}")
    
    if len(sys.argv) > 2:
        output_dir = sys.argv[2]
    else:
        output_dir = default_output_dir
        print(f"使用默认输出目录: {output_dir}")
    
    # 检查路径是否存在
    if not os.path.exists(src_path):
        print(f"路径不存在: {src_path}")
        return
    
    # 处理文件或目录
    if os.path.isdir(src_path):
        count = process_directory(src_path, output_dir)
        print(f"成功处理了 {count} 个GIF文件")
    else:
        if process_file(src_path, output_dir):
            print("文件处理成功")
        else:
            print("文件处理失败")

if __name__ == "__main__":
    main()