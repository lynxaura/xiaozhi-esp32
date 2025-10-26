# 步骤2：代码开发

## 目标

本阶段实现所有代码修改，包括LynxAssets类、动画/音频加载适配、以及移除SD卡依赖。

## 任务清单

- [ ] 2.1 创建LynxAssets类（Flash管理）
- [ ] 2.2 修改AnimaDisplay支持Flash GIF加载
- [ ] 2.3 修改Application支持Flash OGG播放
- [ ] 2.4 更新ALichuangTest板级初始化
- [ ] 2.5 移除SD卡依赖代码
- [ ] 2.6 更新配置文件

---

## 2.1 创建LynxAssets类

### 文件结构

创建两个新文件：
- `main/boards/ALichuangTest/lynx_assets.h` - 头文件
- `main/boards/ALichuangTest/lynx_assets.cc` - 实现文件

### 头文件：lynx_assets.h

**文件路径**：`main/boards/ALichuangTest/lynx_assets.h`

```cpp
#ifndef LYNX_ASSETS_H
#define LYNX_ASSETS_H

#include <esp_partition.h>
#include <unordered_map>
#include <string>

namespace lynxaura {

/**
 * LynxAssets - ALichuangTest Flash资源管理器
 *
 * 功能：
 * - 映射Flash assets分区到虚拟内存（esp_partition_mmap）
 * - 解析64字节索引表
 * - 提供零拷贝资源访问接口（GetResource）
 * - 支持CRC32完整性校验
 *
 * 使用示例：
 *   LynxAssets assets;
 *   if (assets.Initialize()) {
 *       void* gif_data;
 *       size_t gif_size;
 *       if (assets.GetResource("emergency/motion_shake_violently/motion_shake_violently",
 *                              gif_data, gif_size)) {
 *           // 使用gif_data（指向Flash映射内存，无需释放）
 *       }
 *   }
 */
class LynxAssets {
public:
    LynxAssets();
    ~LynxAssets();

    /**
     * 初始化Flash分区并解析索引
     * @return true=成功, false=失败
     */
    bool Initialize();

    /**
     * 获取资源数据（零拷贝）
     * @param name 资源名称（不含扩展名），如 "emergency/motion_shake_violently/motion_shake_violently"
     * @param data [out] 输出数据指针（指向Flash映射内存）
     * @param size [out] 输出数据大小
     * @return true=找到, false=未找到
     */
    bool GetResource(const std::string& name, void*& data, size_t& size);

    /**
     * 检查分区是否有效
     */
    inline bool IsPartitionValid() const { return partition_valid_; }

    /**
     * 检查CRC32校验是否通过
     */
    inline bool IsChecksumValid() const { return checksum_valid_; }

    /**
     * 获取资源文件总数
     */
    inline uint32_t GetFileCount() const { return file_count_; }

private:
    LynxAssets(const LynxAssets&) = delete;
    LynxAssets& operator=(const LynxAssets&) = delete;

    bool MapPartition();
    bool ParseIndex();
    bool VerifyChecksum();

    // Flash分区数据结构
    struct FileEntry {
        uint32_t offset;  // 相对于数据区的偏移量
        uint32_t size;    // 文件大小
    };

    // Flash分区状态
    const esp_partition_t* partition_ = nullptr;
    esp_partition_mmap_handle_t mmap_handle_ = 0;
    const uint8_t* mmap_root_ = nullptr;
    bool partition_valid_ = false;
    bool checksum_valid_ = false;

    // 索引表
    uint32_t file_count_ = 0;
    std::unordered_map<std::string, FileEntry> files_;
};

}  // namespace lynxaura

#endif  // LYNX_ASSETS_H
```

### 实现文件：lynx_assets.cc

**文件路径**：`main/boards/ALichuangTest/lynx_assets.cc`

```cpp
#include "lynx_assets.h"

#include <esp_log.h>
#include <cstring>
#include <zlib.h>

#define TAG "LynxAssets"

// Flash分区格式常量
#define LYNX_MAGIC "LYNX"
#define ENTRY_SIZE 64
#define NAME_SIZE 48
#define HEADER_SIZE 16

namespace lynxaura {

LynxAssets::LynxAssets() {
    // 构造函数不执行耗时操作，延迟到Initialize()
}

LynxAssets::~LynxAssets() {
    if (mmap_handle_ != 0) {
        esp_partition_munmap(mmap_handle_);
        mmap_handle_ = 0;
    }
}

bool LynxAssets::Initialize() {
    ESP_LOGI(TAG, "Initializing LynxAssets...");

    if (!MapPartition()) {
        return false;
    }

    if (!ParseIndex()) {
        return false;
    }

    if (!VerifyChecksum()) {
        ESP_LOGW(TAG, "Checksum verification failed, assets may be corrupted");
        // 不直接返回false，允许在校验失败时继续使用（调试时有用）
    }

    ESP_LOGI(TAG, "LynxAssets initialized successfully: %lu files", file_count_);
    return true;
}

bool LynxAssets::MapPartition() {
    // 1. 查找assets分区
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                         ESP_PARTITION_SUBTYPE_ANY,
                                         "assets");
    if (!partition_) {
        ESP_LOGE(TAG, "Assets partition not found");
        return false;
    }

    ESP_LOGI(TAG, "Found assets partition: size=%lu KB", partition_->size / 1024);

    // 2. 检查mmap可用页数
    int free_pages = spi_flash_mmap_get_free_pages(SPI_FLASH_MMAP_DATA);
    uint32_t free_size = free_pages * 64 * 1024;
    ESP_LOGI(TAG, "Free mmap pages: %d (%.2f MB)", free_pages, free_size / 1024.0 / 1024.0);

    if (free_size < partition_->size) {
        ESP_LOGE(TAG, "Insufficient mmap pages: required %.2f MB, available %.2f MB",
                 partition_->size / 1024.0 / 1024.0, free_size / 1024.0 / 1024.0);
        return false;
    }

    // 3. 映射整个分区到虚拟地址空间
    esp_err_t err = esp_partition_mmap(partition_, 0, partition_->size,
                                      ESP_PARTITION_MMAP_DATA,
                                      (const void**)&mmap_root_,
                                      &mmap_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap partition: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Partition mapped to virtual address: %p", mmap_root_);
    partition_valid_ = true;
    return true;
}

bool LynxAssets::ParseIndex() {
    if (!partition_valid_) {
        return false;
    }

    // 验证Magic
    if (memcmp(mmap_root_, LYNX_MAGIC, 4) != 0) {
        ESP_LOGE(TAG, "Invalid magic: expected 'LYNX', got '%.4s'", mmap_root_);
        return false;
    }

    // 读取Header
    uint32_t version = *(uint32_t*)(mmap_root_ + 4);
    file_count_ = *(uint32_t*)(mmap_root_ + 8);
    uint32_t stored_crc = *(uint32_t*)(mmap_root_ + 12);

    ESP_LOGI(TAG, "Assets header: version=%lu, files=%lu, crc=0x%08lX",
             version, file_count_, stored_crc);

    if (version != 1) {
        ESP_LOGE(TAG, "Unsupported version: %lu (expected 1)", version);
        return false;
    }

    if (file_count_ == 0 || file_count_ > 10000) {  // 合理性检查
        ESP_LOGE(TAG, "Invalid file count: %lu", file_count_);
        return false;
    }

    // 解析文件索引表
    const uint8_t* index_table = mmap_root_ + HEADER_SIZE;
    const uint32_t data_area_start = HEADER_SIZE + file_count_ * ENTRY_SIZE;

    files_.clear();
    for (uint32_t i = 0; i < file_count_; i++) {
        const uint8_t* entry = index_table + i * ENTRY_SIZE;

        // 提取文件名（前48字节）
        char name_buf[NAME_SIZE + 1] = {0};
        memcpy(name_buf, entry, NAME_SIZE);
        std::string name(name_buf);  // 自动在\0处截断

        // 提取偏移量和大小
        uint32_t offset = *(uint32_t*)(entry + 48);
        uint32_t size = *(uint32_t*)(entry + 52);

        // 验证偏移量合法性
        if (data_area_start + offset + size > partition_->size) {
            ESP_LOGE(TAG, "File %lu '%s' exceeds partition boundary", i, name.c_str());
            continue;
        }

        // 添加到映射表
        files_[name] = FileEntry{offset, size};

        ESP_LOGD(TAG, "  [%lu] %s: offset=%lu, size=%lu", i, name.c_str(), offset, size);
    }

    ESP_LOGI(TAG, "Parsed %lu files successfully", files_.size());
    return true;
}

bool LynxAssets::VerifyChecksum() {
    if (!partition_valid_ || file_count_ == 0) {
        return false;
    }

    // 读取存储的CRC32
    uint32_t stored_crc = *(uint32_t*)(mmap_root_ + 12);

    // 计算数据区的CRC32
    const uint32_t data_area_start = HEADER_SIZE + file_count_ * ENTRY_SIZE;
    const uint32_t data_size = partition_->size - data_area_start;
    const uint8_t* data_area = mmap_root_ + data_area_start;

    ESP_LOGI(TAG, "Calculating CRC32 for %lu KB data...", data_size / 1024);
    auto start_time = esp_timer_get_time();

    uint32_t calculated_crc = crc32(0L, data_area, data_size);

    auto end_time = esp_timer_get_time();
    ESP_LOGI(TAG, "CRC32 calculation took %lld ms", (end_time - start_time) / 1000);

    if (calculated_crc != stored_crc) {
        ESP_LOGE(TAG, "CRC32 mismatch: stored=0x%08lX, calculated=0x%08lX",
                 stored_crc, calculated_crc);
        checksum_valid_ = false;
        return false;
    }

    ESP_LOGI(TAG, "CRC32 verified: 0x%08lX", calculated_crc);
    checksum_valid_ = true;
    return true;
}

bool LynxAssets::GetResource(const std::string& name, void*& data, size_t& size) {
    if (!partition_valid_) {
        ESP_LOGW(TAG, "Partition not initialized");
        return false;
    }

    auto it = files_.find(name);
    if (it == files_.end()) {
        ESP_LOGD(TAG, "Resource not found: %s", name.c_str());
        return false;
    }

    const FileEntry& entry = it->second;
    const uint32_t data_area_start = HEADER_SIZE + file_count_ * ENTRY_SIZE;

    // 返回Flash映射地址（零拷贝）
    data = const_cast<uint8_t*>(mmap_root_ + data_area_start + entry.offset);
    size = entry.size;

    ESP_LOGD(TAG, "Resource '%s': %p, %zu bytes", name.c_str(), data, size);
    return true;
}

}  // namespace lynxaura
```

### 添加到CMakeLists.txt

修改 `main/boards/ALichuangTest/CMakeLists.txt`（如果不存在则创建）：

```cmake
# ALichuangTest板级组件

# 添加lynx_assets源文件
target_sources(${COMPONENT_LIB} PRIVATE
    "lynx_assets.cc"
)

# 包含头文件目录
target_include_directories(${COMPONENT_LIB} PUBLIC
    "."
)
```

---

## 2.2 修改AnimaDisplay支持Flash GIF加载

### 修改文件：skills/animation.cc

**关键修改点**：`SetAnima()` 方法

在 `main/boards/ALichuangTest/skills/animation.cc` 的 `SetAnima()` 方法中：

```cpp
void AnimaDisplay::SetAnima(const std::string& animation, int loop_count) {
    DisplayLockGuard lock(this);

    int effective_loops = (loop_count <= 0) ? 0 : loop_count;

    // ========== 新增：优先从Flash加载 ==========
    auto& board = Board::GetInstance();
    auto& assets = *Board::GetInstance().GetLynxAssets();

        if (assets.IsPartitionValid() && assets.IsChecksumValid()) {
            // 构造资源名称（不含扩展名）
            std::string resource_name = ConstructResourceName(animation);

            void* gif_data = nullptr;
            size_t gif_size = 0;

            if (assets.GetResource(resource_name, gif_data, gif_size)) {
                ESP_LOGI(TAG, "Loading GIF from Flash: %s (%zu bytes)",
                         resource_name.c_str(), gif_size);

                // 使用LVGL的内存数据源
                LoadGifFromMemory(gif_data, gif_size, effective_loops);
                return;  // 成功从Flash加载，直接返回
            } else {
                ESP_LOGW(TAG, "Resource not found in Flash: %s", resource_name.c_str());
            }
        }
    }

    // ========== 保留：降级到SD卡（可选） ==========
    // 如果完全移除SD卡，可以删除以下代码
    ESP_LOGE(TAG, "Flash资源加载失败，且已移除SD卡支持");
    // 显示错误占位图或默认动画
    // ...
}
```

### 新增辅助方法

在 `animation.cc` 中添加：

```cpp
// 构造Flash资源名称
std::string AnimaDisplay::ConstructResourceName(const std::string& animation) {
    // 根据动画名称查找完整路径
    auto it = animation_maps_.find(animation);
    if (it != animation_maps_.end()) {
        // 从SD卡路径转换为Flash资源名
        // 例如: "/sdcard/emergency/motion_shake_violently/motion_shake_violently.gif"
        //  -> "emergency/motion_shake_violently/motion_shake_violently"

        std::string sd_path = it->second;

        // 移除 "/sdcard/" 前缀
        size_t prefix_len = strlen("/sdcard/");
        if (sd_path.substr(0, prefix_len) == "/sdcard/") {
            sd_path = sd_path.substr(prefix_len);
        }

        // 移除扩展名 ".gif"
        size_t dot_pos = sd_path.rfind('.');
        if (dot_pos != std::string::npos) {
            sd_path = sd_path.substr(0, dot_pos);
        }

        return sd_path;
    }

    // 未找到映射，直接使用动画名称
    return animation;
}

// 从内存加载GIF
void AnimaDisplay::LoadGifFromMemory(void* data, size_t size, int loop_count) {
    // 删除旧GIF对象
    if (animation_gif_ != nullptr) {
        lv_obj_delete(animation_gif_);
        animation_gif_ = nullptr;
    }

    // 创建新的GIF对象
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
            .cf = LV_COLOR_FORMAT_UNKNOWN,  // GIF会自动检测
            .flags = 0,
            .w = 0,  // 由GIF解码器填充
            .h = 0
        },
        .data = static_cast<const uint8_t*>(data),
        .data_size = static_cast<uint32_t>(size)
    };

    lv_gif_set_src(animation_gif_, &img_dsc);

    // 检查加载结果
    if (!lv_gif_is_loaded(animation_gif_)) {
        ESP_LOGE(TAG, "Failed to load GIF from memory");
        return;
    }

    // 设置循环次数
    lv_gif_set_loop_count(animation_gif_, loop_count);

    ESP_LOGD(TAG, "GIF loaded from Flash successfully");
}
```

### 更新头文件：animation.h

在 `main/boards/ALichuangTest/skills/animation.h` 中添加：

```cpp
private:
    std::string ConstructResourceName(const std::string& animation);
    void LoadGifFromMemory(void* data, size_t size, int loop_count);
```

---

## 2.3 修改Application支持Flash OGG播放

### 修改文件：main/application.cc

在 `PlaySoundOGGFile()` 方法中（约959行）：

```cpp
void Application::PlaySoundOGGFile(const std::string& audio_name, int volume) {
    // 音量范围限制
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    // ========== 新增：优先从Flash加载 ==========
    auto& board = Board::GetInstance();
    auto& assets = *Board::GetInstance().GetLynxAssets();

        if (assets.IsPartitionValid() && assets.IsChecksumValid()) {
            // 构造OGG资源名称
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

                return;  // 成功播放，直接返回
            } else {
                ESP_LOGW(TAG, "OGG resource not found in Flash: %s", resource_name.c_str());
            }
        }
    }

    // ========== 降级：SD卡方式（已移除） ==========
    ESP_LOGE(TAG, "Flash资源加载失败，且已移除SD卡支持");
}
```

### 新增辅助方法

在 `application.cc` 中添加（私有方法）：

```cpp
std::string Application::ConstructOggResourceName(const std::string& audio_name) {
    // 从音频映射表查找完整路径
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

### 更新头文件：application.h

在 `main/application.h` 中添加：

```cpp
private:
    std::string ConstructOggResourceName(const std::string& audio_name);
```

---

## 2.4 更新ALichuangTest板级初始化

### 修改文件：ALichuangTest.cc

```cpp
#include "lynx_assets.h"  // 新增

class ALichuangTest : public WifiBoard {
private:
    // ... 现有成员 ...

    // 新增：Flash资源管理器
    std::unique_ptr<lynxaura::LynxAssets> lynx_assets_;

public:
    ALichuangTest() : WifiBoard() {
        // ... 现有初始化代码 ...

        // ========== 移除：SD卡初始化 ==========
        // SDdata_Pro* sd_card = new SDdata_Pro();  // 删除此行

        // ========== 新增：初始化LynxAssets ==========
        lynx_assets_ = std::make_unique<lynxaura::LynxAssets>();
        if (!lynx_assets_->Initialize()) {
            ESP_LOGE(TAG, "Failed to initialize LynxAssets");
        } else {
            ESP_LOGI(TAG, "LynxAssets initialized: %lu files loaded",
                     lynx_assets_->GetFileCount());
        }

        // ... 其他初始化代码 ...
    }

    // 新增：暴露LynxAssets访问接口
    lynxaura::LynxAssets* GetLynxAssets() const {
        return lynx_assets_.get();
    }
};
```

### 移除SD卡相关头文件

删除或注释掉：

```cpp
// #include "sddata_pro.h"  // 不再需要
```

---

## 2.5 移除SD卡依赖代码

### 检查并移除SD卡初始化

使用以下命令查找所有SD卡引用：

```bash
cd main/boards/ALichuangTest
grep -r "SDdata" . --include="*.cc" --include="*.h"
grep -r "sdcard" . --include="*.cc" --include="*.h"
```

### 需要移除的代码位置

1. **ALichuangTest.cc**
   - [ ] 删除 `#include "sddata_pro.h"`
   - [ ] 删除 `SDdata_Pro* sd_card = new SDdata_Pro();`

2. **animation.cc**
   - [ ] 删除 `#define BOOT_GIF_PATH "/sdcard/..."`（改用Flash资源）
   - [ ] 删除 `#define DEFAULT_GIF_PATH "/sdcard/..."`
   - [ ] 修改 `animation_maps_` 表（仅用于路径转换，不直接访问）

3. **application.cc**
   - [ ] 删除 `audio_file_maps_` 中的SD卡路径（改为Flash资源名）

### 更新启动GIF加载

在 `animation.cc` 构造函数中：

```cpp
AnimaDisplay::AnimaDisplay(...) {
    // ... LVGL初始化 ...

    // 启动GIF从Flash加载
    SetAnima("system/system_boot_up");  // 不再使用硬编码路径
}
```

---

## 2.6 更新配置文件

### 修改config.json

**文件路径**：`main/boards/ALichuangTest/config.json`

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

**关键变更**：
- 分区表从 `v1/16m_anima.csv` → `v2/16m.csv`
- 构建名称更新为 `ALichuangTest-flash-v2`

---

## 编译测试

### 编译固件

```bash
# 清理旧构建
idf.py fullclean

# 选择目标板子
idf.py set-target esp32s3

# 配置
idf.py menuconfig

# 构建
idf.py build
```

### 预期编译结果

```
...
[100%] Linking CXX executable xiaozhi.elf
...
xiaozhi.bin binary size: 3.2MB
...
```

### 验证链接

确保以下符号正确链接：

```bash
# 检查LynxAssets类符号
xtensa-esp32s3-elf-nm build/xiaozhi.elf | grep LynxAssets

# 期望输出类似:
# 400xxxxx T lynxaura::LynxAssets::Initialize()
# 400xxxxx T lynxaura::LynxAssets::GetResource(...)
```

---

## 完成检查清单

- [ ] **LynxAssets类**
  - [ ] `lynx_assets.h` 创建完成
  - [ ] `lynx_assets.cc` 实现完成
  - [ ] 添加到CMakeLists.txt

- [ ] **AnimaDisplay修改**
  - [ ] `SetAnima()` 支持Flash加载
  - [ ] 新增 `ConstructResourceName()` 方法
  - [ ] 新增 `LoadGifFromMemory()` 方法

- [ ] **Application修改**
  - [ ] `PlaySoundOGGFile()` 支持Flash加载
  - [ ] 新增 `ConstructOggResourceName()` 方法

- [ ] **ALichuangTest初始化**
  - [ ] 添加 `lynx_assets_` 成员
  - [ ] 添加 `GetLynxAssets()` 方法
  - [ ] 移除SD卡初始化代码

- [ ] **SD卡依赖移除**
  - [ ] 删除 `sddata_pro.h` 引用
  - [ ] 更新路径常量

- [ ] **配置更新**
  - [ ] `config.json` 使用v2分区表

- [ ] **编译通过**
  - [ ] 无编译错误
  - [ ] 二进制大小 <4MB

**所有检查项通过后，继续 [步骤3：资源打包](03_ASSET_PACKING.md)**

---

**下一步**：[步骤3：资源打包与烧录](03_ASSET_PACKING.md)
