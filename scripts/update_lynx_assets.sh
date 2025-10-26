#!/bin/bash
# ========================================
# Lynx Assets 快速更新工具
# LynxAura - ALichuangTest开发板
# ========================================

# 配置参数（可根据需要修改）
PORT="/dev/ttyUSB0"
BAUD="921600"
INPUT_DIR="sdcard"
OUTPUT_FILE="build/lynx_assets.bin"

echo "========================================"
echo "Lynx Assets 快速更新工具"
echo "========================================"
echo ""
echo "此脚本仅更新assets分区，不影响固件"
echo "适用于开发阶段快速测试GIF/OGG/配置文件"
echo ""

# 检查输入目录
if [ ! -d "$INPUT_DIR" ]; then
    echo "错误: 找不到输入目录 $INPUT_DIR"
    exit 1
fi

# 创建输出目录
mkdir -p build

# 步骤1: 打包资源
echo "========================================"
echo "步骤 1/3: 打包资源文件"
echo "========================================"
echo "正在扫描 $INPUT_DIR 目录..."
echo ""

python3 scripts/pack_lynx_assets.py $INPUT_DIR $OUTPUT_FILE
if [ $? -ne 0 ]; then
    echo ""
    echo "错误: 打包失败！"
    exit 1
fi

echo ""
echo "✓ 打包完成"

# 步骤2: 烧录assets分区
echo ""
echo "========================================"
echo "步骤 2/3: 烧录assets分区"
echo "========================================"
echo "端口: $PORT"
echo "波特率: $BAUD"
echo "地址: 0x800000 (assets分区)"
echo ""
echo "预计时间: 约60秒"
echo "请勿断开USB连接..."
echo ""

esptool.py --port $PORT --baud $BAUD write_flash 0x800000 $OUTPUT_FILE
if [ $? -ne 0 ]; then
    echo ""
    echo "错误: 烧录失败！"
    echo ""
    echo "可能的原因:"
    echo "  1. 设备未连接或端口错误（当前: $PORT）"
    echo "  2. 设备未进入下载模式"
    echo "  3. 权限不足"
    echo ""
    echo "解决方案:"
    echo "  1. 检查设备连接: ls /dev/ttyUSB* 或 ls /dev/ttyACM*"
    echo "  2. 添加用户到dialout组: sudo usermod -a -G dialout \$USER"
    echo "  3. 重新登录或使用 sudo"
    exit 1
fi

echo ""
echo "✓ 烧录完成"

# 步骤3: 重启设备
echo ""
echo "========================================"
echo "步骤 3/3: 重启设备"
echo "========================================"
echo ""

esptool.py --port $PORT run
if [ $? -ne 0 ]; then
    echo "警告: 自动重启失败，请手动按RESET键"
fi

echo ""
echo "========================================"
echo "更新完成！"
echo "========================================"
echo ""
echo "设备已重启，可以查看串口监视器验证更新："
echo "  idf.py -p $PORT monitor"
echo ""
echo "或使用以下命令检查日志："
echo "  esptool.py --port $PORT --baud 115200 read_flash 0x800000 0x100 temp.bin"
echo "  hexdump -C temp.bin"
echo "  (应该看到 \"LYNX\" Magic)"
echo ""
