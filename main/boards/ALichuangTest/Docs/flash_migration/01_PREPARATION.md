# 步骤1：准备工作

## 目标

本阶段完成所有前置准备工作，确保后续开发和迁移顺利进行。

## 任务清单

- [x] 1.1 备份当前固件和SD卡数据
- [ ] 1.2 开发打包工具脚本
- [ ] 1.3 验证Flash分区兼容性
- [ ] 1.4 创建开发分支

---

## 1.1 备份当前固件和SD卡数据

### 备份固件

```bash
# 连接设备到电脑
# 备份当前Flash全部内容（16MB）
esptool.py --port COM3 read_flash 0x0 0x1000000 alichuang_v1_backup_full.bin

# 仅备份关键分区（推荐，速度更快）
esptool.py --port COM3 read_flash 0x0 0x10000 bootloader.bin
esptool.py --port COM3 read_flash 0x100000 0x700000 app_ota0.bin
esptool.py --port COM3 read_flash 0x9000 0x4000 nvs.bin
```

**备份文件保存位置**：`backups/alichuang_v1_$(date +%Y%m%d)/`

### 备份SD卡数据

**方法1：完整拷贝（推荐）**
```bash
# Windows
xcopy E:\sdcard D:\backups\sdcard_backup /E /I /H

# Linux/Mac
cp -r /media/sdcard ~/backups/sdcard_backup
```

**方法2：仅备份多媒体资源**
```bash
# 备份关键目录
sdcard/
├── emergency/              # 紧急事件GIF+OGG
├── interaction/            # 交互动画GIF
├── state_expression/       # 状态表达GIF+OGG
└── system/                 # 系统资源（启动动画）
```

### 验证备份完整性

```bash
# 计算备份文件的SHA256
sha256sum alichuang_v1_backup_full.bin

# 记录文件大小
ls -lh backups/
```

**重要**：至少保留两份备份（本地 + 云端），防止数据丢失。

---

## 1.2 开发打包工具脚本（v1.1 LYNX）

### 工具功能需求

1. 递归扫描指定目录，收集所有`.gif`、`.ogg`、`.json`文件
2. 生成64字节文件索引表
3. 计算CRC32校验和
4. 输出`.bin`文件供`esptool.py`烧录
5. 支持详细日志输出和进度显示

### 创建脚本文件

**文件路径**：`scripts/pack_lynx_assets.py`

```python
#!/usr/bin/env python3
"""
Lynx Assets打包工具（v1.1）
功能：将GIF/OGG/JSON文件打包为Flash可烧录的.bin文件

使用方法：
    python scripts/pack_lynx_assets.py <输入目录> <输出文件.bin>

示例：
    python scripts/pack_lynx_assets.py ./sdcard lynx_assets.bin
"""

import struct
import os
import sys
import zlib
from pathlib import Path

# Flash分区格式常量
MAGIC_LYNX = b'LYNX'
VERSION = 1
ENTRY_SIZE = 64
NAME_SIZE = 48

def collect_files(input_dir):
    """递归收集所有GIF/OGG/JSON文件"""
    files = []
    extensions = {'.gif', '.ogg', '.json'}

    for root, dirs, filenames in os.walk(input_dir):
        for filename in filenames:
            if Path(filename).suffix.lower() in extensions:
                filepath = os.path.join(root, filename)
                filesize = os.path.getsize(filepath)

                # 使用相对路径作为名称（去掉扩展名）
                rel_path = os.path.relpath(filepath, input_dir)
                name = str(Path(rel_path).with_suffix(''))  # 去掉扩展名
                name = name.replace('\\', '/')  # 统一使用正斜杠

                files.append({
                    'name': name,
                    'path': filepath,
                    'size': filesize,
                    'ext': Path(filename).suffix.lower()
                })

    # 按名称排序，保证确定性输出
    files.sort(key=lambda x: x['name'])
    return files

def calculate_offsets(files):
    """计算每个文件在数据区的偏移量"""
    offset = 0
    for f in files:
        f['offset'] = offset
        offset += f['size']
    return offset  # 返回总数据大小

def pack_assets(input_dir, output_file):
    """打包资源文件"""

    print(f"扫描目录: {input_dir}")
    files = collect_files(input_dir)

    if not files:
        print("错误: 未找到任何GIF/OGG/JSON文件")
        sys.exit(1)

    print(f"\n找到 {len(files)} 个文件:")
    total_size = 0
    for f in files:
        print(f"  + {f['name']}{f['ext']} ({f['size']:,} bytes)")
        total_size += f['size']

    data_size = calculate_offsets(files)
    print(f"\n总数据大小: {total_size:,} bytes ({total_size / 1024 / 1024:.2f} MB)")

    # 计算分区总大小
    header_size = 16
    index_size = len(files) * ENTRY_SIZE
    partition_size = header_size + index_size + data_size
    print(f"分区总大小: {partition_size:,} bytes ({partition_size / 1024 / 1024:.2f} MB)")

    if partition_size > 8 * 1024 * 1024:
        print(f"\n警告: 分区大小 {partition_size / 1024 / 1024:.2f} MB 超过8MB限制!")
        print("建议：")
        print("  1. 压缩GIF文件（减少帧数或尺寸）")
        print("  2. 使用有损压缩的OGG音频")
        sys.exit(1)

    # 写入.bin文件
    print(f"\n开始写入: {output_file}")

    with open(output_file, 'wb') as f:
        # === Header (16 bytes) ===
        f.write(MAGIC_LYNX)                          # [0-3] Magic
        f.write(struct.pack('<I', VERSION))          # [4-7] Version
        f.write(struct.pack('<I', len(files)))       # [8-11] File count
        crc_placeholder_pos = f.tell()
        f.write(struct.pack('<I', 0))                # [12-15] CRC32 (占位)

        # === File Index Table ===
        for file in files:
            name_bytes = file['name'].encode('utf-8')[:NAME_SIZE]
            name_bytes = name_bytes.ljust(NAME_SIZE, b'\0')

            f.write(name_bytes)                      # [0-47] 文件名
            f.write(struct.pack('<I', file['offset']))  # [48-51] 偏移量
            f.write(struct.pack('<I', file['size']))    # [52-55] 大小
            f.write(struct.pack('<I', 0))            # [56-59] Reserved
            f.write(struct.pack('<I', 0))            # [60-63] Reserved

        # === Data Area ===
        data_start = f.tell()
        for i, file in enumerate(files, 1):
            print(f"  [{i}/{len(files)}] 写入 {file['name']}{file['ext']} ...", end='\r')
            with open(file['path'], 'rb') as src:
                f.write(src.read())

        print(f"\n\n数据写入完成，计算CRC32校验和...")

        # === 计算并写入CRC32 ===
        f.seek(data_start)
        data = f.read(data_size)
        crc32 = zlib.crc32(data) & 0xFFFFFFFF

        f.seek(crc_placeholder_pos)
        f.write(struct.pack('<I', crc32))

    final_size = os.path.getsize(output_file)
    print(f"\n✓ 打包完成!")
    print(f"  输出文件: {output_file}")
    print(f"  文件大小: {final_size:,} bytes ({final_size / 1024 / 1024:.2f} MB)")
    print(f"  CRC32: 0x{crc32:08X}")
    print(f"\n下一步: 使用以下命令烧录到Flash")
    print(f"  esptool.py --port COM3 write_flash 0x800000 {output_file}")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("用法: pack_lynx_assets.py <输入目录> <输出文件.bin>")
        print("\n示例:")
        print("  python scripts/pack_lynx_assets.py ./sdcard lynx_assets.bin")
        sys.exit(1)

    input_dir = sys.argv[1]
    output_file = sys.argv[2]

    if not os.path.isdir(input_dir):
        print(f"错误: 输入目录不存在: {input_dir}")
        sys.exit(1)

    pack_assets(input_dir, output_file)
```

### 测试打包工具

```bash
# 创建测试目录
mkdir -p test_assets/test_dir
echo "fake gif data" > test_assets/test_dir/test.gif
echo "fake ogg data" > test_assets/test_dir/test.ogg

# 运行打包工具
python scripts/pack_lynx_assets.py test_assets test_output.bin

# 验证输出
hexdump -C test_output.bin | head -20

# 期望看到:
# 00000000  4c 59 4e 58 01 00 00 00  02 00 00 00 XX XX XX XX  |LYNX............|
#           ^^^^^^^^ Magic
#                   ^^^^^^^^ Version=1
#                           ^^^^^^^^ FileCount=2
```

**验证通过标准**：
- [x] 脚本无错误运行
- [x] 输出文件包含正确的Magic "LYNX"
- [x] 文件数量正确
- [x] CRC32计算无误

---

## 1.3 验证Flash分区兼容性

### 检查当前分区表

```bash
# 读取当前分区表
esptool.py --port COM3 read_flash 0x8000 0x1000 current_partition_table.bin

# 解析分区表
python $IDF_PATH/components/partition_table/parttool.py --port COM3 get_partition_info
```

### 烧录v2分区表（测试）

**重要：这是破坏性操作，确保已备份！**

```bash
# 烧录新分区表
esptool.py --port COM3 write_flash 0x8000 partitions/v2/16m.csv

# 重启设备
esptool.py --port COM3 run

# 检查启动日志
idf.py -p COM3 monitor

# 期望看到:
# I (xxx) boot: ...
# I (xxx) esp_image: segment 0: ...
# I (xxx) ALichuangTest: Assets partition found, size=8MB
```

### 检查mmap可用页数

在`ALichuangTest.cc`构造函数中临时添加：

```cpp
// 测试代码：检查mmap页数
int free_pages = spi_flash_mmap_get_free_pages(SPI_FLASH_MMAP_DATA);
uint32_t free_size = free_pages * 64 * 1024;
ESP_LOGI(TAG, "Free mmap pages: %d (%.2f MB)", free_pages, free_size / 1024.0 / 1024.0);

// 期望输出: Free mmap pages: >128 (>8MB)
```

### 回滚测试

如果v2分区表测试失败，立即回滚：

```bash
# 恢复v1分区表
esptool.py --port COM3 write_flash 0x8000 partitions/v1/16m_anima.csv

# 恢复应用程序
esptool.py --port COM3 write_flash 0x100000 app_ota0.bin

# 重启验证
idf.py -p COM3 monitor
```

**验证通过标准**：
- [x] v2分区表烧录成功
- [x] 设备正常启动
- [x] Free mmap pages > 128
- [x] 可以成功回滚到v1

---

## 1.4 创建开发分支

### Git分支管理

```bash
# 确保在最新的main分支
git checkout main
git pull origin main

# 创建feature分支
git checkout -b feature/alichuang-flash-migration

# 查看当前状态
git status
```

### 提交准备工作

```bash
# 添加打包工具
git add scripts/pack_lynx_assets.py

# 提交
git commit -m "feat(ALichuangTest): add LYNX assets packing tool for Flash migration

- Create pack_lynx_assets.py to bundle GIF/OGG/JSON files
- Support 64-byte index format with type/flags
- Calculate CRC32 checksum for data integrity
- Related to Flash migration preparation phase
"

# 推送到远程
git push origin feature/alichuang-flash-migration
```

---

## 完成检查清单

在进入下一阶段前，确认以下所有项目：

- [x] **备份完整**
  - [ ] 固件备份文件存在：`alichuang_v1_backup_full.bin`
  - [ ] SD卡备份目录完整：`sdcard_backup/`
  - [ ] 备份文件已上传云端（可选但推荐）

- [ ] **打包工具就绪**
  - [ ] `pack_lynx_assets.py` 脚本创建完成
  - [ ] 工具测试运行成功
  - [ ] 能够正确生成`.bin`文件

- [ ] **分区兼容性验证**
  - [ ] v2分区表烧录测试成功
  - [ ] mmap页数充足（>128页）
  - [ ] 回滚测试成功

- [ ] **版本管理**
  - [ ] Git分支创建：`feature/alichuang-flash-migration`
  - [ ] 初始提交完成

**所有检查项通过后，继续 [步骤2：代码开发](02_CODE_DEVELOPMENT.md)**

---

## 故障排查

### 问题1：esptool.py烧录分区表失败

**症状**：
```
A fatal error occurred: MD5 of file does not match
```

**解决方案**：
1. 确保分区表文件格式正确（CSV或二进制）
2. 使用`--verify`参数验证烧录
3. 尝试先擦除Flash再烧录：
   ```bash
   esptool.py --port COM3 erase_region 0x8000 0x1000
   esptool.py --port COM3 write_flash 0x8000 partitions/v2/16m.csv
   ```

### 问题2：打包工具报告"文件过大"

**症状**：
```
警告: 分区大小 9.52 MB 超过8MB限制!
```

**解决方案**：
1. 压缩GIF文件：
   ```bash
   gifsicle -O3 --colors 128 input.gif -o output.gif
   ```
2. 降低OGG比特率：
   ```bash
   ffmpeg -i input.ogg -b:a 32k output.ogg
   ```
3. 检查是否有重复或不必要的文件

### 问题3：mmap页数不足

**症状**：
```
E (xxx) LynxAssets: The free size 6 MB is less than assets partition required 8 MB
```

**解决方案**：
1. 检查OTA分区大小是否正确（应为4MB×2）
2. 确认没有其他组件占用mmap页
3. 考虑减小assets分区到6MB

---

**下一步**：[步骤2：代码开发](02_CODE_DEVELOPMENT.md)
