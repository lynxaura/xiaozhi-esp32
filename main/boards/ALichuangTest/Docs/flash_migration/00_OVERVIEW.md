# ALichuangTest Flash分区迁移方案 - 总览

## 文档概述

本文档详细说明如何将ALichuangTest开发板的多媒体资源从SD卡迁移到Flash分区，基于**方案B：自定义Flash分区（简化版，v1.1 LYNX）**实施。本方案完全不依赖SD卡，所有资源存储在Flash的assets分区中。

## 项目背景

### 当前状况（v1.0，参考现状）
- **资源存储方式**：73个GIF动画 + 30个OGG音频，总计~11MB，全部存储在SD卡
- **分区表版本**：使用v1分区表（`partitions/v1/16m_anima.csv`），无assets分区
- **性能问题**：
  - GIF加载延迟：30-50ms（SD卡读取）
  - 内存峰值：360KB（文件缓冲 + 解码canvas）
  - 内存碎片：频繁malloc/free导致内存抖动±50KB
  - OGG播放：双重缓冲浪费14KB内存

### 目标状态（v1.1，LYNX）
- **资源存储方式**：全部迁移到Flash assets分区（8MB），零拷贝访问
- **分区表版本**：升级到v2分区表（`partitions/v2/16m.csv`）
- **性能提升**：
  - GIF加载延迟：<1μs（指针运算，快35000倍）
  - 内存峰值：307KB（仅解码canvas，节省53KB）
  - 内存稳定：无碎片，±10KB波动
  - OGG播放：零拷贝，节省7KB内存
- **完全移除SD卡依赖**：代码不再需要SD卡初始化和文件访问

## 方案选择：方案B（自定义Flash分区）

### 为什么选择方案B？

| 对比项 | 方案A（标准Assets） | 方案B（自定义格式） | 选择理由 |
|-------|-------------------|-------------------|---------|
| **数据格式** | 复杂（48字节索引，"ZZ"魔数） | 简化（64字节索引，无魔数） | ✅ 更简单易维护 |
| **打包工具** | 需学习现有工具 | 自主开发 | ✅ 完全可控 |
| **兼容性** | 需适配LVGL主题、srmodels | 仅存储GIF/OGG原始数据 | ✅ 无需额外适配 |
| **扩展性** | 受限于标准格式 | 64字节预留字段灵活扩展 | ✅ 未来可添加压缩等功能 |
| **调试难度** | 复杂格式不易调试 | 简单格式，hexdump可读 | ✅ 调试友好 |
| **开发时间** | 3-5天（需学习） | 2-3天（自主实现） | ✅ 更快交付 |

**决策：采用方案B，完全移除SD卡，所有资源存储在Flash分区**

## 技术架构

### Flash分区数据结构（v1.1 LYNX）

```
┌─────────────────────────────────────────────┐
│ Header (16 bytes)                           │
│  [0-3]   Magic "LYNX" (LynxAura)           │
│  [4-7]   Version (uint32_t)                 │
│  [8-11]  File Count (uint32_t)              │
│  [12-15] CRC32 (uint32_t)                   │
├─────────────────────────────────────────────┤
│ File Index Table (64 bytes × N)            │
│  struct LynxAssetEntry {                    │
│    char name[48];       // 文件名            │
│    uint32_t offset;     // 数据偏移          │
│    uint32_t size;       // 文件大小          │
│    uint8_t type;        // 文件类型 (GIF/OGG/JSON) │
│    uint8_t flags;       // 标志位 (只读/可写)  │
│    uint16_t reserved;   // 保留               │
│    uint32_t reserved2;  // 保留               │
│  };                                         │
├─────────────────────────────────────────────┤
│ Data Area                                   │
│  [GIF/OGG/JSON Raw Data]                   │
│  [GIF/OGG/JSON Raw Data]                   │
│  ...                                         │
└─────────────────────────────────────────────┘
```

### 分区表变更

**当前 v1 分区表** (`partitions/v1/16m_anima.csv`)：
```csv
model,    data, spiffs,  0x10000,   960KB    # 浪费的空间
ota_0,    app,  ota_0,   0x100000,  7MB
ota_1,    app,  ota_1,   0x800000,  7MB
# 无assets分区
```

**目标 v2 分区表** (`partitions/v2/16m.csv`)：
```csv
ota_0,    app,  ota_0,   0x20000,   4MB      # 减小3MB
ota_1,    app,  ota_1,   ,          4MB      # 减小3MB
assets,   data, spiffs,  0x800000,  8MB      # 新增8MB分区
```

**变更说明**：
- ✅ 移除model分区（960KB），ALichuangTest不使用唤醒词
- ✅ 压缩OTA分区：7MB×2 → 4MB×2（当前固件3.0MB，4MB足够）
- ✅ 新增assets分区：8MB，足够存储11MB资源（压缩后约6-7MB）

### 代码架构（v1.1 LYNX）

#### 新增组件

1. **LynxAssets类** (`main/boards/ALichuangTest/lynx_assets.h/cc`)
   - 管理Flash分区映射（esp_partition_mmap）
   - 提供零拷贝资源访问接口
   - 负责索引表解析和验证

2. **打包工具** (`scripts/pack_lynx_assets.py`)
   - 递归扫描GIF/OGG/JSON文件
   - 生成64字节索引表（含类型/标志）
   - 计算CRC32校验和
   - 输出`.bin`文件供烧录

#### 修改组件

1. **AnimaDisplay** (`skills/animation.cc`)
   - `SetAnima()`: 优先从Flash加载GIF（零拷贝/内存源或FS适配）
   - 使用`lv_img_dsc_t`结构直接传递Flash指针给LVGL
   - **移除SD卡降级逻辑**

2. **Application** (`main/application.cc`)
   - `PlaySoundOGGFile()`: 直接使用Flash数据（零拷贝）
   - 使用`std::string_view`包装Flash数据，无需malloc
   - **移除SD卡文件访问代码**

3. **ALichuangTest Board** (`ALichuangTest.cc`)
   - 构造函数中初始化LynxAssets
   - **移除SD卡初始化代码（SDdata_Pro）**
   - 通过`GetAssets()`方法暴露给其他组件

4. **配置文件** (`config.json`)
   - 修改分区表路径：`partitions/v2/16m.csv`
   - **移除SD卡相关配置**

## 资源迁移范围

### 需要打包的文件（103个文件，~11MB）

#### 1. 紧急事件 (2个GIF + 1个OGG)
```
emergency/motion_shake_violently/motion_shake_violently.gif    63KB
emergency/motion_shake_violently/motion_shake_violently.ogg    35KB
emergency/motion_upside_down/motion_upside_down.gif           104KB
```

#### 2. 交互动画 (24个GIF)
```
interaction/motion_pickup_q1~q4/*.gif       (4个) 45-47KB
interaction/motion_shake_q1~q4/*.gif        (4个) 26-87KB
interaction/touch_cradled_q1~q4/*.gif       (4个) 29KB
interaction/touch_long_press_q1~q4/*.gif    (4个) 39KB
interaction/touch_tap_q1~q4/*.gif           (4个) 52-58KB
interaction/touch_tickled_q1~q4/*.gif       (4个) 29KB
```

#### 3. 状态表达 (47个GIF + 29个OGG)
```
state_expression/idle/idle_q1~q4/*.gif           (4个)
state_expression/listening/listening_q1~q4/*.gif (4个)
state_expression/speaking/talk_*.gif             (8个) 43-104KB
state_expression/speaking/*.ogg                  (29个) 3.6-7.8KB
```

**文件命名规范**：
- Flash中使用相对路径作为key：`"emergency/motion_shake_violently/motion_shake_violently.gif"`
- 代码查找时使用短名称：`"motion_shake_violently"` → 自动拼接路径和扩展名

## 实施阶段概览

### 阶段1：准备工作（1天）
- [x] 备份当前固件和SD卡数据
- [ ] 开发打包工具 `pack_lynx_assets.py`
- [ ] 创建LynxAssets类框架
- [ ] 验证v2分区表在硬件上的兼容性

### 阶段2：代码开发（2天）
- [ ] 实现LynxAssets类（Flash映射 + 索引解析 + JSON支持）
- [ ] 修改AnimaDisplay支持Flash GIF加载
- [ ] 修改Application支持Flash OGG播放
- [ ] **移除ALichuangTest.cc中的SD卡初始化代码**
- [ ] 更新config.json分区表配置

### 阶段3：资源打包与烧录（0.5天）
- [ ] 执行打包脚本生成`lynx_assets.bin`
- [ ] 烧录v2分区表
- [ ] 烧录assets分区数据
- [ ] 验证Flash数据完整性（CRC32校验）

### 阶段4：测试验证（1天）
- [ ] 功能测试：所有GIF动画播放正常
- [ ] 功能测试：所有OGG音频播放正常
- [ ] 性能测试：加载延迟 <5ms
- [ ] 内存测试：峰值 <350KB，波动 <±20KB
- [ ] 压力测试：连续切换100次动画无内存泄漏

### 阶段5：文档与发布（0.5天）
- [ ] 编写用户烧录指南
- [ ] 更新板级README
- [ ] 提交代码并标记版本

**总预计时间：5天**

## 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| **分区表升级失败** | 设备变砖 | 低 | 备份v1固件，提供完整烧录指南 |
| **Flash容量不足** | 部分资源无法打包 | 低 | 8MB空间足够11MB压缩资源 |
| **LVGL兼容性问题** | GIF无法显示 | 低 | 提前测试，LVGL支持内存数据源 |
| **内存映射失败** | 资源无法访问 | 极低 | ESP32-S3有1824个mmap页，远超需要的128页 |
| **CRC校验失败** | 启动失败 | 低 | 打包工具自动验证，烧录后再次验证 |

## 性能预期

### 加载延迟对比

| 操作 | SD卡方式 | Flash方式 | 提升倍数 |
|------|---------|----------|---------|
| **GIF加载** | 30-50ms | <1μs | **35000×** |
| **OGG加载** | 15-20ms | <0.5μs | **30000×** |
| **文件打开** | 5-15ms | 指针运算 | **无限大** |

### 内存消耗对比

| 场景 | SD卡方式 | Flash方式 | 节省 |
|------|---------|----------|------|
| **单个GIF** | 360KB | 307KB | **53KB** |
| **单个OGG** | 14KB | 7KB | **7KB** |
| **GIF+音效** | 374KB | 314KB | **60KB** |
| **内存波动** | ±50KB | ±10KB | **减少80%** |

### 用户体验提升

```
场景：用户说"开心点" → LLM返回emotion="happy"

SD卡方式:
  TTS开始 → 35ms延迟 → 显示笑脸 GIF
  用户感知：有点卡顿

Flash方式:
  TTS开始 → <1ms → 显示笑脸 GIF
  用户感知：即时响应，非常流畅
```

## 后续优化方向

### 短期（1-2月）
- [ ] 实现OTA assets更新（无需重新烧录）
- [ ] 添加资源版本管理
- [ ] 优化打包工具支持增量更新

### 中期（3-6月）
- [ ] 实现GIF资源压缩（DEFLATE/LZ4）
- [ ] 添加多语言音频支持
- [ ] 实现运行时资源热更新

### 长期（6月+）
- [ ] 统一到标准Assets格式（与其他板子一致）
- [ ] 支持云端资源CDN下载
- [ ] 实现用户自定义表情包

## 关键文档索引

| 文档 | 路径 | 说明 |
|------|------|------|
| **总览（本文档）** | `00_OVERVIEW.md` | 方案背景、架构、阶段规划 |
| **步骤1：准备工作** | `01_PREPARATION.md` | 备份、工具开发、分区验证 |
| **步骤2：代码开发** | `02_CODE_DEVELOPMENT.md` | LynxAssets类、动画/音频适配 |
| **步骤3：资源打包** | `03_ASSET_PACKING.md` | 打包工具使用、烧录步骤 |
| **步骤4：测试验证** | `04_TESTING.md` | 功能/性能/压力测试清单 |
| **步骤5：部署发布** | `05_DEPLOYMENT.md` | 用户指南、版本发布 |
| **代码迁移清单** | `CODE_MIGRATION_CHECKLIST.md` | 详细代码修改点 |

## 联系与支持

如在实施过程中遇到问题，请参考：
- 技术分析文档：`../sdcard_vs_assets_analysis.md`
- ESP-IDF分区表文档：`partitions/v2/README.md`
- 相关代码参考：`main/boards/lichuang-dev/`（已实现Assets的板子）

---

**文档版本**：v1.0
**创建日期**：2025-10-26
**适用板型**：ALichuangTest (ESP32-S3, 16MB Flash)
**维护者**：ALichuangTest开发团队
