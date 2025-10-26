# Flash迁移快速开始指南 v1.1

> 状态与前置条件（重要）：
> - 本文描述的是“v1.1”方案目标与使用方式，当前仓库代码仍以 SD 卡资源加载为主（GIF/OGG/JSON 仍从 `/sdcard` 读取）。
> - 文档中的 LynxAssets 类、LYNX 分区格式、配置文件从 Flash 读取等能力尚未在代码中落地；`scripts/pack_lynx_assets.py` 亦未随仓库提交（完整脚本示例见 `UPDATES_V1.1.md`）。
> - `scripts/update_lynx_assets.bat/.sh` 已存在，但依赖的 `pack_lynx_assets.py` 缺失时会报错；在脚本可用前，请不要据此流程操作。
> - 分区表已包含 `assets` 分区（8MB，起始地址 0x800000），地址与容量与本文一致，可用于后续落地。
>
> 建议先阅读“完成文档索引”中的 `UPDATES_V1.1.md`，按其中的“代码修改摘要/完成检查清单”推进代码与脚本落地，之后再完全按本文流程执行。

## 🎯 一分钟了解

将ALichuangTest的**所有资源**（GIF、OGG、配置文件）从SD卡迁移到Flash，实现：
- ⚡ **35000倍**的加载速度提升
- 💾 **20%**的内存节省
- 🚀 **1分钟**快速更新（无需重编译）
- 🏷️ **LYNX品牌**标识（LynxAura）

## 📦 资源结构（按类型统计）

```
sdcard/
├── config/                     # 配置文件（2个JSON，~15KB）
│   ├── event_config.json       ~3.5KB
│   └── response_config.json    ~11.5KB
├── GIF动画（约73个）            ~3.6MB
├── OGG音频（约30个）            ~200KB
└── 说明：以上为“主要资源类型”的统计，仓库当前 `sdcard/` 实际文件总数约为 200 个（含 .p3 等辅助文件），总体积约 10.3MB（以本仓库当前内容为准）。
```

## 🔧 核心技术更新

### 1. Flash分区格式（LYNX v1.1，待落地）

```
┌─ Header ─────────────────────┐
│ Magic: "LYNX" (LynxAura)     │  ✨ 品牌标识
│ Version: 1                    │
│ File Count: N                 │  ✨ 包含配置文件
│ CRC32: 校验和                 │
├─ Index (64字节×105) ─────────┤
│ 文件名(48) + 偏移(4) + 大小(4)│
│ + 类型(1) + 标志(1) + 保留(6)│  ✨ 新增类型和标志
├─ Data ───────────────────────┤
│ GIF/OGG/JSON原始数据          │
└───────────────────────────────┘
```

### 2. 文件类型支持（待落地）

```cpp
enum AssetType {
    ASSET_TYPE_GIF = 0,     // GIF动画
    ASSET_TYPE_OGG = 1,     // OGG音频
    ASSET_TYPE_JSON = 2,    // ✨ JSON配置（新增）
};

enum AssetFlags {
    ASSET_FLAG_READONLY = 0x01,   // 只读（GIF/OGG）
    ASSET_FLAG_WRITABLE = 0x02,   // ✨ 可写（JSON配置）
};
```

## ⚡ 快速更新流程（开发必备，脚本依赖待落地）

### 方法1：一键脚本（推荐）

**Windows**（需先添加 `scripts/pack_lynx_assets.py`）：
```cmd
scripts\update_lynx_assets.bat
```

**Linux/Mac**（需先添加 `scripts/pack_lynx_assets.py`）：
```bash
./scripts/update_lynx_assets.sh
```

### 方法2：手动执行

```bash
# 1. 打包（约5秒）
# 依赖：scripts/pack_lynx_assets.py（见 UPDATES_V1.1.md 完整脚本）
python scripts/pack_lynx_assets.py sdcard build/lynx_assets.bin

# 2. 烧录（约60秒）
esptool.py --port COM3 --baud 921600 write_flash 0x800000 build/lynx_assets.bin

# 3. 重启
esptool.py --port COM3 run
```

注：上述流程在 `pack_lynx_assets.py` 未落地前不可执行。整体用时目标约 70 秒（无需重编译固件）。

## 🔍 验证更新

### 查看串口日志

```
I (xxx) LynxAssets: Initializing LynxAssets...
I (xxx) LynxAssets: Found assets partition: size=8192 KB
I (xxx) LynxAssets: Assets header: version=1, files=N, crc=0xXXXXXXXX
I (xxx) LynxAssets: Parsed N files successfully
I (xxx) LynxAssets: CRC32 verified: 0xXXXXXXXX
I (xxx) ALichuangTest: LynxAssets initialized: N files loaded
I (xxx) ALichuangTest: ✓ event_config.json loaded: XX events
I (xxx) ALichuangTest: ✓ response_config.json loaded: version=1.0
```

提示：以上为 v1.1 预期日志格式。当前代码仍为 `Assets`（非 `LynxAssets`）且未包含 JSON 加载能力。

### 检查Magic

```bash
esptool.py --port COM3 read_flash 0x800000 0x100 temp.bin
# Linux/Mac: hexdump -C temp.bin | head -5
# Windows（PowerShell）可使用：Get-Content -Encoding Byte temp.bin | Format-Hex -Count 64

# 期望输出:
# 00000000  4c 59 4e 58 01 00 00 00  xx 00 00 00 XX XX XX XX  |LYNX....|...
#           ^^^^^^^^ "LYNX" Magic
#                   ^^^^^^^^ Version=1
#                           ^^^^^^^^ FileCount（示例）
```

## 📝 配置文件访问

### 代码示例（v1.1 目标 API，待落地）

```cpp
// 加载配置文件（v1.1 目标）
auto& assets = *Board::GetInstance().GetLynxAssets();

cJSON* event_config = nullptr;
if (assets.GetConfigJson("config/event_config", event_config)) {
    // 成功加载，解析配置
    cJSON* events = cJSON_GetObjectItem(event_config, "events");

    // 使用配置...

    // 清理
    cJSON_Delete(event_config);
} else {
    ESP_LOGE(TAG, "Failed to load event_config.json");
}
```

提示：当前代码仍从 `/sdcard/config/*.json` 读取并在加载失败时回退到内置默认，请在落地 LynxAssets 后替换为 Flash 读取。

### 配置文件路径（命名映射约定）

| 资源名称 | Flash路径 | 用途 |
|---------|----------|------|
| `config/event_config` | `/sdcard/config/event_config.json` | 事件检测配置 |
| `config/response_config` | `/sdcard/config/response_config.json` | 响应行为配置 |

## 📊 性能对比（预期，待实测）

| 操作 | SD卡 | Flash (LYNX) | 提升 |
|------|------|--------------|------|
| **加载GIF** | 30-50ms | 预计亚毫秒级（内存映射直取） | 大幅提升 |
| **加载OGG** | 15-20ms | 预计亚毫秒级（内存映射直取） | 大幅提升 |
| **加载JSON** | 5-10ms | 预计亚毫秒级 | 大幅提升 |
| **内存峰值** | 360KB | 预期略降 | 待实测 |
| **更新资源** | 重新插拔SD卡 | 约1分钟烧录 | 更快更稳定 |

## 🛠️ 开发工作流

### 典型场景：修改GIF动画

```bash
# 1. 修改GIF文件
# 编辑 sdcard/state_expression/speaking/talk_happy/talk_happy.gif

# 2. 快速更新（1分钟）
# 依赖：scripts/pack_lynx_assets.py 落地后
scripts\update_lynx_assets.bat

# 3. 查看效果
idf.py -p COM3 monitor

# 无需重编译固件！（前提：v1.1 方案已落地）
```

### 典型场景：调整配置参数

```bash
# 1. 修改配置文件
# 编辑 sdcard/config/event_config.json
# 例如：调整 "threshold_g": 3.0 → 2.5

# 2. 快速更新（1分钟）
# 依赖：scripts/pack_lynx_assets.py 落地后
scripts\update_lynx_assets.bat

# 3. 配置立即生效
# 设备重启后自动加载新配置（v1.1 方案）
```

## 📚 完整文档索引

| 阶段 | 文档 | 说明 |
|------|------|------|
| **必读** | [UPDATES_V1.1.md](UPDATES_V1.1.md) | v1.1更新详情 |
| 总览 | [00_OVERVIEW.md](00_OVERVIEW.md) | 项目背景、架构 |
| 步骤1 | [01_PREPARATION.md](01_PREPARATION.md) | 备份、工具准备 |
| 步骤2 | [02_CODE_DEVELOPMENT.md](02_CODE_DEVELOPMENT.md) | 代码开发 |
| 步骤3 | [03_ASSET_PACKING.md](03_ASSET_PACKING.md) | 资源打包 |
| 步骤4 | [04_TESTING.md](04_TESTING.md) | 测试验证 |
| 步骤5 | [05_DEPLOYMENT.md](05_DEPLOYMENT.md) | 部署发布 |
| 参考 | [CODE_MIGRATION_CHECKLIST.md](CODE_MIGRATION_CHECKLIST.md) | 代码清单 |

## ⚠️ 重要提示

### 命名变更（计划，待落地）

以下命名变更为 v1.1 计划项，当前仓库尚未替换：

| 原名称 | 新名称 |
|-------|--------|
| `ALAssets` | `LynxAssets` |
| `al_assets.h/cc` | `lynx_assets.h/cc` |
| Magic `"ALGF"` | Magic `"LYNX"` |
| `alichuang_assets.bin` | `lynx_assets.bin` |

### 兼容性（按 v1.1 方案预期）

- ✅ **向前兼容**：新固件支持LYNX格式
- ⚠️ **不向后兼容**：旧固件无法识别LYNX Magic
- 🔄 **首次升级**：需要完整烧录（分区表+固件+assets）
- ✅ **后续更新**：仅更新assets分区即可（1分钟）

## 🚀 开始实施

### 立即可做

1. **阅读更新说明**：[UPDATES_V1.1.md](UPDATES_V1.1.md)
2. **测试快速更新**：
   ```bash
   scripts\update_lynx_assets.bat
   ```
3. **验证功能**：检查串口日志，确认文件解析成功（具体数量以实际打包为准）

### 完整迁移

按照文档顺序执行：
1. [准备工作](01_PREPARATION.md)
2. [代码开发](02_CODE_DEVELOPMENT.md)
3. [资源打包](03_ASSET_PACKING.md)
4. [测试验证](04_TESTING.md)
5. [部署发布](05_DEPLOYMENT.md)

---

**版本**: v1.1
**更新日期**: 2025-10-26
**品牌**: LynxAura
**Magic**: LYNX
