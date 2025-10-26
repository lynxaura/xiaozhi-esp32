#!/usr/bin/env python3
"""
Lynx Assets 打包工具 (v1.1)
将 GIF/OGG/JSON 文件打包为可烧录到 assets 分区的二进制文件。

格式说明：
Header (16 bytes)
  [0-3]   Magic:  'LYNX'
  [4-7]   Version: uint32 LE (1)
  [8-11]  File Count: uint32 LE
  [12-15] CRC32 (Data Area): uint32 LE

Index (64 bytes × N)
  name[48] (UTF-8, zero-padded)
  offset[4] (uint32, relative to data area start)
  size[4]   (uint32)
  type[1]   (0=GIF,1=OGG,2=JSON)
  flags[1]  (bitmask: 0x01=READONLY, 0x02=WRITABLE)
  reserved[2]
  reserved2[4]

Data Area
  Raw concatenation of all file contents
"""

import argparse
import os
import struct
import sys
import zlib
from pathlib import Path

MAGIC_LYNX = b'LYNX'
VERSION = 1
ENTRY_SIZE = 64
NAME_SIZE = 48

ASSET_TYPE_GIF = 0
ASSET_TYPE_OGG = 1
ASSET_TYPE_JSON = 2

ASSET_FLAG_READONLY = 0x01
ASSET_FLAG_WRITABLE = 0x02


def detect_type_flags(path: Path):
    ext = path.suffix.lower()
    if ext == '.gif':
        return ASSET_TYPE_GIF, ASSET_FLAG_READONLY
    if ext == '.ogg':
        return ASSET_TYPE_OGG, ASSET_FLAG_READONLY
    if ext == '.json':
        # JSON 配置默认标记为可写（可运行时覆盖到 NVS）
        return ASSET_TYPE_JSON, ASSET_FLAG_WRITABLE
    return None, None


def collect_files(input_dir: Path):
    files = []
    for root, _, filenames in os.walk(input_dir):
        for filename in filenames:
            p = Path(root) / filename
            ext = p.suffix.lower()
            if ext not in ('.gif', '.ogg', '.json'):
                continue
            t, fl = detect_type_flags(p)
            if t is None:
                continue
            rel = p.relative_to(input_dir)
            # 使用相对路径（去掉扩展名，统一 / 分隔）作为 name
            name = str(rel.with_suffix('')).replace('\\', '/')
            size = p.stat().st_size
            files.append({
                'name': name,
                'path': str(p),
                'size': size,
                'type': t,
                'flags': fl,
            })

    files.sort(key=lambda f: f['name'])
    return files


def calculate_offsets(files):
    off = 0
    for f in files:
        f['offset'] = off
        off += f['size']
    return off


def human_size(num):
    return f"{num:,} bytes ({num/1024/1024:.2f} MB)"


def pack_assets(input_dir: str, output_file: str):
    input_path = Path(input_dir)
    if not input_path.is_dir():
        print(f"错误: 输入目录不存在: {input_dir}")
        sys.exit(1)

    print(f"扫描目录: {input_dir}")
    files = collect_files(input_path)
    if not files:
        print("错误: 未找到任何GIF/OGG/JSON文件")
        sys.exit(1)

    print(f"\n找到 {len(files)} 个文件:")
    total_size = 0
    for f in files:
        print(f"  + {f['name']} ({f['size']:,} bytes)")
        total_size += f['size']

    data_size = calculate_offsets(files)
    header_size = 16
    index_size = len(files) * ENTRY_SIZE
    partition_size = header_size + index_size + data_size

    print(f"\n总数据大小: {human_size(total_size)}")
    print(f"分区总大小: {human_size(partition_size)}")

    # 目标分区为 8MB
    if partition_size > 8 * 1024 * 1024:
        print("\n警告: 分区大小超过 8MB 限制！")
        print("建议：")
        print("  1. 压缩GIF（减少帧/尺寸/色深）")
        print("  2. 降低OGG比特率或移除冗余音频")
        sys.exit(1)

    print(f"\n开始写入: {output_file}")
    with open(output_file, 'wb') as f:
        # Header
        f.write(MAGIC_LYNX)                          # [0-3]
        f.write(struct.pack('<I', VERSION))          # [4-7]
        f.write(struct.pack('<I', len(files)))       # [8-11]
        crc_pos = f.tell()
        f.write(struct.pack('<I', 0))                # [12-15] placeholder

        # Index
        for file in files:
            name_bytes = file['name'].encode('utf-8')[:NAME_SIZE]
            name_bytes = name_bytes.ljust(NAME_SIZE, b'\0')
            f.write(name_bytes)                      # [0-47]
            f.write(struct.pack('<I', file['offset']))  # [48-51]
            f.write(struct.pack('<I', file['size']))    # [52-55]
            f.write(struct.pack('<B', file['type']))    # [56]
            f.write(struct.pack('<B', file['flags']))   # [57]
            f.write(struct.pack('<H', 0))               # [58-59] reserved
            f.write(struct.pack('<I', 0))               # [60-63] reserved2

        # Data Area
        data_start = f.tell()
        for i, file in enumerate(files, 1):
            print(f"  [{i}/{len(files)}] 写入 {file['name']} ...", end='\r')
            with open(file['path'], 'rb') as src:
                f.write(src.read())

        print("\n\n数据写入完成，计算CRC32校验和...")
        f.seek(data_start)
        data = f.read(data_size)
        crc32_value = zlib.crc32(data) & 0xFFFFFFFF
        f.seek(crc_pos)
        f.write(struct.pack('<I', crc32_value))

    final_size = os.path.getsize(output_file)
    print("\n✓ 打包完成!")
    print(f"  输出文件: {output_file}")
    print(f"  文件大小: {human_size(final_size)}")
    print(f"  CRC32: 0x{crc32_value:08X}")
    print("\n下一步: 使用以下命令烧录")
    print(f"  esptool.py --port COM3 --baud 921600 write_flash 0x800000 {output_file}")


def main():
    parser = argparse.ArgumentParser(description='Pack Lynx assets into a binary file for Flash.')
    parser.add_argument('input_dir', help='Input directory (e.g. ./sdcard)')
    parser.add_argument('output_file', help='Output .bin file')
    args = parser.parse_args()

    pack_assets(args.input_dir, args.output_file)


if __name__ == '__main__':
    main()

