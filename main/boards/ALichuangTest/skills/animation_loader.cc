#include "animation_loader.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstring>

#define TAG "AnimationLoader"

AnimationLoader::AnimationLoader() : initialized_(false) {
}

AnimationLoader::~AnimationLoader() {
    ClearCache();
}

bool AnimationLoader::Initialize() {
    if (initialized_) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing AnimationLoader");

    // 清理缓存
    ClearCache();

    initialized_ = true;
    ESP_LOGI(TAG, "AnimationLoader initialized successfully");
    return true;
}

bool AnimationLoader::LoadFrame(const std::string& file_path, uint8_t* buffer) {
    if (!buffer) {
        ESP_LOGE(TAG, "Buffer is null");
        return false;
    }

    if (!initialized_) {
        ESP_LOGW(TAG, "Loader not initialized");
        return false;
    }

    // 检查文件是否存在
    if (!CheckFrameExists(file_path)) {
        ESP_LOGD(TAG, "Frame file not found: %s", file_path.c_str());
        return false;
    }

    // 读取文件（参考SDdata_Pro::ReadImageBin实现）
    return ReadBinaryFile(file_path, buffer, FRAME_SIZE);
}

bool AnimationLoader::ReadBinaryFile(const std::string& file_path, uint8_t* buffer, size_t expected_size) {
    FILE* f = fopen(file_path.c_str(), "rb");
    if (!f) {
        ESP_LOGD(TAG, "Failed to open file: %s", file_path.c_str());
        return false;
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size != expected_size) {
        ESP_LOGW(TAG, "File size mismatch: %s (expected %u, got %ld)",
                file_path.c_str(), expected_size, file_size);
        fclose(f);
        return false;
    }

    // 读取数据
    size_t read_size = fread(buffer, 1, expected_size, f);
    fclose(f);

    if (read_size != expected_size) {
        ESP_LOGE(TAG, "Failed to read complete file: %s (read %u of %u bytes)",
                file_path.c_str(), read_size, expected_size);
        return false;
    }

    return true;
}

bool AnimationLoader::PreloadAnimation(const std::string& dir_path, int frame_count) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Loader not initialized");
        return false;
    }

    std::string normalized_path = NormalizePath(dir_path);

    // 检查是否已经缓存
    auto it = frame_cache_.find(normalized_path);
    if (it != frame_cache_.end()) {
        UpdateCacheAccessTime(normalized_path);
        ESP_LOGD(TAG, "Animation already preloaded: %s", normalized_path.c_str());
        return true;
    }

    // 检查目录是否存在
    if (!CheckAnimationExists(normalized_path)) {
        ESP_LOGW(TAG, "Animation directory not found: %s", normalized_path.c_str());
        return false;
    }

    // 如果frame_count为0，自动检测
    if (frame_count == 0) {
        frame_count = GetFrameCount(normalized_path);
        if (frame_count <= 0) {
            ESP_LOGW(TAG, "No frames found in: %s", normalized_path.c_str());
            return false;
        }
    }

    ESP_LOGI(TAG, "Preloading animation: %s (%d frames)", normalized_path.c_str(), frame_count);

    // 检查缓存空间
    if (frame_cache_.size() >= MAX_CACHE_FRAMES / 4) { // 预留空间给其他动画
        EvictLRUCache();
    }

    // 创建缓存项
    CachedAnimation* cached = new CachedAnimation();
    cached->dir_path = normalized_path;
    cached->frames.reserve(frame_count);

    // 加载所有帧
    int loaded_frames = 0;
    for (int frame = 1; frame <= frame_count; frame++) {
        std::string frame_path = BuildFramePath(normalized_path, frame);

        // 分配帧缓冲区
        uint8_t* frame_buffer = AllocateFrameBuffer();
        if (!frame_buffer) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer for frame %d", frame);
            break;
        }

        // 加载帧
        if (LoadFrame(frame_path, frame_buffer)) {
            cached->frames.push_back(frame_buffer);
            loaded_frames++;
        } else {
            FreeFrameBuffer(frame_buffer);
            ESP_LOGW(TAG, "Failed to load frame %d from %s", frame, frame_path.c_str());
        }
    }

    if (loaded_frames == 0) {
        delete cached;
        ESP_LOGE(TAG, "Failed to load any frames from: %s", normalized_path.c_str());
        return false;
    }

    // 更新访问时间并缓存
    cached->last_used_time = GetCurrentTimeUs();
    frame_cache_[normalized_path] = cached;

    ESP_LOGI(TAG, "Preloaded %d/%d frames for: %s", loaded_frames, frame_count, normalized_path.c_str());
    return true;
}

uint8_t* AnimationLoader::GetPreloadedFrame(int frame_index) {
    if (current_preload_path_.empty()) {
        ESP_LOGW(TAG, "No animation currently preloaded");
        return nullptr;
    }

    auto it = frame_cache_.find(current_preload_path_);
    if (it == frame_cache_.end()) {
        ESP_LOGW(TAG, "Preloaded animation not found in cache: %s", current_preload_path_.c_str());
        return nullptr;
    }

    CachedAnimation* cached = it->second;
    if (frame_index < 0 || frame_index >= cached->frames.size()) {
        ESP_LOGW(TAG, "Frame index out of range: %d (max: %d)", frame_index, cached->frames.size() - 1);
        return nullptr;
    }

    // 更新访问时间
    UpdateCacheAccessTime(current_preload_path_);

    return cached->frames[frame_index];
}

void AnimationLoader::ClearCache() {
    ESP_LOGI(TAG, "Clearing animation cache (%u items)", frame_cache_.size());

    for (auto& pair : frame_cache_) {
        delete pair.second;
    }

    frame_cache_.clear();
    current_preload_path_.clear();

    ESP_LOGI(TAG, "Animation cache cleared");
}

void AnimationLoader::ClearAnimationCache(const std::string& dir_path) {
    std::string normalized_path = NormalizePath(dir_path);

    auto it = frame_cache_.find(normalized_path);
    if (it != frame_cache_.end()) {
        delete it->second;
        frame_cache_.erase(it);
        ESP_LOGI(TAG, "Cleared cache for: %s", normalized_path.c_str());

        if (current_preload_path_ == normalized_path) {
            current_preload_path_.clear();
        }
    }
}

bool AnimationLoader::CheckAnimationExists(const std::string& dir_path) {
    return IsDirectory(dir_path);
}

int AnimationLoader::GetFrameCount(const std::string& dir_path) {
    std::vector<std::string> files = ScanBinFiles(dir_path);
    return files.size();
}

std::vector<std::string> AnimationLoader::GetFrameFiles(const std::string& dir_path) {
    return ScanBinFiles(dir_path);
}

bool AnimationLoader::CheckFrameExists(const std::string& file_path) {
    return IsFile(file_path);
}

size_t AnimationLoader::GetFileSize(const std::string& file_path) {
    struct stat st;
    if (stat(file_path.c_str(), &st) == 0) {
        return st.st_size;
    }
    return 0;
}

uint8_t* AnimationLoader::AllocateFrameBuffer() {
    return (uint8_t*)heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
}

void AnimationLoader::FreeFrameBuffer(uint8_t* buffer) {
    if (buffer) {
        heap_caps_free(buffer);
    }
}

std::string AnimationLoader::BuildFramePath(const std::string& dir_path, int frame_number) {
    char frame_path[256];
    snprintf(frame_path, sizeof(frame_path), "%s/%03d%s",
            dir_path.c_str(), frame_number, FRAME_FILE_EXT);
    return std::string(frame_path);
}

bool AnimationLoader::ValidateFrameFile(const std::string& file_path) {
    // 检查文件扩展名
    if (file_path.find(FRAME_FILE_EXT) == std::string::npos) {
        return false;
    }

    // 检查文件大小
    size_t file_size = GetFileSize(file_path);
    if (file_size != FRAME_SIZE) {
        ESP_LOGW(TAG, "Invalid frame file size: %s (%u bytes, expected %u)",
                file_path.c_str(), file_size, FRAME_SIZE);
        return false;
    }

    return true;
}

void AnimationLoader::EvictLRUCache() {
    if (frame_cache_.empty()) {
        return;
    }

    // 找到最久未使用的缓存项
    auto lru_it = frame_cache_.begin();
    int64_t oldest_time = lru_it->second->last_used_time;

    for (auto it = frame_cache_.begin(); it != frame_cache_.end(); ++it) {
        if (it->second->last_used_time < oldest_time) {
            oldest_time = it->second->last_used_time;
            lru_it = it;
        }
    }

    ESP_LOGI(TAG, "Evicting LRU cache: %s", lru_it->first.c_str());

    // 删除最久未使用的项
    delete lru_it->second;
    frame_cache_.erase(lru_it);
}

void AnimationLoader::UpdateCacheAccessTime(const std::string& dir_path) {
    auto it = frame_cache_.find(dir_path);
    if (it != frame_cache_.end()) {
        it->second->last_used_time = GetCurrentTimeUs();
    }
}

int64_t AnimationLoader::GetCurrentTimeUs() {
    return esp_timer_get_time();
}

std::vector<std::string> AnimationLoader::ScanBinFiles(const std::string& dir_path) {
    std::vector<std::string> files;

    DIR* dir = opendir(dir_path.c_str());
    if (!dir) {
        ESP_LOGD(TAG, "Failed to open directory: %s", dir_path.c_str());
        return files;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename(entry->d_name);

        // 跳过目录项
        if (filename == "." || filename == "..") {
            continue;
        }

        // 检查扩展名
        if (filename.find(FRAME_FILE_EXT) != std::string::npos) {
            files.push_back(filename);
        }
    }

    closedir(dir);

    // 按文件名排序
    std::sort(files.begin(), files.end());

    ESP_LOGD(TAG, "Found %u .bin files in %s", files.size(), dir_path.c_str());
    return files;
}

bool AnimationLoader::IsDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

bool AnimationLoader::IsFile(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISREG(st.st_mode);
    }
    return false;
}

std::string AnimationLoader::NormalizePath(const std::string& path) {
    std::string normalized = path;

    // 移除末尾的斜杠（除非是根目录）
    while (normalized.length() > 1 && normalized.back() == '/') {
        normalized.pop_back();
    }

    // 替换多个连续斜杠为单个
    size_t pos = 0;
    while ((pos = normalized.find("//", pos)) != std::string::npos) {
        normalized.replace(pos, 2, "/");
    }

    return normalized;
}