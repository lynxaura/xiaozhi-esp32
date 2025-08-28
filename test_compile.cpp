// 简单的编译测试文件
#include "main/boards/ALichuangTest/interaction/core/emotion_engine.h"
#include "main/boards/ALichuangTest/interaction/core/event_engine.h"

int main() {
    // 测试是否可以编译
    EmotionEngine& emotion_engine = EmotionEngine::GetInstance();
    EventEngine event_engine;
    return 0;
}