import sys
import re

def extract_hex_data_from_c_file(file_path):
    # 读取文件内容
    with open(file_path, 'r') as file:
        content = file.read()
    
    # 使用正则表达式查找数组数据
    pattern = r'{(.*?)};'  # 注意：这个正则表达式假定数组以{}包围，且以;结尾。根据实际情况调整。
    match = re.search(pattern, content, re.DOTALL)
    if match:
        hex_data_str = match.group(1)  # 获取匹配的数组内容部分
        hex_data = hex_data_str.split(',')  # 分割成单独的16进制数
        hex_data = [int(byte.strip(), 16) for byte in hex_data]  # 将字符串转换为整数列表
        return hex_data
    else:
        raise ValueError("No valid hex array found in the file.")
 
def write_to_binary_file(hex_data, output_file_path):
    # 将数据写入二进制文件
    with open(output_file_path, 'wb') as file:
        for byte in hex_data:
            file.write(byte.to_bytes(1, 'little'))  # 将每个字节写入文件，大端字节序
 

filename = sys.argv[1]
print(f"read {filename}.h")
print(filename)

input_file = f'{filename}.h'  # C语言文件路径
output_file = f'{filename}.bin'  # 二进制输出文件路径
print(input_file)
hex_data = extract_hex_data_from_c_file(input_file)
write_to_binary_file(hex_data, output_file)
