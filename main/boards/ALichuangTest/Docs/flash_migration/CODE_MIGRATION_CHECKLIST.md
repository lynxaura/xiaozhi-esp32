# 代码迁移清单

## 文档概述

本清单详细列出了Flash迁移所需的所有代码修改点，便于开发者逐项检查和实施。

---

## 新增文件

### 1. LynxAssets类

| 文件路径 | 说明 | 行数估计 |
|---------|------|---------|
| `main/boards/ALichuangTest/lynx_assets.h` | LynxAssets类头文件 | ~70行 |
| `main/boards/ALichuangTest/lynx_assets.cc` | LynxAssets类实现 | ~200行 |

**关键功能**：
- [x] Flash分区映射 (`esp_partition_mmap`)
- [x] 64字节索引表解析（含类型/标志）
- [x] CRC32完整性校验
- [x] 零拷贝资源访问 (`GetResource`)

**依赖项**：
- `esp_partition.h`
- `esp_log.h`
- `zlib.h` (for CRC32)

### 2. 打包工具（v1.1 LYNX）

| 文件路径 | 说明 | 行数估计 |
|---------|------|---------|
| `scripts/pack_lynx_assets.py` | 资源打包脚本 | ~200行 |

**功能**：
- [x] 递归扫描GIF/OGG/JSON文件
- [x] 生成64字节索引表（含类型/标志）
- [x] 计算CRC32校验和
- [x] 输出.bin文件

### 3. 文档

| 文件路径 | 说明 |
|---------|------|
| `main/boards/ALichuangTest/Docs/flash_migration/00_OVERVIEW.md` | 总览 |
| `main/boards/ALichuangTest/Docs/flash_migration/01_PREPARATION.md` | 准备工作 |
| `main/boards/ALichuangTest/Docs/flash_migration/02_CODE_DEVELOPMENT.md` | 代码开发 |
| `main/boards/ALichuangTest/Docs/flash_migration/03_ASSET_PACKING.md` | 资源打包 |
| `main/boards/ALichuangTest/Docs/flash_migration/04_TESTING.md` | 测试验证 |
| `main/boards/ALichuangTest/Docs/flash_migration/05_DEPLOYMENT.md` | 部署发布 |
| `main/boards/ALichuangTest/Docs/FLASH_UPGRADE_GUIDE.md` | 用户升级指南 |

---

## 修改文件

### 1. animation.cc - GIF加载逻辑（优先Flash）

**文件路径**：`main/boards/ALichuangTest/skills/animation.cc`

#### 修改点1：SetAnima() 方法

**位置**：第216行附近

**原代码**：
```cpp
void AnimaDisplay::SetAnima(const std::string& animation, int loop_count) {
    DisplayLockGuard lock(this);

    int effective_loops = (loop_count <= 0) ? 0 : loop_count;

    // 使用哈希表快速查找
    auto it = animation_maps_.find(animation);
    const char* gif_path = nullptr;

    if (it != animation_maps_.end()) {
        gif_path = it->second;  // SD卡路径
    } else {
        gif_path = DEFAULT_GIF_PATH;
    }

    // 从SD卡加载
    lv_gif_set_src(animation_gif_, gif_path);
    // ...
}
```

**新代码**：
```cpp
void AnimaDisplay::SetAnima(const std::string& animation, int loop_count) {
    DisplayLockGuard lock(this);

    int effective_loops = (loop_count <= 0) ? 0 : loop_count;

    // ===== 新增：优先从Flash加载 =====
    auto& board = Board::GetInstance();
    auto& assets = *Board::GetInstance().GetLynxAssets();

        if (assets.IsPartitionValid() && assets.IsChecksumValid()) {
            std::string resource_name = ConstructResourceName(animation);

            void* gif_data = nullptr;
            size_t gif_size = 0;

            if (assets.GetResource(resource_name, gif_data, gif_size)) {
                ESP_LOGI(TAG, "Loading GIF from Flash: %s (%zu bytes)",
                         resource_name.c_str(), gif_size);

                LoadGifFromMemory(gif_data, gif_size, effective_loops);
                return;  // 成功加载，直接返回
            }
        }
    }

    // ===== 移除：SD卡降级逻辑 =====
    ESP_LOGE(TAG, "Failed to load GIF from Flash: %s", animation.c_str());
    // 可选：显示错误占位图
}
```

**检查点**：
- [ ] 添加Flash优先加载逻辑
- [ ] 移除SD卡降级代码
- [ ] 添加错误处理

#### 修改点2：新增辅助方法

**位置**：animation.cc 末尾

**新增代码**：
```cpp
std::string AnimaDisplay::ConstructResourceName(const std::string& animation) {
    auto it = animation_maps_.find(animation);
    if (it != animation_maps_.end()) {
        std::string sd_path = it->second;

        // 移除 "/sdcard/" 前缀
        if (sd_path.substr(0, 8) == "/sdcard/") {
            sd_path = sd_path.substr(8);
        }

        // 移除扩展名 ".gif"
        size_t dot_pos = sd_path.rfind('.');
        if (dot_pos != std::string::npos) {
            sd_path = sd_path.substr(0, dot_pos);
        }

        return sd_path;
    }

    return animation;
}

void AnimaDisplay::LoadGifFromMemory(void* data, size_t size, int loop_count) {
    // 删除旧GIF对象
    if (animation_gif_ != nullptr) {
        lv_obj_delete(animation_gif_);
        animation_gif_ = nullptr;
    }

    // 创建新GIF对象
    auto screen = lv_screen_active();
    animation_gif_ = lv_gif_create(screen);
    if (animation_gif_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create GIF object");
        return;
    }

    // 配置GIF对象
    lv_obj_set_size(animation_gif_, width_, height_);
    lv_obj_set_style_border_width(animation_gif_, 0, 0);
    lv_obj_set_style_bg_opa(animation_gif_, LV_OPA_TRANSP, 0);
    lv_obj_center(animation_gif_);

    // 使用内存数据源
    lv_img_dsc_t img_dsc = {
        .header = {
            .cf = LV_COLOR_FORMAT_UNKNOWN,
            .flags = 0,
            .w = 0,
            .h = 0
        },
        .data = static_cast<const uint8_t*>(data),
        .data_size = static_cast<uint32_t>(size)
    };

    lv_gif_set_src(animation_gif_, &img_dsc);

    if (!lv_gif_is_loaded(animation_gif_)) {
        ESP_LOGE(TAG, "Failed to load GIF from memory");
        return;
    }

    lv_gif_set_loop_count(animation_gif_, loop_count);
    ESP_LOGD(TAG, "GIF loaded from Flash successfully");
}
```

**检查点**：
- [ ] `ConstructResourceName()` 实现正确
- [ ] `LoadGifFromMemory()` 实现正确
- [ ] LVGL内存数据源设置正确

#### 修改点3：更新头文件

**文件路径**：`main/boards/ALichuangTest/skills/animation.h`

**位置**：private 部分

**新增声明**：
```cpp
private:
    std::string ConstructResourceName(const std::string& animation);
    void LoadGifFromMemory(void* data, size_t size, int loop_count);
```

**检查点**：
- [ ] 头文件声明添加

---

### 2. application.cc - OGG加载逻辑（优先Flash）

**文件路径**：`main/application.cc`

#### 修改点1：PlaySoundOGGFile() 方法

**位置**：第959行附近

**原代码**：
```cpp
void Application::PlaySoundOGGFile(const std::string& audio_name, int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    auto it = audio_file_maps_.find(audio_name);
    if (it == audio_file_maps_.end()) {
        ESP_LOGW(TAG, "Unknown audio: '%s'", audio_name.c_str());
        return;
    }

    const char* filePath = it->second;
    FILE *f = fopen(filePath, "r");  // SD卡文件
    // ... 读取文件到内存 ...
    fread(databuf, 1, file_size, f);
    // ...
    audio_service_.PlaySound(ogg);
    fclose(f);
    free(databuf);
}
```

**新代码**：
```cpp
void Application::PlaySoundOGGFile(const std::string& audio_name, int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    // ===== 新增：优先从Flash加载 =====
    auto& board = Board::GetInstance();
    auto& assets = *Board::GetInstance().GetLynxAssets();

        if (assets.IsPartitionValid() && assets.IsChecksumValid()) {
            std::string resource_name = ConstructOggResourceName(audio_name);

            void* ogg_data = nullptr;
            size_t ogg_size = 0;

            if (assets.GetResource(resource_name, ogg_data, ogg_size)) {
                ESP_LOGI(TAG, "Playing OGG from Flash: %s (%zu bytes, volume=%d)",
                         resource_name.c_str(), ogg_size, volume);

                // 零拷贝：直接使用Flash数据
                const std::string_view ogg_view(
                    static_cast<const char*>(ogg_data),
                    ogg_size
                );

                // 临时调整音量
                auto codec = board.GetAudioCodec();
                int original_volume = codec->output_volume();
                codec->SetOutputVolume(volume);

                audio_service_.PlaySound(ogg_view);

                // 恢复音量
                codec->SetOutputVolume(original_volume);

                return;  // 成功播放
            }
        }
    }

    // ===== 移除：SD卡降级逻辑 =====
    ESP_LOGE(TAG, "Failed to load OGG from Flash: %s", audio_name.c_str());
}
```

**检查点**：
- [ ] 添加Flash优先加载逻辑
- [ ] 使用零拷贝 `std::string_view`
- [ ] 移除SD卡文件读取代码
- [ ] 移除 `malloc/free` 调用

#### 修改点2：新增辅助方法

**位置**：application.cc 私有方法

**新增代码**：
```cpp
std::string Application::ConstructOggResourceName(const std::string& audio_name) {
    auto it = audio_file_maps_.find(audio_name);
    if (it != audio_file_maps_.end()) {
        std::string sd_path = it->second;

        // 移除 "/sdcard/" 前缀
        if (sd_path.substr(0, 8) == "/sdcard/") {
            sd_path = sd_path.substr(8);
        }

        // 移除扩展名 ".ogg"
        size_t dot_pos = sd_path.rfind('.');
        if (dot_pos != std::string::npos) {
            sd_path = sd_path.substr(0, dot_pos);
        }

        return sd_path;
    }

    return audio_name;
}
```

**检查点**：
- [ ] `ConstructOggResourceName()` 实现正确

#### 修改点3：更新头文件

**文件路径**：`main/application.h`

**位置**：private 部分

**新增声明**：
```cpp
private:
    std::string ConstructOggResourceName(const std::string& audio_name);
```

**检查点**：
- [ ] 头文件声明添加

---

### 3. ALichuangTest.cc - 板级初始化

**文件路径**：`main/boards/ALichuangTest/ALichuangTest.cc`

#### 修改点1：添加头文件

**位置**：文件开头

**新增代码**：
```cpp
#include "lynx_assets.h"
```

**检查点**：
- [ ] 头文件添加

#### 修改点2：添加成员变量

**位置**：ALichuangTest 类定义 private 部分

**新增代码**：
```cpp
private:
    // ... 现有成员 ...

    // Flash资源管理器
    std::unique_ptr<lynxaura::LynxAssets> lynx_assets_;
```

**检查点**：
- [ ] 成员变量添加

#### 修改点3：修改构造函数

**位置**：ALichuangTest 构造函数

**移除代码**：
```cpp
// 移除SD卡初始化
// SDdata_Pro* sd_card = new SDdata_Pro();  // 删除这行
```

**新增代码**：
```cpp
ALichuangTest() : WifiBoard() {
    // ... 现有初始化代码 ...

    // ===== 移除：SD卡初始化 =====
    // SDdata_Pro* sd_card = new SDdata_Pro();  // 已删除

    // ===== 新增：初始化LynxAssets =====
    lynx_assets_ = std::make_unique<lynxaura::LynxAssets>();
    if (!lynx_assets_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize LynxAssets");
    } else {
        ESP_LOGI(TAG, "LynxAssets initialized: %lu files loaded",
                 lynx_assets_->GetFileCount());
    }

    // ... 其他初始化代码 ...
}
```

**检查点**：
- [ ] 移除SD卡初始化代码
- [ ] 添加LynxAssets初始化代码
- [ ] 添加初始化日志

#### 修改点4：添加访问接口

**位置**：ALichuangTest 类 public 部分

**新增代码**：
```cpp
public:
    // 获取Flash资源管理器
    lynxaura::LynxAssets* GetLynxAssets() const {
        return lynx_assets_.get();
    }
```

**检查点**：
- [ ] 访问接口添加

#### 修改点5：移除SD卡头文件

**位置**：文件开头

**移除代码**：
```cpp
// #include "sddata_pro.h"  // 删除或注释这行
```

**检查点**：
- [ ] SD卡头文件移除

---

### 4. config.json - 分区表配置

**文件路径**：`main/boards/ALichuangTest/config.json`

#### 完整替换内容

**新配置**：
```json
{
    "target": "esp32s3",
    "builds": [
        {
            "name": "ALichuangTest-flash-v2",
            "sdkconfig_append": [
                "CONFIG_USE_DEVICE_AEC=y",
                "CONFIG_PARTITION_TABLE_CUSTOM=y",
                "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME=\"partitions/v2/16m.csv\"",
                "CONFIG_PARTITION_TABLE_FILENAME=\"partitions/v2/16m.csv\"",
                "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
                "CONFIG_ESPTOOLPY_FLASHSIZE=\"16MB\"",
                "CONFIG_OTA_URL=\"http://101.126.79.22:8002/xiaozhi/ota/\""
            ]
        }
    ]
}
```

**变更点**：
- [x] 构建名称更新：`ALichuangTest` → `ALichuangTest-flash-v2`
- [x] 分区表路径更新：`partitions/v1/16m_anima.csv` → `partitions/v2/16m.csv`

**检查点**：
- [ ] 分区表路径正确
- [ ] 构建名称更新

---

### 5. CMakeLists.txt - 构建配置

**文件路径**：`main/boards/ALichuangTest/CMakeLists.txt`

如果文件不存在，创建新文件：

```cmake
# ALichuangTest 板级组件

# 添加源文件
target_sources(${COMPONENT_LIB} PRIVATE
    "al_assets.cc"
    "ALichuangTest.cc"
    # ... 其他源文件
)

# 包含头文件目录
target_include_directories(${COMPONENT_LIB} PUBLIC
    "."
)

# 链接zlib（用于CRC32）
target_link_libraries(${COMPONENT_LIB} PRIVATE
    idf::esp_partition
    z
)
```

**检查点**：
- [ ] `al_assets.cc` 添加到源文件列表
- [ ] zlib链接正确

---

## 删除/移除代码

### 1. SD卡相关代码

| 位置 | 操作 | 检查 |
|------|------|------|
| `ALichuangTest.cc` | 删除 `#include "sddata_pro.h"` | [ ] |
| `ALichuangTest.cc` | 删除 `SDdata_Pro* sd_card = new SDdata_Pro();` | [ ] |
| `animation.cc` | 删除SD卡降级逻辑 | [ ] |
| `application.cc` | 删除SD卡文件读取代码 | [ ] |

### 2. 硬编码路径常量（可选）

| 位置 | 操作 | 检查 |
|------|------|------|
| `animation.cc` | 更新 `BOOT_GIF_PATH` 为 Flash资源名 | [ ] |
| `animation.cc` | 更新 `DEFAULT_GIF_PATH` 为 Flash资源名 | [ ] |

**建议**：保留 `animation_maps_` 映射表，用于路径转换。

---

## 编译验证

### 编译命令

```bash
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

### 编译成功标准

- [ ] 无编译错误
- [ ] 无链接错误
- [ ] 固件大小 <4MB
- [ ] 所有新增文件参与编译

### 符号检查

```bash
# 检查LynxAssets类符号
xtensa-esp32s3-elf-nm build/xiaozhi.elf | grep LynxAssets

# 期望输出:
# 400xxxxx T lynxaura::LynxAssets::Initialize()
# 400xxxxx T lynxaura::LynxAssets::GetResource(...)
```

---

## 测试清单

### 单元测试

| 测试项 | 描述 | 状态 |
|-------|------|------|
| Flash映射 | `LynxAssets::Initialize()` 成功 | [ ] |
| 索引解析 | 103个文件正确解析 | [ ] |
| CRC32校验 | 校验和匹配 | [ ] |
| 资源访问 | `GetResource()` 返回正确数据 | [ ] |

### 集成测试

| 测试项 | 描述 | 状态 |
|-------|------|------|
| GIF加载 | 所有73个GIF正常播放 | [ ] |
| OGG播放 | 所有30个OGG正常播放 | [ ] |
| 联合播放 | GIF+OGG同时播放无冲突 | [ ] |

---

## 代码审查检查点

### 代码质量

- [ ] 所有新代码添加了注释
- [ ] 遵循项目编码规范
- [ ] 无硬编码魔数
- [ ] 错误处理完善

### 资源管理

- [ ] 无内存泄漏
- [ ] RAII模式正确使用
- [ ] mmap_handle 正确释放

### 性能

- [ ] 零拷贝实现正确
- [ ] 无不必要的内存分配
- [ ] 索引查找使用哈希表（O(1)）

### 兼容性

- [ ] 向后兼容考虑（错误时的降级处理）
- [ ] Flash损坏时的容错
- [ ] 日志级别合理

---

## 提交清单

### Git Commit

建议按以下顺序提交：

1. **Commit 1**: 添加LynxAssets类
   ```bash
   git add main/boards/ALichuangTest/lynx_assets.*
   git commit -m "feat(ALichuangTest): add LynxAssets class for Flash resource management"
   ```

2. **Commit 2**: 添加打包工具
   ```bash
   git add scripts/pack_lynx_assets.py
   git commit -m "feat(tools): add LYNX asset packing tool"
   ```

3. **Commit 3**: 修改AnimaDisplay
   ```bash
   git add main/boards/ALichuangTest/skills/animation.*
   git commit -m "feat(ALichuangTest): support Flash GIF loading in AnimaDisplay"
   ```

4. **Commit 4**: 修改Application
   ```bash
   git add main/application.*
   git commit -m "feat(ALichuangTest): support Flash OGG playback in Application"
   ```

5. **Commit 5**: 更新板级初始化
   ```bash
   git add main/boards/ALichuangTest/ALichuangTest.cc
   git commit -m "feat(ALichuangTest): migrate from SD card to Flash assets

- Initialize LynxAssets in constructor
- Remove SDdata_Pro initialization
- Add GetLynxAssets() accessor
"
   ```

6. **Commit 6**: 更新配置文件
   ```bash
   git add main/boards/ALichuangTest/config.json
   git commit -m "config(ALichuangTest): update partition table to v2"
   ```

7. **Commit 7**: 添加文档
   ```bash
   git add main/boards/ALichuangTest/Docs/flash_migration/
   git commit -m "docs(ALichuangTest): add Flash migration guide"
   ```

---

## 最终验收标准

### 功能验收

- [ ] 所有103个资源文件正常加载
- [ ] GIF动画播放流畅
- [ ] OGG音频播放正常
- [ ] 无SD卡依赖

### 性能验收

- [ ] GIF加载延迟 <5ms
- [ ] OGG加载延迟 <1ms
- [ ] 内存峰值 <350KB
- [ ] 内存波动 <±30KB

### 稳定性验收

- [ ] 长时间运行无崩溃（1000次迭代）
- [ ] 内存泄漏 <10KB
- [ ] 快速切换无问题

### 文档验收

- [ ] 用户升级指南完整
- [ ] 技术文档完整
- [ ] 代码注释充分

---

**全部检查项通过后，项目迁移完成！**
