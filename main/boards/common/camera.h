#ifndef CAMERA_H
#define CAMERA_H

#include <string>

class Camera {
public:
    virtual ~Camera() = default;  // 添加虚析构函数
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual std::string Explain(const std::string& question) = 0;

    // 添加内存管理方法
    virtual bool Deinitialize() = 0;  // 释放摄像头内存
    virtual bool Reinitialize() = 0;  // 重新初始化摄像头
};

#endif // CAMERA_H
