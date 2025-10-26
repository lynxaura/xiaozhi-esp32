# 步骤3：资源打包与烧录

## 目标

本阶段将SD卡上的所有GIF/OGG/JSON文件打包成Flash可烧录的`.bin`文件，并完成分区表和资源数据的烧录。

## 任务清单

- [ ] 3.1 准备资源文件目录
- [ ] 3.2 执行资源打包
- [ ] 3.3 烧录分区表
- [ ] 3.4 烧录assets分区
- [ ] 3.5 验证Flash数据完整性

---

## 3.1 准备资源文件目录

### 从SD卡复制资源文件

```bash
# 创建临时打包目录
mkdir -p build/assets_temp

# 复制SD卡资源（假设SD卡挂载在E盘）
# Windows
xcopy E:\sdcard build\assets_temp /E /I /H

# Linux/Mac
cp -r /media/sdcard/* build/assets_temp/
```

### 目录结构验证

确保目录结构如下：

```
build/assets_temp/
├── emergency/
│   ├── motion_shake_violently/
│   │   ├── motion_shake_violently.gif
│   │   └── motion_shake_violently.ogg
│   └── motion_upside_down/
│       └── motion_upside_down.gif
├── interaction/
│   ├── motion_pickup_q1/
│   │   └── motion_pickup_q1.gif
│   ├── touch_tap_q1/
│   │   └── touch_tap_q1.gif
│   └── ...
├── state_expression/
│   ├── idle/
│   │   ├── idle_q1/
│   │   │   └── idle_q1.gif
│   │   └── ...
│   ├── listening/
│   │   └── ...
│   └── speaking/
│       ├── talk_calm/
│       │   ├── talk_calm.gif
│       │   └── talk_calm.ogg
│       └── ...
└── system/
    └── system_boot_up.gif
```

### 统计文件数量

```bash
# Linux/Mac/Git Bash
find build/assets_temp -name "*.gif" -o -name "*.ogg" | wc -l

# 期望输出: 约105（以实际为准）
```

### 检查文件总大小

```bash
# Linux/Mac
du -sh build/assets_temp

# Windows (PowerShell)
Get-ChildItem -Path build\assets_temp -Recurse | Measure-Object -Property Length -Sum

# 期望输出: ~11MB（以实际为准）
```

---

## 3.2 执行资源打包

### 运行打包脚本

```bash
# 进入项目根目录
cd D:\AIDev\xiaozhi-esp32

# 执行打包（v1.1 LYNX，包含 GIF/OGG/JSON）
python scripts/pack_lynx_assets.py build/assets_temp build/lynx_assets.bin
```

### 预期输出

```
扫描目录: build/assets_temp

找到 103 个文件:
  + emergency/motion_shake_violently/motion_shake_violently.gif (63,421 bytes)
  + emergency/motion_shake_violently/motion_shake_violently.ogg (35,128 bytes)
  + emergency/motion_upside_down/motion_upside_down.gif (104,592 bytes)
  ...
  + state_expression/speaking/talk_content.ogg (7,845 bytes)

总数据大小: 10,985,632 bytes (10.48 MB)
分区总大小: 10,992,208 bytes (10.48 MB)

开始写入: build/lynx_assets.bin
  [103/103] 写入 state_expression/speaking/talk_content ...

数据写入完成，计算CRC32校验和...

✓ 打包完成!
  输出文件: build/lynx_assets.bin
  文件大小: 10,992,208 bytes (10.48 MB)
  CRC32: 0x1A2B3C4D

下一步: 使用以下命令烧录到Flash
  esptool.py --port COM3 write_flash 0x800000 build/lynx_assets.bin
```

### 验证打包文件

```bash
# 查看文件大小
ls -lh build/lynx_assets.bin

# 检查Magic和Header
hexdump -C build/lynx_assets.bin | head -5

# 期望输出:
# 00000000  4c 59 4e 58 01 00 00 00  67 00 00 00 4d 3c 2b 1a  |LYNX....g...M<+.|
#           ^^^^^^^^ Magic "LYNX"
#                   ^^^^^^^^ Version=1
#                           ^^^^^^^^ FileCount=103
#                                   ^^^^^^^^ CRC32
```

### 处理"文件过大"警告

如果输出显示分区大小超过8MB：

```
警告: 分区大小 9.52 MB 超过8MB限制!
```

**解决方案1：压缩GIF文件**

```bash
# 安装gifsicle
# Windows: choco install gifsicle
# Linux: apt install gifsicle
# Mac: brew install gifsicle

# 批量压缩GIF（保留在原地）
find build/assets_temp -name "*.gif" -exec gifsicle -O3 --colors 128 -b {} \;

# 再次打包
python scripts/pack_lynx_assets.py build/assets_temp build/lynx_assets.bin
```

**解决方案2：降低OGG比特率**

```bash
# 安装ffmpeg
# Windows: choco install ffmpeg
# Linux: apt install ffmpeg
# Mac: brew install ffmpeg

# 批量重编码OGG
for f in build/assets_temp/**/*.ogg; do
    ffmpeg -i "$f" -b:a 32k "${f}.new" && mv "${f}.new" "$f"
done
```

---

## 3.3 烧录分区表

### 备份当前分区表（可选）

```bash
esptool.py --port COM3 read_flash 0x8000 0x1000 build/old_partition_table.bin
```

### 烧录v2分区表

```bash
# 确认分区表文件存在
ls partitions/v2/16m.csv

# 烧录
esptool.py --port COM3 write_flash 0x8000 partitions/v2/16m.csv

# 期望输出:
# Wrote 3072 bytes at 0x00008000 in 0.3 seconds
```

### 验证分区表

```bash
# 重启设备
esptool.py --port COM3 run

# 监控日志
idf.py -p COM3 monitor

# 期望看到:
# I (xxx) boot: Partition Table:
# I (xxx) boot: ## Label            Usage          Type ST Offset   Length
# I (xxx) boot:  0 nvs              WiFi data        01 02 00009000 00004000
# I (xxx) boot:  1 otadata          OTA data         01 00 0000d000 00002000
# I (xxx) boot:  2 phy_init         RF data          01 01 0000f000 00001000
# I (xxx) boot:  3 ota_0            OTA app          00 10 00020000 003f0000
# I (xxx) boot:  4 ota_1            OTA app          00 11 00410000 003f0000
# I (xxx) boot:  5 assets           Unknown data     01 82 00800000 00800000
#                                                                    ^^^^^^^^ 8MB assets分区
```

---

## 3.4 烧录assets分区

### 擦除assets分区（推荐）

```bash
# 擦除assets分区（0x800000起始，8MB大小）
esptool.py --port COM3 erase_region 0x800000 0x800000

# 期望输出:
# Erasing region (this may take some time)...
```

### 烧录assets数据

```bash
# 烧录（可能需要5-10分钟，取决于文件大小）
esptool.py --port COM3 --baud 921600 write_flash 0x800000 build/lynx_assets.bin

# 参数说明:
#   --baud 921600: 提高波特率加速烧录（可选，默认115200）
#   0x800000: assets分区起始地址（见partitions/v2/16m.csv）
```

### 预期烧录输出

```
esptool.py v4.7.0
Serial port COM3
Connecting....
Detecting chip type... ESP32-S3
Chip is ESP32-S3 (QFN56) (revision v0.2)
...
Configuring flash size...
Flash will be erased from 0x00800000 to 0x00dfffff...
Compressed 10992208 bytes to 6543210...
Wrote 10992208 bytes (6543210 compressed) at 0x00800000 in 120.5 seconds (effective 729.3 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```

**时间估算**：
- 115200 baud: ~180秒（3分钟）
- 460800 baud: ~90秒（1.5分钟）
- 921600 baud: ~60秒（1分钟）

---

## 3.5 验证Flash数据完整性

### 方法1：设备端验证（推荐）

```bash
# 烧录固件（包含LynxAssets类）
esptool.py --port COM3 write_flash 0x20000 build/xiaozhi.bin

# 重启并监控日志
idf.py -p COM3 monitor
```

**期望日志输出**：

```
I (xxx) LynxAssets: Initializing LynxAssets...
I (xxx) LynxAssets: Found assets partition: size=8192 KB
I (xxx) LynxAssets: Free mmap pages: 1824 (114.00 MB)
I (xxx) LynxAssets: Partition mapped to virtual address: 0x3c000000
I (xxx) LynxAssets: Assets header: version=1, files=103, crc=0x1A2B3C4D
D (xxx) LynxAssets:   [0] emergency/motion_shake_violently/motion_shake_violently: offset=0, size=63421
D (xxx) LynxAssets:   [1] emergency/motion_shake_violently/motion_shake_violently: offset=63421, size=35128
...
D (xxx) LynxAssets:   [102] system/system_boot_up: offset=10978943, size=6689
I (xxx) LynxAssets: Parsed 103 files successfully
I (xxx) LynxAssets: Calculating CRC32 for 10729 KB data...
I (xxx) LynxAssets: CRC32 calculation took 1234 ms
I (xxx) LynxAssets: CRC32 verified: 0x1A2B3C4D
I (xxx) LynxAssets: LynxAssets initialized successfully: 103 files
I (xxx) ALichuangTest: LynxAssets initialized: 103 files loaded
```

**关键验证点**：
- [x] Partition mapped successfully
- [x] File count correct (103)
- [x] CRC32 matched
- [x] All 103 files parsed

### 方法2：读回Flash验证（可选）

```bash
# 读回assets分区数据
esptool.py --port COM3 read_flash 0x800000 0xa7e000 build/readback_assets.bin
# 注: 0xa7e000 = 10,985,472 bytes (实际数据大小，从打包输出获取)

# 比较文件
# Windows (PowerShell)
(Get-FileHash build\alichuang_assets_v1.bin).Hash -eq (Get-FileHash build\readback_assets.bin).Hash

# Linux/Mac
diff build/lynx_assets.bin build/readback_assets.bin

# 期望: 无差异
```

### 方法3：测试资源加载

在设备端执行简单测试：

```cpp
// 在ALichuangTest.cc构造函数中添加测试代码
if (al_assets_ && al_assets_->IsChecksumValid()) {
    // 测试加载一个GIF
    void* gif_data = nullptr;
    size_t gif_size = 0;

    if (al_assets_->GetResource("emergency/motion_shake_violently/motion_shake_violently",
                                gif_data, gif_size)) {
        ESP_LOGI(TAG, "✓ Test GIF loaded: %p, %zu bytes", gif_data, gif_size);

        // 验证数据非空
        const uint8_t* bytes = static_cast<const uint8_t*>(gif_data);
        ESP_LOGI(TAG, "  First 4 bytes: %02X %02X %02X %02X",
                 bytes[0], bytes[1], bytes[2], bytes[3]);
        // GIF文件应以 "GIF8" 开头 (47 49 46 38)
    } else {
        ESP_LOGE(TAG, "✗ Test GIF load failed");
    }

    // 测试加载一个OGG
    void* ogg_data = nullptr;
    size_t ogg_size = 0;

    if (al_assets_->GetResource("emergency/motion_shake_violently/motion_shake_violently",
                                ogg_data, ogg_size)) {
        ESP_LOGI(TAG, "✓ Test OGG loaded: %p, %zu bytes", ogg_data, ogg_size);

        const uint8_t* bytes = static_cast<const uint8_t*>(ogg_data);
        ESP_LOGI(TAG, "  First 4 bytes: %02X %02X %02X %02X",
                 bytes[0], bytes[1], bytes[2], bytes[3]);
        // OGG文件应以 "OggS" 开头 (4F 67 67 53)
    } else {
        ESP_LOGE(TAG, "✗ Test OGG load failed");
    }
}
```

**期望输出**：

```
I (xxx) ALichuangTest: ✓ Test GIF loaded: 0x3c001000, 63421 bytes
I (xxx) ALichuangTest:   First 4 bytes: 47 49 46 38  (GIF8)
I (xxx) ALichuangTest: ✓ Test OGG loaded: 0x3c010000, 35128 bytes
I (xxx) ALichuangTest:   First 4 bytes: 4F 67 67 53  (OggS)
```

---

## 烧录问题排查

### 问题1：烧录超时

**症状**：
```
A fatal error occurred: Timed out waiting for packet header
```

**解决方案**：
1. 降低波特率：`--baud 115200`
2. 更换USB线（使用带屏蔽的优质线缆）
3. 缩短USB线长度（<1米）
4. 使用USB 2.0接口（避免USB 3.0兼容问题）

### 问题2：CRC32校验失败

**症状**：
```
E (xxx) LynxAssets: CRC32 mismatch: stored=0x1A2B3C4D, calculated=0xDEADBEEF
```

**解决方案**：
1. 重新打包资源文件
2. 重新烧录assets分区
3. 检查Flash是否有物理损坏：
   ```bash
   esptool.py --port COM3 flash_id
   ```

### 问题3：设备启动卡住

**症状**：
启动日志在分区表验证处停止

**解决方案**：
1. 完全擦除Flash重新烧录：
   ```bash
   esptool.py --port COM3 erase_flash
   esptool.py --port COM3 write_flash 0x0 build/xiaozhi.bin
   esptool.py --port COM3 write_flash 0x8000 partitions/v2/16m.csv
   esptool.py --port COM3 write_flash 0x800000 build/lynx_assets.bin
   ```

2. 验证bootloader完整性：
   ```bash
   esptool.py --port COM3 write_flash 0x0 build/bootloader/bootloader.bin
   ```

### 问题4：mmap失败

**症状**：
```
E (xxx) LynxAssets: Failed to mmap partition: ESP_ERR_NO_MEM
```

**解决方案**：
1. 检查OTA分区大小（应为4MB×2，不是7MB×2）
2. 验证分区表烧录正确：
   ```bash
   esptool.py --port COM3 read_flash 0x8000 0x1000 readback_partition.bin
   hexdump -C readback_partition.bin | grep assets
   ```

---

## 完成检查清单

- [ ] **资源准备**
  - [ ] SD卡资源复制到 `build/assets_temp/`
  - [ ] 文件数量正确（103个）
  - [ ] 总大小约11MB

- [ ] **资源打包**
  - [ ] `pack_lynx_assets.py` 执行成功
  - [ ] 输出文件 `lynx_assets.bin` 生成
  - [ ] 文件大小 <8MB（或已压缩）
  - [ ] CRC32校验和记录

- [ ] **分区表烧录**
  - [ ] v2分区表烧录成功
  - [ ] 设备日志显示assets分区（8MB）

- [ ] **Assets烧录**
  - [ ] assets分区数据烧录成功
  - [ ] 烧录时间合理（1-3分钟）

- [ ] **完整性验证**
  - [ ] 设备端CRC32校验通过
  - [ ] 文件数量匹配（103个）
  - [ ] 测试GIF/OGG加载成功
  - [ ] 文件Magic验证通过（GIF8, OggS）

**所有检查项通过后，继续 [步骤4：测试验证](04_TESTING.md)**

---

**下一步**：[步骤4：测试验证](04_TESTING.md)
