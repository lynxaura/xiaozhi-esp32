#ifndef ANIMATION_LOADER_H
#define ANIMATION_LOADER_H

#include <string>
#include <vector>
#include <map>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <dirent.h>
#include <sys/stat.h>

/**
 * @brief 动画加载器 - 负责从SD卡加载动画帧
 *
 * 提供高效的动画帧加载、预加载和缓存功能
 * 参考SDdata_Pro的实现，优化内存使用和加载性能
 */
class AnimationLoader {
public:
    /**
     * @brief 构造函数
     */
    AnimationLoader();

    /**
     * @brief 析构函数，自动清理资源
     */
    ~AnimationLoader();

    /**
     * @brief 初始化加载器
     * @return true 初始化成功, false 初始化失败
     */
    bool Initialize();

    // === 核心加载功能 ===

    /**
     * @brief 加载单帧动画（参考SDdata_Pro::ReadImageBin）
     * @param file_path 帧文件路径（如："/sdcard/emergency/motion_free_fall/animation/001.bin"）
     * @param buffer 输出缓冲区，必须预分配153600字节
     * @return true 加载成功, false 加载失败
     */
    bool LoadFrame(const std::string& file_path, uint8_t* buffer);

    /**
     * @brief 预加载动画序列到缓存（优化性能）
     * @param dir_path 动画目录路径
     * @param frame_count 帧数，0表示自动检测
     * @return true 预加载成功, false 预加载失败
     */
    bool PreloadAnimation(const std::string& dir_path, int frame_count = 0);

    /**
     * @brief 获取预加载的帧数据
     * @param frame_index 帧索引（从0开始）
     * @return 帧数据指针，失败返回nullptr
     */
    uint8_t* GetPreloadedFrame(int frame_index);

    /**
     * @brief 清理预加载缓存
     */
    void ClearCache();

    /**
     * @brief 清理特定动画的缓存
     * @param dir_path 动画目录路径
     */
    void ClearAnimationCache(const std::string& dir_path);

    // === 工具函数 ===

    /**
     * @brief 检查动画目录是否存在
     * @param dir_path 动画目录路径
     * @return true 存在, false 不存在
     */
    bool CheckAnimationExists(const std::string& dir_path);

    /**
     * @brief 获取动画帧数（扫描目录中的.bin文件）
     * @param dir_path 动画目录路径
     * @return 帧数，失败返回0
     */
    int GetFrameCount(const std::string& dir_path);

    /**
     * @brief 获取动画文件列表
     * @param dir_path 动画目录路径
     * @return 已排序的文件名列表
     */
    std::vector<std::string> GetFrameFiles(const std::string& dir_path);

    /**
     * @brief 检查单个帧文件是否存在
     * @param file_path 帧文件路径
     * @return true 存在, false 不存在
     */
    bool CheckFrameExists(const std::string& file_path);

    /**
     * @brief 获取文件大小
     * @param file_path 文件路径
     * @return 文件大小（字节），失败返回0
     */
    size_t GetFileSize(const std::string& file_path);

    // === 内存管理 ===

    /**
     * @brief 分配帧缓冲区（使用SPIRAM）
     * @return 帧缓冲区指针，失败返回nullptr
     */
    uint8_t* AllocateFrameBuffer();

    /**
     * @brief 释放帧缓冲区
     * @param buffer 缓冲区指针
     */
    void FreeFrameBuffer(uint8_t* buffer);

    /**
     * @brief 获取缓存使用情况
     * @return 缓存的帧数
     */
    size_t GetCacheSize() const { return frame_cache_.size(); }

    /**
     * @brief 获取缓存使用的内存大小
     * @return 内存大小（字节）
     */
    size_t GetCacheMemoryUsage() const { return frame_cache_.size() * FRAME_SIZE; }

    // === 常量定义 ===
    static constexpr size_t FRAME_SIZE = 153600;  // 320x240x2 (RGB565)
    static constexpr size_t MAX_CACHE_FRAMES = 32; // 最大缓存帧数
    static constexpr const char* FRAME_FILE_EXT = ".bin"; // 帧文件扩展名

private:
    // === 缓存管理 ===
    struct CachedAnimation {
        std::string dir_path;
        std::vector<uint8_t*> frames;
        int64_t last_used_time;  // 最后使用时间（用于LRU）

        CachedAnimation() : last_used_time(0) {}
        ~CachedAnimation() {
            for (auto* frame : frames) {
                if (frame) {
                    heap_caps_free(frame);
                }
            }
        }
    };

    // 动画缓存映射 (path -> CachedAnimation)
    std::map<std::string, CachedAnimation*> frame_cache_;

    // 当前预加载的动画路径
    std::string current_preload_path_;

    // 初始化标志
    bool initialized_ = false;

    // === 内部方法 ===

    /**
     * @brief 读取二进制文件（参考SDdata_Pro::ReadImageBin）
     * @param file_path 文件路径
     * @param buffer 输出缓冲区
     * @param expected_size 期望的文件大小
     * @return true 读取成功, false 读取失败
     */
    bool ReadBinaryFile(const std::string& file_path, uint8_t* buffer, size_t expected_size);

    /**
     * @brief 构建帧文件路径
     * @param dir_path 动画目录路径
     * @param frame_number 帧号（从1开始）
     * @return 完整的帧文件路径
     */
    std::string BuildFramePath(const std::string& dir_path, int frame_number);

    /**
     * @brief 验证帧文件格式
     * @param file_path 文件路径
     * @return true 格式正确, false 格式错误
     */
    bool ValidateFrameFile(const std::string& file_path);

    /**
     * @brief LRU缓存管理：清理最久未使用的缓存
     */
    void EvictLRUCache();

    /**
     * @brief 更新缓存使用时间
     * @param dir_path 动画目录路径
     */
    void UpdateCacheAccessTime(const std::string& dir_path);

    /**
     * @brief 获取当前时间戳
     * @return 时间戳（微秒）
     */
    int64_t GetCurrentTimeUs();

    /**
     * @brief 扫描目录获取所有.bin文件
     * @param dir_path 目录路径
     * @return 排序后的文件名列表
     */
    std::vector<std::string> ScanBinFiles(const std::string& dir_path);

    /**
     * @brief 检查路径是否为目录
     * @param path 路径
     * @return true 是目录, false 不是目录
     */
    bool IsDirectory(const std::string& path);

    /**
     * @brief 检查路径是否为文件
     * @param path 路径
     * @return true 是文件, false 不是文件
     */
    bool IsFile(const std::string& path);

    /**
     * @brief 规范化路径（移除末尾的斜杠等）
     * @param path 原始路径
     * @return 规范化后的路径
     */
    std::string NormalizePath(const std::string& path);
};

#endif // ANIMATION_LOADER_H