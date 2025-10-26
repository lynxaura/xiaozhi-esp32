# Flash迁移方案更新 v1.1

## 更新概述

基于测试反馈，对原方案进行以下重要更新：

### 1. 包含配置文件支持 ✅
- 将SD卡config目录下的配置文件也打包到Flash
- 支持运行时读取和更新配置

### 2. 更改品牌标识 ✅
- Magic从 "ALGF" 改为 **"LYNX"** (LynxAura公司品牌)
- 更新所有相关命名

### 3. 本地烧录更新支持 ✅
- 无需云端OTA即可更新assets
- 支持开发阶段快速迭代测试

---

## 更新1：配置文件支持

### 1.1 SD卡完整文件结构

```
sdcard/
├── config/                              # 配置文件目录（新增）
│   ├── event_config.json        3.5KB  # 事件配置
│   └── response_config.json    11.5KB  # 响应配置
├── emergency/                           # 紧急事件
│   ├── motion_shake_violently/
│   │   ├── motion_shake_violently.gif  63KB
│   │   └── motion_shake_violently.ogg  35KB
│   └── motion_upside_down/
│       └── motion_upside_down.gif     104KB
├── interaction/                         # 交互动画（24个GIF）
│   ├── motion_pickup_q1~q4/
│   ├── motion_shake_q1~q4/
│   ├── touch_cradled_q1~q4/
│   ├── touch_long_press_q1~q4/
│   ├── touch_tap_q1~q4/
│   └── touch_tickled_q1~q4/
├── state_expression/                    # 状态表达
│   ├── idle/idle_q1~q4/        (4个GIF)
│   ├── listening/listening_q1~q4/ (4个GIF)
│   └── speaking/                (8个GIF + 29个OGG)
├── system/
│   └── system_boot_up.gif
└── welcome.ogg                   24KB  # 欢迎音频（可选）
```

**总计**：
- **105个资源文件**（73 GIF + 30 OGG + 2 JSON）
- **总大小**：约11.015MB

### 1.2 更新的Flash分区格式

```
┌─────────────────────────────────────────────┐
│ Header (16 bytes)                           │
│  [0-3]   Magic "LYNX" (LynxAura)           │  ← 更新
│  [4-7]   Version (uint32_t)                 │
│  [8-11]  File Count (uint32_t)              │
│  [12-15] CRC32 (uint32_t)                   │
├─────────────────────────────────────────────┤
│ File Index Table (64 bytes × N)            │
│  struct LynxAssetEntry {                    │
│    char name[48];         // 文件名          │
│    uint32_t offset;       // 数据偏移        │
│    uint32_t size;         // 文件大小        │
│    uint8_t type;          // 文件类型 ✨新增 │
│    uint8_t flags;         // 标志位 ✨新增   │
│    uint16_t reserved;     // 保留            │
│    uint32_t reserved2;    // 保留            │
│  };                                          │
├─────────────────────────────────────────────┤
│ Data Area                                   │
│  [GIF/OGG/JSON Raw Data]                   │
│  ...                                         │
└─────────────────────────────────────────────┘
```

**新增字段说明**：
```cpp
// 文件类型枚举
enum AssetType : uint8_t {
    ASSET_TYPE_GIF = 0,     // GIF动画
    ASSET_TYPE_OGG = 1,     // OGG音频
    ASSET_TYPE_JSON = 2,    // JSON配置 ✨新增
    ASSET_TYPE_UNKNOWN = 0xFF
};

// 标志位
enum AssetFlags : uint8_t {
    ASSET_FLAG_READONLY = 0x01,    // 只读（不可运行时修改）
    ASSET_FLAG_WRITABLE = 0x02,    // 可写（可运行时更新）✨配置文件使用
    ASSET_FLAG_COMPRESSED = 0x04   // 压缩（未来扩展）
};
```

### 1.3 配置文件访问接口

#### 读取配置

```cpp
// 在LynxAssets类中新增方法
class LynxAssets {
public:
    // 读取JSON配置文件
    bool GetConfigJson(const std::string& name, cJSON*& json_root);

    // 读取原始配置数据
    bool GetConfigData(const std::string& name, const char*& data, size_t& size);

    // 检查配置文件是否存在
    bool HasConfig(const std::string& name);
};
```

#### 使用示例

```cpp
// 加载event_config.json
auto& assets = *Board::GetInstance().GetLynxAssets();

cJSON* event_config = nullptr;
if (assets.GetConfigJson("config/event_config", event_config)) {
    // 解析配置
    cJSON* events = cJSON_GetObjectItem(event_config, "events");

    // 使用配置...

    // 清理
    cJSON_Delete(event_config);
} else {
    ESP_LOGE(TAG, "Failed to load event_config.json");
}
```

---

## 更新2：品牌标识更改

### 2.1 命名更新

| 原名称 | 新名称 | 说明 |
|-------|--------|------|
| `ALAssets` | `LynxAssets` | 类名 |
| `al_assets.h/cc` | `lynx_assets.h/cc` | 文件名 |
| `ALAssetEntry` | `LynxAssetEntry` | 结构体名 |
| Magic `"ALGF"` | Magic `"LYNX"` | Flash标识 |
| `pack_alichuang_assets.py` | `pack_lynx_assets.py` | 打包脚本 |
| `alichuang_assets.bin` | `lynx_assets.bin` | 输出文件名 |

### 2.2 代码更新示例

**头文件更新**：
```cpp
// lynx_assets.h
#ifndef LYNX_ASSETS_H
#define LYNX_ASSETS_H

#define LYNX_MAGIC "LYNX"
#define LYNX_VERSION 1

namespace lynxaura {

struct LynxAssetEntry {
    char name[48];
    uint32_t offset;
    uint32_t size;
    uint8_t type;
    uint8_t flags;
    uint16_t reserved;
    uint32_t reserved2;
};

class LynxAssets {
public:
    LynxAssets();
    ~LynxAssets();

    bool Initialize();
    bool GetResource(const std::string& name, void*& data, size_t& size);
    bool GetConfigJson(const std::string& name, cJSON*& json_root);

    // ... 其他方法
};

}  // namespace lynxaura

#endif  // LYNX_ASSETS_H
```

**实现文件验证Magic**：
```cpp
bool LynxAssets::ParseIndex() {
    // 验证Magic
    if (memcmp(mmap_root_, LYNX_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "Invalid magic: expected 'LYNX', got '%.4s'", mmap_root_);
        return false;
    }

    // ... 解析索引
}
```

---

## 更新3：本地烧录更新流程

### 3.1 开发阶段快速更新

无需重新编译固件，仅更新assets分区：

```bash
# 步骤1: 修改SD卡资源（GIF/OGG/JSON）
# 在 ./sdcard/ 目录中修改文件

# 步骤2: 重新打包
python scripts/pack_lynx_assets.py ./sdcard build/lynx_assets.bin

# 步骤3: 仅烧录assets分区（约1分钟）
esptool.py --port COM3 --baud 921600 write_flash 0x800000 build/lynx_assets.bin

# 步骤4: 重启设备查看效果
esptool.py --port COM3 run
```

**优势**：
- ⏱️ **快速迭代**：1分钟内完成资源更新
- 🔧 **无需重编译**：固件代码不变
- 🧪 **方便测试**：快速验证GIF/OGG/配置修改

### 3.2 配置文件热更新机制（可选）

为了支持配置文件在运行时更新，提供两种方案：

#### 方案A：仅烧录时更新（推荐）
- 配置文件随assets分区一起烧录
- 运行时只读访问
- 简单可靠

#### 方案B：支持运行时修改（复杂）
- 配置文件使用 `ASSET_FLAG_WRITABLE` 标志
- 修改后的配置保存到NVS
- 优先读取NVS中的配置，fallback到Flash

**方案B实现示例**：
```cpp
bool LynxAssets::GetConfigJson(const std::string& name, cJSON*& json_root) {
    // 1. 优先从NVS读取（运行时修改过的）
    nvs_handle_t nvs_handle;
    if (nvs_open("config", NVS_READONLY, &nvs_handle) == ESP_OK) {
        size_t required_size = 0;
        if (nvs_get_blob(nvs_handle, name.c_str(), nullptr, &required_size) == ESP_OK) {
            char* buffer = (char*)malloc(required_size);
            if (nvs_get_blob(nvs_handle, name.c_str(), buffer, &required_size) == ESP_OK) {
                json_root = cJSON_Parse(buffer);
                free(buffer);
                nvs_close(nvs_handle);
                if (json_root) {
                    ESP_LOGI(TAG, "Loaded config from NVS: %s", name.c_str());
                    return true;
                }
            }
            free(buffer);
        }
        nvs_close(nvs_handle);
    }

    // 2. Fallback到Flash
    const char* data = nullptr;
    size_t size = 0;
    if (GetConfigData(name, data, size)) {
        json_root = cJSON_ParseWithLength(data, size);
        if (json_root) {
            ESP_LOGI(TAG, "Loaded config from Flash: %s", name.c_str());
            return true;
        }
    }

    return false;
}
```

**建议**：开发阶段使用方案A，后期如需运行时配置再考虑方案B。

### 3.3 本地更新脚本

创建便捷脚本 `scripts/update_lynx_assets.bat`：

```bat
@echo off
echo ========================================
echo Lynx Assets 快速更新工具
echo ========================================

set PORT=COM3
set BAUD=921600

echo 提示: 此脚本仅更新assets分区，不影响固件

echo.
echo 步骤1/3: 打包资源...
python scripts/pack_lynx_assets.py sdcard build/lynx_assets.bin
if %ERRORLEVEL% NEQ 0 goto error

echo.
echo 步骤2/3: 烧录assets分区 (约60秒)...
esptool.py --port %PORT% --baud %BAUD% write_flash 0x800000 build/lynx_assets.bin
if %ERRORLEVEL% NEQ 0 goto error

echo.
echo 步骤3/3: 重启设备...
esptool.py --port %PORT% run

echo.
echo ========================================
echo 更新完成！
echo ========================================
pause
exit /b 0

:error
echo.
echo ========================================
echo 更新失败！
echo ========================================
pause
exit /b 1
```

**Linux/Mac版本** (`update_lynx_assets.sh`)：
```bash
#!/bin/bash

PORT="/dev/ttyUSB0"
BAUD="921600"

echo "========================================"
echo "Lynx Assets 快速更新工具"
echo "========================================"

echo "步骤1/3: 打包资源..."
python scripts/pack_lynx_assets.py sdcard build/lynx_assets.bin || exit 1

echo ""
echo "步骤2/3: 烧录assets分区 (约60秒)..."
esptool.py --port $PORT --baud $BAUD write_flash 0x800000 build/lynx_assets.bin || exit 1

echo ""
echo "步骤3/3: 重启设备..."
esptool.py --port $PORT run

echo ""
echo "========================================"
echo "更新完成！"
echo "========================================"
```

---

## 更新的打包工具

### 完整的 `pack_lynx_assets.py`

```python
#!/usr/bin/env python3
"""
Lynx Assets 打包工具 (LynxAura)
支持GIF、OGG、JSON文件打包到Flash分区
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

# 文件类型
ASSET_TYPE_GIF = 0
ASSET_TYPE_OGG = 1
ASSET_TYPE_JSON = 2
ASSET_TYPE_UNKNOWN = 0xFF

# 标志位
ASSET_FLAG_READONLY = 0x01
ASSET_FLAG_WRITABLE = 0x02

def get_file_type(filename):
    """根据扩展名判断文件类型"""
    ext = Path(filename).suffix.lower()
    if ext == '.gif':
        return ASSET_TYPE_GIF
    elif ext == '.ogg':
        return ASSET_TYPE_OGG
    elif ext == '.json':
        return ASSET_TYPE_JSON
    else:
        return ASSET_TYPE_UNKNOWN

def get_file_flags(filename):
    """根据文件类型确定标志位"""
    # JSON配置文件可写（可运行时更新）
    if filename.endswith('.json'):
        return ASSET_FLAG_WRITABLE
    else:
        return ASSET_FLAG_READONLY

def collect_files(input_dir):
    """递归收集所有GIF、OGG、JSON文件"""
    files = []
    extensions = {'.gif', '.ogg', '.json'}

    for root, dirs, filenames in os.walk(input_dir):
        for filename in filenames:
            if Path(filename).suffix.lower() in extensions:
                filepath = os.path.join(root, filename)
                filesize = os.path.getsize(filepath)

                # 使用相对路径作为名称（去掉扩展名）
                rel_path = os.path.relpath(filepath, input_dir)
                name = str(Path(rel_path).with_suffix(''))
                name = name.replace('\\', '/')

                file_type = get_file_type(filename)
                file_flags = get_file_flags(filename)

                files.append({
                    'name': name,
                    'path': filepath,
                    'size': filesize,
                    'type': file_type,
                    'flags': file_flags,
                    'ext': Path(filename).suffix.lower()
                })

    # 按名称排序
    files.sort(key=lambda x: x['name'])
    return files

def calculate_offsets(files):
    """计算每个文件的偏移量"""
    offset = 0
    for f in files:
        f['offset'] = offset
        offset += f['size']
    return offset

def pack_assets(input_dir, output_file):
    """打包资源文件"""

    print(f"扫描目录: {input_dir}")
    files = collect_files(input_dir)

    if not files:
        print("错误: 未找到任何GIF、OGG或JSON文件")
        sys.exit(1)

    print(f"\n找到 {len(files)} 个文件:")

    # 统计文件类型
    gif_count = sum(1 for f in files if f['type'] == ASSET_TYPE_GIF)
    ogg_count = sum(1 for f in files if f['type'] == ASSET_TYPE_OGG)
    json_count = sum(1 for f in files if f['type'] == ASSET_TYPE_JSON)

    total_size = 0
    for f in files:
        type_str = {
            ASSET_TYPE_GIF: 'GIF',
            ASSET_TYPE_OGG: 'OGG',
            ASSET_TYPE_JSON: 'JSON'
        }.get(f['type'], 'UNKNOWN')

        flags_str = 'RW' if f['flags'] & ASSET_FLAG_WRITABLE else 'RO'

        print(f"  + {f['name']}{f['ext']} ({f['size']:,} bytes) [{type_str}, {flags_str}]")
        total_size += f['size']

    print(f"\n文件统计:")
    print(f"  GIF动画: {gif_count} 个")
    print(f"  OGG音频: {ogg_count} 个")
    print(f"  JSON配置: {json_count} 个")
    print(f"  总大小: {total_size:,} bytes ({total_size / 1024 / 1024:.2f} MB)")

    data_size = calculate_offsets(files)

    # 计算分区总大小
    header_size = 16
    index_size = len(files) * ENTRY_SIZE
    partition_size = header_size + index_size + data_size
    print(f"  分区大小: {partition_size:,} bytes ({partition_size / 1024 / 1024:.2f} MB)")

    if partition_size > 8 * 1024 * 1024:
        print(f"\n警告: 分区大小超过8MB!")
        print("  建议压缩GIF或降低OGG比特率")
        sys.exit(1)

    # 写入bin文件
    print(f"\n开始写入: {output_file}")

    with open(output_file, 'wb') as f:
        # === Header (16 bytes) ===
        f.write(MAGIC_LYNX)                          # [0-3] Magic "LYNX"
        f.write(struct.pack('<I', VERSION))          # [4-7] Version
        f.write(struct.pack('<I', len(files)))       # [8-11] File count
        crc_placeholder_pos = f.tell()
        f.write(struct.pack('<I', 0))                # [12-15] CRC32占位

        # === File Index Table (64 bytes × N) ===
        for file in files:
            name_bytes = file['name'].encode('utf-8')[:NAME_SIZE]
            name_bytes = name_bytes.ljust(NAME_SIZE, b'\0')

            f.write(name_bytes)                      # [0-47] 文件名
            f.write(struct.pack('<I', file['offset']))  # [48-51] 偏移量
            f.write(struct.pack('<I', file['size']))    # [52-55] 大小
            f.write(struct.pack('<B', file['type']))    # [56] 类型
            f.write(struct.pack('<B', file['flags']))   # [57] 标志位
            f.write(struct.pack('<H', 0))            # [58-59] Reserved
            f.write(struct.pack('<I', 0))            # [60-63] Reserved

        # === Data Area ===
        data_start = f.tell()
        for i, file in enumerate(files, 1):
            print(f"  [{i}/{len(files)}] 写入 {file['name']}{file['ext']} ...", end='\r')
            with open(file['path'], 'rb') as src:
                f.write(src.read())

        print(f"\n\n数据写入完成，计算CRC32校验和...")

        # === 计算CRC32 ===
        f.seek(data_start)
        data = f.read(data_size)
        crc32_value = zlib.crc32(data) & 0xFFFFFFFF

        f.seek(crc_placeholder_pos)
        f.write(struct.pack('<I', crc32_value))

    final_size = os.path.getsize(output_file)
    print(f"\n✓ 打包完成!")
    print(f"  输出文件: {output_file}")
    print(f"  文件大小: {final_size:,} bytes ({final_size / 1024 / 1024:.2f} MB)")
    print(f"  CRC32: 0x{crc32_value:08X}")
    print(f"\n下一步: 使用以下命令烧录")
    print(f"  esptool.py --port COM3 write_flash 0x800000 {output_file}")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("用法: pack_lynx_assets.py <输入目录> <输出文件.bin>")
        print("\n示例:")
        print("  python scripts/pack_lynx_assets.py ./sdcard build/lynx_assets.bin")
        sys.exit(1)

    input_dir = sys.argv[1]
    output_file = sys.argv[2]

    if not os.path.isdir(input_dir):
        print(f"错误: 输入目录不存在: {input_dir}")
        sys.exit(1)

    pack_assets(input_dir, output_file)
```

---

## 代码修改摘要

### 文件重命名

```bash
# 重命名文件
mv main/boards/ALichuangTest/al_assets.h main/boards/ALichuangTest/lynx_assets.h
mv main/boards/ALichuangTest/al_assets.cc main/boards/ALichuangTest/lynx_assets.cc
mv scripts/pack_alichuang_assets.py scripts/pack_lynx_assets.py
```

### 代码中的命名替换

在所有相关文件中执行替换：
- `ALAssets` → `LynxAssets`
- `al_assets` → `lynx_assets`
- `alichuang::` → `lynxaura::`
- `"ALGF"` → `"LYNX"`

### 新增配置文件访问方法

在 `LynxAssets` 类中添加：

```cpp
bool LynxAssets::GetConfigJson(const std::string& name, cJSON*& json_root) {
    const char* data = nullptr;
    size_t size = 0;

    if (!GetConfigData(name, data, size)) {
        return false;
    }

    json_root = cJSON_ParseWithLength(data, size);
    if (!json_root) {
        ESP_LOGE(TAG, "Failed to parse JSON config: %s", name.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Loaded JSON config: %s (%zu bytes)", name.c_str(), size);
    return true;
}

bool LynxAssets::GetConfigData(const std::string& name, const char*& data, size_t& size) {
    void* ptr = nullptr;
    size_t sz = 0;

    if (!GetResource(name, ptr, sz)) {
        return false;
    }

    data = static_cast<const char*>(ptr);
    size = sz;
    return true;
}
```

---

## 测试验证

### 配置文件加载测试

```cpp
// 在ALichuangTest.cc构造函数中测试
if (lynx_assets_ && lynx_assets_->IsChecksumValid()) {
    ESP_LOGI(TAG, "Testing config file loading...");

    // 测试event_config.json
    cJSON* event_config = nullptr;
    if (lynx_assets_->GetConfigJson("config/event_config", event_config)) {
        cJSON* events = cJSON_GetObjectItem(event_config, "events");
        int event_count = cJSON_GetArraySize(events);
        ESP_LOGI(TAG, "✓ event_config.json loaded: %d events", event_count);
        cJSON_Delete(event_config);
    } else {
        ESP_LOGE(TAG, "✗ Failed to load event_config.json");
    }

    // 测试response_config.json
    cJSON* response_config = nullptr;
    if (lynx_assets_->GetConfigJson("config/response_config", response_config)) {
        cJSON* version = cJSON_GetObjectItem(response_config, "version");
        ESP_LOGI(TAG, "✓ response_config.json loaded: version=%s",
                 cJSON_GetStringValue(version));
        cJSON_Delete(response_config);
    } else {
        ESP_LOGE(TAG, "✗ Failed to load response_config.json");
    }
}
```

---

## 完成检查清单

- [ ] **文件重命名**
  - [ ] `al_assets.*` → `lynx_assets.*`
  - [ ] `pack_alichuang_assets.py` → `pack_lynx_assets.py`

- [ ] **Magic更新**
  - [ ] 所有 `"ALGF"` 替换为 `"LYNX"`
  - [ ] 命名空间 `alichuang::` → `lynxaura::`

- [ ] **配置文件支持**
  - [ ] 打包工具支持JSON文件
  - [ ] 添加 `GetConfigJson()` 方法
  - [ ] 添加文件类型和标志位字段

- [ ] **本地更新流程**
  - [ ] 创建 `update_lynx_assets.bat/sh`
  - [ ] 测试快速更新流程
  - [ ] 验证配置文件加载

---

**更新版本**: v1.1
**更新日期**: 2025-10-26
**适用范围**: ALichuangTest Flash迁移项目
