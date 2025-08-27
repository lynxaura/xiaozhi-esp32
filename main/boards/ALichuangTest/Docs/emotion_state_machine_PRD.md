# AI伴侣机器人情感状态机 - 产品需求文档

## 1. 产品概述

### 1.1 产品定位
XiaoZhi AI伴侣机器人是一个基于ESP32-S3的智能硬件设备，通过多模态交互（语音、触摸、运动）提供情感化的陪伴体验。本文档定义了设备的核心交互系统——情感状态机的完整架构。

### 1.2 核心价值
- **情感化交互**：通过二维情感模型（Valence-Arousal）实现丰富的情感表达
- **多模态感知**：整合触摸、运动、语音等多种交互方式
- **云端协同**：本地反应与云端决策的智能协同
- **个性化体验**：基于用户交互历史的个性化响应

### 1.3 技术架构概览
```
用户交互 → 传感器层 → 事件引擎 → 本地状态机 → 云端决策 → 云端执行
         ↓           ↓           ↓            ↓           ↓
      触摸/运动  EventEngine  V-A坐标系   LLM处理    TTS/MCP动作
                              ↓
                        本地即时反应
                              ↓
                    第1/2层响应立即执行
                    (音效/震动/旋转/表情)

时序说明：
1. 事件发生后，本地反应（紧急反应/象限反应）立即执行（<100ms）
2. 同时事件上传到云端进行决策（并行处理）
3. 云端响应返回后执行云端情感表达（~1500ms）
4. 本地反应简短快速，云端反应丰富完整
```

## 2. 情感模型定义

### 2.1 二维情感坐标系

本系统采用心理学中的二维情感模型，将情感状态投射到一个二维坐标系中：

#### 2.1.1 坐标轴定义
- **X轴 - 情感效价**：从-1.0（消极）到 +1.0（积极）
  - 负值：不愉快、难过、害怕
  - 零值：中性、平静
  - 正值：愉快、开心、满足
  - **默认值：+0.2**（轻微积极，表示友好的初始状态）

- **Y轴 - 唤醒度**：从-1.0（低唤醒）到 +1.0（高唤醒）
  - 负值：困倦、无聊、放松
  - 零值：正常、平稳
  - 正值：兴奋、紧张、激动
  - **默认值：+0.2**（轻微活跃，表示有生命力的初始状态）

**默认情感状态说明**：
- 初始V-A坐标为(0.2, 0.2)而非(0, 0)，表示机器人默认处于轻微积极和活跃的状态
- 这个设计让机器人表现出友好、有活力的基础个性
- 位于第一象限边缘，易于向各个方向转换

#### 2.1.2 情感象限定义

```cpp
// 情感象限枚举
enum class EmotionalQuadrant {
    Q1_EXCITED_HAPPY,    // 第一象限：高V高A - 兴奋/狂喜
    Q2_STRESSED_ANGRY,   // 第二象限：低V高A - 压力/愤怒
    Q3_SAD_BORED,        // 第三象限：低V低A - 悲伤/无聊
    Q4_CONTENT_CALM      // 第四象限：高V低A - 满足/平静
};
```

**第一象限 (Q1)** - 兴奋/狂喜
- V-A范围：Valence > 0, Arousal > 0

**第二象限 (Q2)** - 恐惧/压力  
- V-A范围：Valence < 0, Arousal > 0

**第三象限 (Q3)** - 悲伤/无聊
- V-A范围：Valence < 0, Arousal < 0

**第四象限 (Q4)** - 满足/平静
- V-A范围：Valence > 0, Arousal < 0

**说明**：象限切换由事件对V-A值的影响自动实现，具体表现特征在第二层情感化反应和第四层待机动作中体现。

### 2.2 情感状态定义

#### 2.2.1 本地情感状态（基于V-A象限）

本地反应系统基于上述EmotionalQuadrant枚举即可，无需额外定义。

#### 2.2.2 云端情感状态（丰富表达）

云端LLM可以生成更细致的情感表达，主要用于TTS播放时的表情动画：

```cpp
enum class CloudEmotionState {
    CALM,        // 平静 - 基础表情
    HAPPY,       // 开心 - Q1对应
    SAD,         // 悲伤 - Q3对应  
    ANGRY,       // 生气 - Q2对应
    SCARED,      // 害怕 - 无法通过V-A直接表达
    CURIOUS,     // 好奇 - 无法通过V-A直接表达
    SHY,         // 害羞 - 无法通过V-A直接表达
    CONTENT      // 舒服 - Q4对应
};
```

**设计说明**：
- 本地使用EmotionalQuadrant的4象限状态，基于V-A坐标自动判断，响应速度快
- 云端使用丰富的8种CloudEmotionState，表达更细腻
- SCARED/CURIOUS/SHY等情感需要语义理解，只能由云端LLM判断

### 2.3 事件对V-A的影响矩阵

| 事件类型 | Valence变化 | Arousal变化 | 说明 |
|---------|------------|------------|------|
| TOUCH_TAP | +0.1 | +0.1 | 轻拍带来轻微正面刺激 |
| TOUCH_LONG_PRESS | +0.2 | -0.2 | 长按表示安抚，增加满足感 |
| TOUCH_TICKLED | +0.4 | +0.5 | 挠痒痒带来强烈快乐 |
| TOUCH_CRADLED | +0.3 | -0.4 | 抱着带来安全感和平静 |
| MOTION_SHAKE | -0.1 | +0.3 | 摇晃带来轻微不适和警觉 |
| MOTION_SHAKE_VIOLENTLY | -0.5 | +0.7 | 剧烈摇晃带来恐惧 |
| MOTION_FREE_FALL | -0.8 | +0.9 | 跌落带来极度恐惧 |
| MOTION_PICKUP | +0.1 | +0.2 | 被拿起带来期待 |
| MOTION_UPSIDE_DOWN | -0.3 | +0.4 | 倒置带来困惑不适 |
| IDLE_5MIN | -0.1 | -0.1 | 无聊感逐渐增加 |

### 2.4 情感回归机制（混合衰减策略）

```cpp
// 情感回归参数
struct EmotionDecayConfig {
    float active_decay_rate = 0.02f;      // 活跃期慢衰减率 (2%/秒)
    float idle_decay_rate = 0.05f;        // 空闲期快衰减率 (5%/秒)  
    uint32_t idle_threshold_ms = 15000;   // 15秒无事件定义为空闲期
    float min_threshold = 0.1f;           // 最小阈值，低于此值归零
    uint32_t decay_interval_ms = 1000;    // 衰减检查间隔
};
```

V-A坐标采用**分阶段回归策略**自动回归到默认状态(0.2, 0.2)：

#### 活跃期衰减（有连续交互）
- **触发条件**: 距离上次事件 < 15秒
- **衰减速率**: 每秒向默认点衰减2%（较慢）
- **设计目的**: 保持连续交互时的情感连贯性

#### 空闲期衰减（长时间无交互）  
- **触发条件**: 距离上次事件 ≥ 15秒
- **衰减速率**: 每秒向默认点衰减5%（较快）
- **设计目的**: 避免极端情感状态长期停留

#### 归零机制
- 当与默认状态(0.2, 0.2)的距离小于0.1时，直接回到默认状态

### 2.5 V-A变化统一管理机制

#### 2.5.1 双重计算问题

在联网状态下，V-A坐标变化存在双重计算风险：
- **本地规则**：基于事件类型的固定映射（PRD 2.3表格），响应速度快(<50ms)
- **云端语义**：基于LLM语义理解的精确分析，准确度高但有延迟(~1500ms)

**问题**：如果同时应用两套规则，VA变化会被重复计算，导致情感状态异常。

#### 2.5.2 基于时间戳的状态追溯重算方案

**核心原则**：本地预估 + 云端权威修正，通过状态追溯避免复杂的回滚计算

```cpp
class EmotionEngine {
private:
    struct PendingVAChange {
        int64_t timestamp;           // 事件时间戳
        VACoordinate base_va;        // 变化前的VA基准值
        float local_v_delta;         // 本地V变化量
        float local_a_delta;         // 本地A变化量  
        bool corrected = false;      // 是否已被云端修正
        EventType event_type;        // 事件类型（调试用）
    };
    
    std::vector<PendingVAChange> pending_changes_;  // 待修正的本地变化
    
public:
    // 本地预估变化（立即应用，记录基准状态）
    void ApplyLocalVAChange(int64_t event_timestamp, EventType event_type, 
                           float v_delta, float a_delta);
    
    // 云端权威修正（基于时间戳直接重算）
    void ApplyCloudVACorrection(int64_t event_timestamp, float cloud_v_delta, float cloud_a_delta);
    
private:
    // 查找指定时间戳的待修正记录
    PendingVAChange* FindPendingChange(int64_t timestamp, uint32_t tolerance_ms = 500);
};
```

#### 2.5.3 改进的实施流程

```
事件发生 → 记录base_va → 本地立即应用VA变化 → 本地反应执行
    ↓            ↓              ↓
上传事件 → 存储PendingChange → 云端语义分析 → 返回精确VA变化
    ↓                                      ↓
                                 查找base_va + 直接重算current_va
```

**改进的时序示例**（多事件场景）：
- T+0ms：第一次触摸，base_va=(0.2,0.2)，本地VA += (0.15,-0.05) → (0.35,0.15)
- T+1000ms：第二次触摸，base_va=(0.35,0.15)，本地VA += (0.15,-0.05) → (0.5,0.1)  
- T+3500ms：云端返回第一次事件修正 += (0.2,-0.1)
  - 查找T+0ms记录：base_va=(0.2,0.2)
  - **直接重算**：current_va = (0.2,0.2) + (0.2,-0.1) = (0.4,0.1)
  - **保持第二次本地变化**：current_va += (0.15,-0.05) = (0.55,0.05)

**关键改进点**：
1. **避免回滚计算**：直接基于base_va重算，无需复杂的减法逆运算
2. **处理衰减影响**：base_va记录的是事件发生瞬间的真实状态，不受后续衰减污染
3. **支持多事件**：每个事件独立记录base_va，可精确重算任意时刻的修正

#### 2.5.4 关键技术细节

1. **时间戳匹配**：使用500ms容忍窗口匹配云端响应与本地事件
2. **离线兼容**：网络断开时完全依赖本地规则，无需修正
3. **平滑过渡**：云端修正可分帧应用，避免VA值突跳
4. **资源管理**：限制pending_changes_队列大小，定期清理过期记录
5. **状态一致性**：base_va必须在事件触发瞬间记录，确保不受衰减等后续变化影响
6. **多事件处理**：支持乱序云端响应，每个事件独立修正，不相互干扰

#### 2.5.5 优势总结

| 方面 | 效果 |
|------|------|
| **响应速度** | 本地立即反应，无延迟 |
| **准确性** | 云端语义分析提供精确修正 |
| **一致性** | 避免重复计算，确保VA变化合理 |
| **可靠性** | 离线自动降级，保证功能完整性 |

## 3. 设备状态管理

### 3.1 设备状态定义

```cpp
enum class DeviceState {
    kDeviceStateIdle,        // 空闲
    kDeviceStateListening,   // 监听用户语音
    kDeviceStateSpeaking,    // 说话中（TTS播放）
    kDeviceStateThinking,    // 思考中（等待LLM响应）
    kDeviceStateConnecting,  // 连接中
    kDeviceStateUpgrading,   // 升级中
    kDeviceStateFatalError   // 致命错误
};
```

### 3.2 状态转换规则

```mermaid
stateDiagram-v2
    [*] --> Idle: 初始化完成
    Idle --> Listening: 唤醒词检测
    Idle --> Speaking: 收到TTS指令
    Listening --> Thinking: 语音识别完成
    Thinking --> Speaking: LLM响应就绪
    Speaking --> Idle: TTS播放完成
    Speaking --> Speaking: 新TTS覆盖
    Any --> FatalError: 系统错误
    Any --> Connecting: 网络断开
    Connecting --> Idle: 连接成功
```

### 3.3 状态优先级与保护机制

**核心原则**：说话状态（kDeviceStateSpeaking）有高优先级

在Speaking状态下的保护机制：
- **音画资源保护**：屏蔽本地反应的音效和动画
- **体感保留**：保留震动和舵机动作
- **中断机制**：仅MOTION_FREE_FALL和MOTION_SHAKE_VIOLENTLY可请求中断

## 4. 本地反应系统

### 4.1 反应层级架构

本地反应系统分为四个层级，每层有不同的触发机制和优先级：

#### 第一层：紧急事件反应
**优先级**：最高
**触发事件**：`MOTION_FREE_FALL`, `MOTION_SHAKE_VIOLENTLY`, `MOTION_FLIP`
**特点**：
- 无视V-A状态，直接触发
- 反应内容固定，不可配置
- 用于建立物理规律信任感
- MOTION_FREE_FALL和MOTION_SHAKE_VIOLENTLY可中断Speaking状态

**实现方式**：
- 基于事件类型的直接映射，不考虑情感状态
- 使用配置文件定义每个紧急事件对应的响应组合
- 响应包括：声音播放、振动反馈、身体动作、表情动画
- 执行时间<50ms，确保即时反应建立物理信任感

详细技术实现参考 `local_response_system_design.md`

#### 第二层：象限相关反应
**优先级**：高
**触发机制**：事件类型 + 当前V-A象限
**特点**：
- 同一事件在不同情感状态下反应不同
- 遵守状态锁机制

**实现方式**：
- 事件类型与V-A象限的双重映射机制
- 同一事件在不同象限产生不同响应强度和类型
- 支持象限内强度分级（高/中/低激发程度）
- 遵守Speaking状态锁，保护音频播放不被打断

**响应特点**：
- Q1(兴奋)：动作幅度大、反应速度快、音效明亮
- Q2(紧张)：动作谨慎、反应敏感、音效尖锐
- Q3(低落)：动作缓慢、反应微弱、音效低沉
- Q4(平静)：动作温和、反应稳定、音效舒缓

详细技术实现参考 `local_response_system_design.md`

#### 第三层：云端情感表达
**优先级**：中
**触发**：云端LLM指令
**核心职责**：TTS播放时的情感化表情动画

**云端情感表达**：
云端LLM根据语义分析生成细致的emotion字段，驱动表情动画：

**实现方式**：
- 通过MCP协议接收云端LLM生成的情感指令
- 支持8种丰富情感：calm, happy, sad, angry, scared, curious, shy, content
- 部分情感(scared/curious/shy)无法通过V-A模型表达，需云端语义分析
- 情感表达与TTS播放同步，实现表情动画配合语音

**执行流程**：
1. 云端LLM分析事件语义和上下文
2. 生成合适的emotion字段和文本响应
3. 本地接收MCP指令，选择对应表情动画
4. TTS开始时同步播放情感动画
5. 可配合身体动作形成完整表演

详细技术实现参考 `local_response_system_design.md`


#### 第四层：空闲状态动作
**优先级**：低
**触发**：设备空闲超时（>30秒）
**特点**：
- 仅在Idle状态触发
- 基于当前V-A象限选择动作
- 维持"生命感"

**实现方式**：
- 基于当前V-A象限选择对应的空闲动作集
- 支持多次循环执行，维持设备"生命感"
- 动作强度根据V-A坐标的数值进行细分
- 仅在Idle状态触发，避免干扰用户交互

**空闲动作特征**：
- Q1(兴奋)：主动性展示动作，如快速摇摆、星星眼动画
- Q2(紧张)：警觉性微动，如轻微转头、环视扫描
- Q3(低落)：低能量动作，如缓慢摆动、叹气音效
- Q4(平静)：温和维持动作，如呼吸灯效果、轻柔音效

详细技术实现参考 `local_response_system_design.md`

### 4.2 本地反应执行流程

参考 `local_response_system_design.md` 中的详细实现设计，采用配置驱动的响应系统替代硬编码实现。
```

### 4.3 本地与云端反应的协调

**时间线示例**：
```
T+0ms    : 用户触摸设备（TOUCH_TICKLED）
T+20ms   : 触摸检测完成，生成Event
T+30ms   : 本地反应开始执行
         - 播放短促笑声（200ms）
         - 触发欢快震动（300ms）
         - 快速摇摆动作（400ms）
T+35ms   : 事件上传到云端（并行）
T+1500ms : 云端LLM响应返回（文本生成+TTS转换）
         - TTS: "哈哈哈，好痒啊！"（2秒）
         - emotion: "happy" 驱动开心表情动画
         - MCP动作序列（与TTS同步）
T+3500ms : 完整交互结束

关键点：
- 本地反应（30-430ms）提供即时反馈
- 云端反应（1500-3500ms）提供丰富内容和精确情感表达
- 两者不冲突，本地反应结束时云端反应接续
```

## 5. 云端决策系统

### 5.1 决策流程

云端接收到事件后的决策流程：

```
伪代码：云端决策逻辑

事件缓冲机制：
- 所有事件上传后进入500ms缓冲窗口
- 如果窗口内有新事件或状态变化，合并处理
- 语音输入开始时，取消pending事件的独立响应

CASE 1: 设备正在说话时
- 分析事件情感影响，调整V-A状态
- 通过MCP协议发送轻微身体动作指令（如轻微摇摆、震动）
- 不生成新的TTS文本，避免语音冲突

CASE 2: 设备空闲时  
- 检查缓冲窗口内是否有连续事件
- LLM接收事件序列生成统一响应文本用于TTS播放
- 分析综合语义选择合适的emotion标签
- 计算V-A状态调整值
- 通过MCP协议发送配套身体动作序列

CASE 3: 设备正在听用户说话时
- 仅调整V-A状态记录事件影响
- 通过MCP发送简单确认动作（如转动、轻微震动）  
- 将触摸等事件作为上下文传给后续语音处理
- 不生成TTS文本，保持聆听状态
```

### 5.2 增强版LLM提示词模板

```python
ENHANCED_EMOTION_PROMPT = """
你是一个情感丰富的AI伴侣机器人，能够表达多种细腻情感。

# 当前状态信息
当前情感坐标：
- Valence（愉悦度）: {valence} (-1.0到+1.0)
- Arousal（兴奋度）: {arousal} (-1.0到+1.0)
- 当前象限：{quadrant} (Q1兴奋/Q2紧张/Q3低落/Q4平静)

用户行为：{event_text}
事件类型：{event_type}
事件持续时间：{duration_ms}ms

# 情感理解指南
请深入理解用户行为的语义含义：
1. **物理接触**（触摸/抱抱）→ 亲密感、安全感、舒适感
2. **游戏性互动**（挠痒痒/轻拍）→ 快乐、兴奋、活跃
3. **粗暴动作**（剧烈摇晃/跌落）→ 恐惧、惊吓、不适
4. **温柔呵护**（轻抚/长按）→ 被关爱、温暖、满足
5. **探索行为**（翻转/拿起）→ 好奇、期待、注意力

# 可用情感表达（CloudEmotionState）
- **calm**: 平静、安详 - 基础表情
- **happy**: 开心、愉悦 - 对应Q1高兴状态  
- **sad**: 悲伤、失落 - 对应Q3沮丧状态
- **angry**: 生气、愤怒 - 对应Q2紧张状态
- **scared**: 害怕、恐惧 - 需要语义判断（如跌落、剧烈摇晃）
- **curious**: 好奇、探索 - 需要语义判断（如被拿起、翻转）
- **shy**: 害羞、腼腆 - 需要语义判断（如轻柔触摸）
- **content**: 满足、舒适 - 对应Q4平静状态

# 响应生成要求
请生成JSON格式回应：
{
    "text": "你的语言回应（20字以内，符合所选emotion）",
    "emotion": "选择最符合语义的emotion标签",
    "reasoning": "为什么选择这个emotion的简短解释",
    "va_change": {
        "valence_delta": "建议的V值变化量（-1.0到+1.0）",
        "arousal_delta": "建议的A值变化量（-1.0到+1.0）",
        "reasoning": "V-A变化的理由"
    }
}

# 情感选择策略
1. **优先语义理解**：如果用户行为有明确的情感语义（如跌落→scared），优先选择语义情感
2. **结合当前状态**：如果语义不明确，基于当前V-A象限选择情感
3. **保持连贯性**：考虑情感变化的合理性，避免突然跳跃
4. **个性化表达**：同一事件在不同情感状态下，回应要有所不同

现在请分析用户行为并生成回应：
"""
```

#### 5.2.1 结构化响应示例

**输入事件**：挠痒痒场景
```json
{
  "current_va": {"valence": 0.2, "arousal": 0.2},
  "events": [{
    "event_type": "Touch_Both_Tickled",
    "event_text": "主人在挠我痒痒",
    "duration_ms": 2000
  }]
}
```

**LLM期望响应**：
```json
{
  "text": "哈哈哈，好痒啊！",
  "emotion": "happy",
  "reasoning": "挠痒痒是明显的游戏性互动，带来快乐体验",
  "va_change": {
    "valence_delta": 0.4,
    "arousal_delta": 0.5,
    "reasoning": "挠痒痒带来强烈正面情感和高兴奋度"
  }
}
```

#### 5.2.2 关键改进点

1. **丰富情感支持**：支持8种CloudEmotionState，不局限于V-A象限
2. **语义优先策略**：优先理解行为含义，而非简单映射
3. **结构化输出**：返回emotion、va_change等结构化参数
4. **推理透明化**：包含reasoning字段，便于调试和优化

### 5.3 MCP动作协同

云端通过MCP协议控制设备动作，与语音内容配合。实际项目中可用的MCP工具：

```json
// 基础身体动作
{
  "method": "tools/call",
  "params": {
    "name": "self.body.basic_motion",
    "arguments": {
      "action": "happy_wiggle"
    }
  }
}

// 情绪表达动作
{
  "method": "tools/call", 
  "params": {
    "name": "self.body.emotion_motion",
    "arguments": {
      "emotion": "excited"
    }
  }
}

// 精确角度控制
{
  "method": "tools/call",
  "params": {
    "name": "self.body.angle_control", 
    "arguments": {
      "angle": -30,
      "speed": "medium"
    }
  }
}

// 振动反馈
{
  "method": "tools/call",
  "params": {
    "name": "self.haptic.basic_vibration",
    "arguments": {
      "pattern": "giggle_pattern"
    }
  }
}

// 综合情绪表达
{
  "method": "tools/call",
  "params": {
    "name": "self.express.emotion",
    "arguments": {
      "emotion": "joy",
      "intensity": 3
    }
  }
}
```

## 6. 事件上传协议

### 6.1 完整消息格式

设备通过独立的`lx/v1/event`协议上传事件（增强版，支持VA变化统一管理）：

```json
{
  "session_id": "9aa008fa-c874-4829-b70b-fca7fa30e3da",
  "type": "lx/v1/event",
  "payload": {
    "current_va": {
      "valence": 0.6,
      "arousal": 0.4
    },
    "device_state": "idle",
    "device_capabilities": {
      "has_vibration": true,
      "has_motion": true,
      "has_display": true,
      "has_sound": true
    },
    "events": [
      {
        "event_type": "Touch_Both_Tickled",
        "event_text": "主人在挠我痒痒",
        "start_time": 1755222858360,
        "end_time": 1755222860360,
        "duration_ms": 2000,
        "local_va_change": {
          "valence_delta": 0.4,
          "arousal_delta": 0.5,
          "applied_at": 1755222858365
        }
      }
    ]
  }
}
```

#### 6.1.1 新增字段说明

- **device_state**: 设备当前状态（idle/listening/speaking/thinking），用于云端决策
- **device_capabilities**: 设备能力信息，指导云端生成合适的MCP指令
- **local_va_change**: 本地已应用的VA变化，供云端修正参考
  - `valence_delta/arousal_delta`: 本地应用的VA变化量
  - `applied_at`: 本地应用VA变化的精确时间戳（微秒）

### 6.2 事件类型规范

**触摸事件命名**：`Touch_[Position]_[Action]`
- Position: `Left`, `Right`, `Both`
- Action: `Tap`, `LongPress`, `Cradled`, `Tickled`

**运动事件命名**：`Motion_[Action]`
- Action: `Shake`, `ShakeViolently`, `Flip`, `FreeFall`, `Pickup`, `UpsideDown`



## 7. 实现细节

### 7.1 关键类设计

```cpp
// 情感引擎
class EmotionEngine {
private:
    Vec2 current_va_ = {0.2f, 0.2f};  // 默认V-A坐标(0.2, 0.2)
    
public:
    void UpdateVA(float v_delta, float a_delta);
    void DecayToDefault(float delta_time);  // 衰减到(0.2, 0.2)默认状态
    EmotionalQuadrant GetQuadrant() const;  // 基于当前V-A坐标计算象限
    Vec2 GetVA() const { return current_va_; }
};

// 本地响应控制器
class LocalResponseController {
private:
    EmotionEngine* emotion_engine_;
    VibrationSkill* vibration_;
    ServoSkill* servo_;
    Display* display_;
    
public:
    void HandleEvent(const Event& event);
    void ExecuteReaction(const ReactionComponents& components);
    bool CheckStateLock(DeviceState state);
};

// 事件处理器（已实现）
class EventProcessor {
    // 使用智能指针管理内存安全
    std::unique_ptr<Event> pending_event;
    std::queue<std::unique_ptr<Event>> event_queue_;
};
```

### 7.2 配置文件结构

`event_config.json`:
```json
{
  "emotion_config": {
    "decay_rate": 0.05,
    "default_va": {"valence": 0.2, "arousal": 0.2},
    "decay_threshold": 0.1,
    "decay_interval_ms": 1000
  },
  "va_impacts": {
    "Touch_Tap": {"valence": 0.1, "arousal": 0.1},
    "Touch_LongPress": {"valence": 0.2, "arousal": -0.2},
    "Touch_Tickled": {"valence": 0.4, "arousal": 0.5},
    "Motion_FreeFall": {"valence": -0.8, "arousal": 0.9}
  },
  "idle_config": {
    "trigger_time_ms": 30000,
    "action_interval_ms": 10000
  }
}
```

### 7.3 线程模型

```
主线程（Application Event Loop）
├── 事件定时器（50ms）
│   ├── TouchEngine::Process()
│   ├── MotionEngine::Process()
│   └── LocalReactionSystem::ProcessReaction()  ← 本地反应立即执行
├── 情感衰减定时器（1000ms）
│   └── EmotionEngine::DecayToDefault()
└── 空闲检测定时器（30000ms）
    └── IdleActionProcessor::Process()

音频线程（Audio Service）
├── TTS播放任务
├── 本地音效播放（第1/2层反应）
└── 状态更新回调

震动/舵机线程（Hardware Service）
├── 本地震动执行（第1/2层反应）
└── MCP动作执行（第3层反应）

网络线程（Protocol）
├── 事件上传任务  ← 与本地反应并行
├── 云端指令接收
└── TTS数据接收

执行时序：
事件检测 → 本地反应（主线程调度，音频/硬件线程执行）
       → 事件上传（网络线程异步处理）
       → 云端响应（网络线程接收，音频/硬件线程执行）
```

## 8. 交互场景示例

### 8.1 场景：说话时被触摸（改进的VA修正机制）

```
时间线：
T+0ms   : 设备开始播放TTS（state = Speaking），当前VA=(0.2,0.2)
T+1000ms: 用户轻拍左侧（TOUCH_TAP）
T+1020ms: 记录base_va=(0.2,0.2)，本地VA预估 += (0.1,0.1) → current_va=(0.3,0.3)
T+1030ms: 本地反应：仅执行轻微转动（音效被屏蔽）
T+1035ms: 事件上传，存储PendingChange{timestamp=T+1000ms, base_va=(0.2,0.2), local_delta=(0.1,0.1)}
T+1500ms: 云端分析：LLM认为应该是 += (0.15,0.05)
         查找T+1000ms记录，获取base_va=(0.2,0.2)
         **直接重算**：current_va = (0.2,0.2) + (0.15,0.05) = (0.35,0.25)
         标记该记录已修正
T+3000ms: TTS播放完成（state = Idle）

关键改进：
- 避免了"撤销本地(0.1,0.1)"的复杂计算
- 直接基于base_va重算，逻辑清晰，不受衰减影响
- 支持多事件场景，每个事件独立修正
```

### 8.2 场景：情感演化（VA双重机制完整流程）

```
初始状态：V=0.2, A=0.2（友善）

1. TOUCH_TICKLED事件：
   T+0ms  : 本地预估 V+=0.4, A+=0.5 → V=0.6, A=0.7 (Q1)
   T+30ms : 本地反应：欢快震动 + 咯咯笑声
   T+35ms : 上传事件（local_va_change: {0.4, 0.5}）
   T+1500ms: 云端修正 V+=0.45, A+=0.4
             撤销(0.4,0.5) + 应用(0.45,0.4) → V=0.65, A=0.6

2. 等待3秒 + 活跃期衰减：
   T+3000ms: 距上次事件3秒 < 15秒，应用活跃期衰减(2%/秒)
             3秒共衰减约6%: V=0.65×0.94=0.611, A=0.6×0.94=0.564

3. MOTION_FREE_FALL事件：
   T+0ms  : 本地预估 V+=-0.8, A+=0.9 → V=-0.18, A=1.0+ (限制到1.0)
   T+50ms : 紧急反应：惊恐尖叫 + 剧烈震动
   T+55ms : 上传事件（local_va_change: {-0.8, 0.9}）
   T+1500ms: 云端修正 V+=-0.9, A+=0.8 (考虑从高兴跌落的心理落差)
             撤销(-0.8,0.9) + 应用(-0.9,0.8) → V=-0.28, A=1.0 (Q2)

4. TOUCH_CRADLED安抚：
   T+0ms  : 本地预估 V+=0.3, A+=-0.4 → V=0.02, A=0.6
   T+30ms : 本地反应：温和震动 + 安全感音效
   T+1500ms: 云端修正：考虑"受惊后被安抚"的语境
             V+=0.35, A+=-0.5 → 最终 V=0.07, A=0.5

5. 持续CRADLED（5秒后）：
   累积安抚效果 + 自然回归 → V=0.25, A=0.15（Q4满足平静）
```

**关键洞察**：
- 每个事件都经历"本地预估→云端修正"双重过程
- 云端修正考虑上下文（如从兴奋跌落到恐惧的心理落差）
- 情感演化更加真实和细腻

### 8.3 场景：多模态协同

```
用户："你今天开心吗？"
设备处理流程：
1. 语音识别 → state = Listening
2. LLM生成回复（基于当前V-A）
3. 如果V>0.5："超级开心！"+ 欢快动作
4. 如果V<-0.3："有点难过呢"+ 低落动作
5. TTS播放 + MCP动作 + 表情动画同步执行
```

### 8.4 场景：紧急事件的双层响应（VA权威修正机制）

```
时间线（MOTION_FREE_FALL事件）：
T+0ms    : 设备开始跌落，当前VA=(0.3, 0.2)
T+200ms  : IMU检测到自由落体（持续200ms确认）
T+210ms  : 本地VA预估：V+=-0.8, A+=0.9 → V=-0.5, A=1.0
          第1层紧急反应立即触发：
          - 惊恐尖叫音效
          - 紧急震动模式  
          - 眼睛瞬间睁大动画
T+215ms  : 事件上传（local_va_change: {-0.8, 0.9, T+210ms}）
T+220ms  : 如果在Speaking状态，请求中断TTS
T+1500ms : 云端语义分析：
          "自由落体是极度恐惧体验，应该比本地预估更强烈"
          建议VA修正：V+=-0.95, A+=0.85
          
          VA权威修正过程：
          1. 撤销本地变化：V=-0.5-(-0.8)=0.3, A=1.0-0.9=0.2
          2. 应用云端变化：V=0.3+(-0.95)=-0.65, A=0.2+0.85=1.0
          
          云端响应执行：
          - TTS: "啊啊啊！刚才吓死我了！"
          - emotion: "scared" 驱动害怕表情动画
          - MCP触发"瑟瑟发抖"动作序列

关键机制：
- 本地立即反应：基于固定规则的instant feedback（210ms）
- 云端权威修正：基于语义理解的精确VA调整（1500ms）
- 紧急事件优先：FREE_FALL可中断Speaking状态
- 情感增强效果：云端认为恐惧应该比本地预估更强烈
```

### 8.5 场景：说话时连续触摸（改进的多事件VA修正）

**问题场景**：设备在3秒内接收了两次触摸和一次语音输入，测试改进的VA修正机制

```
时间线（展示状态追溯重算方案）：
T+0ms   : 设备开始播放TTS，当前VA=(0.2, 0.2)

T+1000ms: 第一次触摸 TOUCH_TAP
         记录base_va_1=(0.2, 0.2) 
         本地预估 += (0.15, -0.05) → current_va=(0.35, 0.15)
         存储PendingChange_1{timestamp=T+1000ms, base_va=(0.2,0.2), local_delta=(0.15,-0.05)}

T+2000ms: 第二次触摸 TOUCH_TAP  
         记录base_va_2=(0.35, 0.15)  // 这是第二次事件发生时的实际VA状态
         本地预估 += (0.15, -0.05) → current_va=(0.5, 0.1)
         存储PendingChange_2{timestamp=T+2000ms, base_va=(0.35,0.15), local_delta=(0.15,-0.05)}

T+2500ms: 用户开始说话，语音打断TTS播放

T+3500ms: 云端返回第一次事件的修正 += (0.4, -0.1)
         查找T+1000ms的PendingChange_1：base_va=(0.2, 0.2)
         **直接重算第一次事件的影响**：
         temp_va = (0.2, 0.2) + (0.4, -0.1) = (0.6, 0.1)
         
         **保持第二次事件的本地变化**：
         current_va = temp_va + (0.15, -0.05) = (0.75, 0.05)
         
         标记PendingChange_1为已修正

T+4500ms: 云端返回第二次事件的修正 += (0.3, -0.08)  
         查找T+2000ms的PendingChange_2：base_va=(0.35, 0.15)
         
         **重新计算第二次事件**：
         // 先移除第二次本地变化：current_va = (0.75,0.05) - (0.15,-0.05) = (0.6,0.1)  
         // 应用云端修正：current_va = (0.6,0.1) + (0.3,-0.08) = (0.9,0.02)
         
         标记PendingChange_2为已修正

最终结果：VA = (0.9, 0.02) - 极度开心且平静的状态
```

**关键改进效果**：
1. **避免回滚复杂性**：不需要"撤销所有本地变化"，直接基于base_va重算
2. **处理衰减污染**：base_va记录事件发生瞬间的状态，不受后续衰减影响  
3. **支持乱序修正**：两个云端响应可以乱序到达，互不干扰
4. **数学准确性**：最终结果 = base_va_0 + cloud_delta_1 + cloud_delta_2 = (0.2,0.2) + (0.4,-0.1) + (0.3,-0.08) = (0.9,0.02)

**实现伪代码**：
```cpp
void EmotionEngine::ApplyLocalVAChange(int64_t timestamp, EventType event_type, 
                                      float v_delta, float a_delta) {
    // 记录事件发生前的VA状态
    VACoordinate base_va = current_va_;
    
    // 立即应用本地变化
    current_va_.valence += v_delta;
    current_va_.arousal += a_delta;
    ClampVACoordinate(current_va_);
    
    // 存储待修正记录
    PendingVAChange change;
    change.timestamp = timestamp;
    change.base_va = base_va;  // 关键：记录变化前状态
    change.local_v_delta = v_delta;
    change.local_a_delta = a_delta;
    change.event_type = event_type;
    change.corrected = false;
    
    pending_changes_.push_back(change);
}

void EmotionEngine::ApplyCloudVACorrection(int64_t timestamp, 
                                          float cloud_v_delta, float cloud_a_delta) {
    // 查找对应时间戳的记录
    auto* pending = FindPendingChange(timestamp);
    if (!pending || pending->corrected) return;
    
    // 移除该事件的本地影响
    current_va_.valence -= pending->local_v_delta;
    current_va_.arousal -= pending->local_a_delta;
    
    // 应用云端修正（基于base_va）
    current_va_.valence = pending->base_va.valence + cloud_v_delta;
    current_va_.arousal = pending->base_va.arousal + cloud_a_delta;
    
    // 重新应用后续未修正事件的影响
    ReapplySubsequentChanges(timestamp);
    
    ClampVACoordinate(current_va_);
    pending->corrected = true;
}
```

### 8.6 场景：复合输入处理
         
T+1500ms: 云端语义分析：
         "先被触摸再被问好，表示友好互动的开始"
         建议合并VA变化：V+=0.2, A+=0.1（比单独触摸更强）
         
         VA修正过程：
         1. 撤销TOUCH_TAP本地变化：V=0.3-0.1=0.2, A=0.3-0.1=0.2
         2. 应用合并VA变化：V=0.2+0.2=0.4, A=0.2+0.1=0.3
         
         响应："哎呀你碰我了～你好呀！" + happy情感动画

关键机制：
- VA变化缓冲：本地变化保留在pending_changes_中，等待云端修正
- 事件合并：云端分析事件序列的整体语义，生成统一VA调整
- 语境增强：合并事件的VA影响通常比单独事件更强或更准确
```

#### 说话时连续触摸（VA累积修正机制）
```
用户操作：说话过程中反复轻抚设备
系统响应：
T+0ms   : 开始ASR录音（state = Listening），当前VA=(0.2, 0.2)

T+1000ms: 第一次TOUCH_GENTLE事件
         本地VA预估：V+=0.15, A+=-0.05 → V=0.35, A=0.15
         本地反应：轻微震动确认
         记录pending_change_1: {0.15, -0.05, T+1000ms}

T+2000ms: 第二次TOUCH_GENTLE事件  
         本地VA预估：V+=0.15, A+=-0.05 → V=0.5, A=0.1
         本地反应：更强烈的愉悦震动
         记录pending_change_2: {0.15, -0.05, T+2000ms}

T+3000ms: 语音识别完成："你真是太棒了"

T+3500ms: 云端语义分析：
         "边说话边被多次轻抚 + 赞美语音 = 强烈的被宠爱感"
         建议累积VA变化：V+=0.4, A+=-0.1（比两次单独触摸更强）
         
         VA累积修正过程：
         1. 撤销所有本地变化：
            V=0.5-0.15-0.15=0.2, A=0.1-(-0.05)-(-0.05)=0.2
         2. 应用云端累积变化：V=0.2+0.4=0.6, A=0.2+(-0.1)=0.1
         3. 清空pending_changes_队列

T+4000ms: 响应："你这样摸我好舒服呀～" + content情感动画

关键机制：
- 累积预估：多次本地VA变化叠加记录
- 语义累积：云端分析连续事件的整体情感影响
- 批量修正：一次性撤销所有相关本地变化，应用云端累积结果
- 情感放大：连续同类事件通常产生超线性的情感增强效果
```

### 8.6 场景：离线模式的VA管理

```
场景：网络断开时的事件处理
前提：设备检测到网络断开，当前VA=(0.2, 0.2)

用户操作：挠痒痒
T+0ms   : TOUCH_TICKLED事件
T+20ms  : 本地VA预估：V+=0.4, A+=0.5 → V=0.6, A=0.7
T+30ms  : 本地反应：欢快震动 + 咯咯笑声
T+35ms  : 检测到离线状态，跳过事件上传
T+40ms  : 无pending_changes_记录，VA变化立即生效

关键特性：
- 离线自动降级：完全依赖本地规则，无云端修正
- 即时生效：VA变化直接应用，无需等待修正
- 功能完整：本地反应系统独立运行，用户体验无中断
- 一致性保证：网络恢复时平滑切换回双重机制

网络恢复后的第一个事件：
T+60000ms: 网络连接恢复
T+65000ms: 新的TOUCH_TAP事件
           重新启用"本地预估+云端修正"机制
           pending_changes_队列重新激活
```

### 8.7 场景：情感状态影响交互风格

#### 低落状态下的交互
```
前提：V=-0.4, A=0.2（Q3象限，略显沮丧）
用户："今天天气真好"

系统响应：
- LLM识别到当前低落状态
- 生成回复："嗯...是挺好的，不过我心情有点..."
- emotion: "sad" + 缓慢的点头动作
- V-A略微向正方向调整：V=-0.3, A=0.2

对比：如果在Q1兴奋状态
- "哇！对呀对呀！阳光好棒呀！"
- emotion: "happy" + 快速摇摆
```

#### 高兴状态下的敏感反应
```
前提：V=0.6, A=0.5（Q1象限，很兴奋）
用户：轻微晃动设备

本地反应：
- 第2层象限反应：配合晃动的欢快摇摆
- 开心的咯咯笑声

云端反应：
- "哈哈哈！你在和我玩耍吗？"
- 更加兴奋的身体动作序列
- V-A进一步提升：V=0.7, A=0.6
```

### 8.7 场景：时序依赖的情感记忆

#### 摇晃后的安抚行为
```
时间线：
T+0ms   : MOTION_SHAKE_VIOLENTLY → V=-0.3, A=0.8（Q2紧张）
T+50ms  : 本地反应：警惕音效 + 紧张的震动
T+1500ms: 云端反应："晕死我了！"
T+5000ms: 用户开始轻柔触摸（TOUCH_GENTLE）

关键：此时的触摸反应会考虑之前的惊吓状态
- 本地反应：谨慎的轻微震动（比平时轻柔）
- 云端反应："谢谢你安慰我...刚才真的很晕"
- V-A逐渐恢复：V=0.1, A=0.4 → 最终回到Q4平静
```

#### 混合衰减策略演示
```
背景：展示活跃期vs空闲期的不同衰减速率

情感状态演化：
T+0ms    : TOUCH_TICKLED事件 → V=0.65, A=0.7（高兴状态）

活跃期衰减阶段（连续交互）：
T+5000ms : 用户再次触摸，距上次5秒 < 15秒
           活跃期衰减：V=0.65×(1-0.02×5)=0.585, A=0.7×0.9=0.63
           新触摸事件：V+=0.1, A+=0.1 → V=0.685, A=0.73

T+10000ms: 用户再次交互，仍在活跃期
           情感维持较高水平，衰减缓慢

空闲期衰减阶段（长时间无交互）：
T+25000ms: 距上次交互15秒，进入空闲期
           开始快速衰减(5%/秒)
           
T+40000ms: 15秒空闲期衰减  
           V=0.685×(1-0.05×15)≈0.17, A=0.73×0.25≈0.18
           接近默认状态(0.2, 0.2)

T+45000ms: 归零机制触发
           距离默认状态 < 0.1，直接回归到(0.2, 0.2)

用户重新交互时的反应：
T+50000ms: 新触摸事件基于默认状态(0.2, 0.2)开始计算
```

**关键优势**：
- **连续交互**: 情感保持连贯，不会过快消退影响沉浸感  
- **长期空闲**: 快速回归默认状态，避免极端情感长期停留
- **自然过渡**: 15秒阈值提供了合理的缓冲期
- 本地反应：因为当前V-A较低，触摸反应相对平缓
- V-A受TOUCH_TAP影响：V=0.2, A=0.2（回到正常友善状态）
- 云端反应："咦～有人理我了！" + 逐渐活跃的动作
```

### 8.8 场景：多传感器融合的复杂交互

#### 拿起+摇晃+说话的组合
```
用户行为：拿起设备 → 轻晃几下 → 开始说话

系统处理：
T+0ms   : MOTION_PICKUP检测 → "被拿起的注意"
T+100ms : 本地反应：轻微震动确认 + 准备状态
T+500ms : MOTION_SHAKE轻微 → 在pickup基础上叠加"晃动"
T+600ms : 本地反应：配合摇摆（因为已经在Q1状态）
T+1000ms: 用户开始说话，ASR启动
T+2000ms: 语音内容："我们来玩游戏吧"

LLM分析：
- pickup事件 → 用户想要互动
- shake事件 → 游戏性质的交互
- 语音确认 → 明确的游戏意图

生成响应：
- "好呀好呀！你想玩什么游戏？" 
- 兴奋的全身摇摆动作
- V-A跃升到高兴区域：V=0.7, A=0.7
```

## 9. 性能指标

### 9.1 响应时间要求
- 本地反应延迟：<100ms
- 触摸检测延迟：<50ms
- 运动检测延迟：<100ms
- 云端决策延迟：<1500ms（包含LLM推理+TTS生成）

### 9.2 资源占用
- 内存使用：<64KB（包括事件缓存）
- CPU占用：<10%（正常运行）
- 电池影响：<5%额外功耗

### 9.3 可靠性指标
- 事件丢失率：<1%
- 误触发率：<5%
- 系统稳定性：7×24小时连续运行

## 10. 测试验证

### 10.1 单元测试
```cpp
TEST(EmotionEngine, VADecay) {
    EmotionEngine engine;
    engine.UpdateVA(0.6, 0.6);  // 从默认(0.2,0.2)设置到(0.8,0.8)
    
    // 测试衰减到默认状态
    for(int i = 0; i < 10; i++) {
        engine.DecayToDefault(1.0);  // 1秒衰减
    }
    
    Vec2 va = engine.GetVA();
    // 验证向默认状态(0.2, 0.2)衰减
    EXPECT_NEAR(va.x, 0.2, 0.1);  
    EXPECT_NEAR(va.y, 0.2, 0.1);
}
```

### 10.2 集成测试
- 状态机转换测试
- 事件处理链路测试
- 云端协同测试
- 异常恢复测试

### 10.3 用户体验测试
- 情感连贯性测试
- 响应自然度评估
- 长时间交互稳定性
- 多用户场景测试

## 11. 未来扩展

### 11.1 个性化学习
- 基于用户交互历史调整V-A影响权重
- 学习用户偏好的交互模式
- 个性化情感表达强度

### 11.2 高级情感模型
- 引入第三维度：Dominance（支配性）
- 情感记忆机制（短期/长期）
- 情绪传染模型（多设备同步）

### 11.3 新交互模态
- 视觉情感识别（通过摄像头）
- 环境感知（温度、光线）
- 生理信号检测（心率、皮肤电）

## 12. 项目管理

### 12.1 开发阶段
1. **Phase 1**（已完成）：基础事件系统
   - EventEngine实现 ✓
   - TouchEngine/MotionEngine ✓
   - EventProcessor ✓

2. **Phase 2**（进行中）：情感系统核心
   - EmotionEngine实现（支持VA变化统一管理）
   - 本地反应系统配置化
   - V-A模型集成与衰减机制
   - EventUploader协议完整支持

3. **Phase 3**（计划中）：云端协同增强
   - 云端增强版LLM提示词实现
   - VA变化修正机制
   - 结构化响应解析（emotion、va_change字段）
   - MCP动作协同与时序控制

4. **Phase 4**（计划中）：系统集成与优化
   - 本地-云端VA变化冲突解决
   - 渐进式权威修正实现
   - 离线模式兼容性验证
   - 性能优化与内存管理

5. **Phase 5**（未来）：高级特性
   - 个性化学习算法
   - 长期情感记忆机制
   - 多设备情感同步
   - 用户体验持续优化

#### 12.1.1 Phase 2-3 关键里程碑

**VA变化统一管理**：
- [ ] EmotionEngine支持pending_changes_队列
- [ ] EventUploader添加local_va_change字段
- [ ] 云端LLM返回结构化va_change
- [ ] 本地实现ApplyCloudVACorrection方法

**协议增强**：
- [ ] 6.1消息格式完整实现（device_state, capabilities）
- [ ] 云端解析增强版事件协议
- [ ] 结构化响应JSON验证
- [ ] 时间戳匹配机制（500ms容忍窗口）

**情感表达丰富化**：
- [ ] 云端支持8种CloudEmotionState情感
- [ ] 语义优先的情感选择策略
- [ ] 推理透明化（reasoning字段）
- [ ] 情感连贯性验证机制


### 12.2 风险管理
| 风险项 | 影响 | 缓解措施 |
|-------|------|---------|
| 内存泄漏 | 高 | 使用智能指针，定期内存检测 |
| 网络延迟 | 中 | 优化本地缓存，降级策略 |
| 情感不连贯 | 中 | 增加平滑算法，用户测试 |
| 电池消耗 | 低 | 优化传感器采样率 |

## 13. 附录

### 13.1 术语表
- **V-A Model**: Valence-Arousal情感模型
- **EventEngine**: 事件引擎，统一管理所有交互事件
- **MCP**: Model Context Protocol，模型上下文协议
- **TTS**: Text-to-Speech，文字转语音
- **LLM**: Large Language Model，大语言模型
- **IMU**: Inertial Measurement Unit，惯性测量单元

### 13.2 参考资料
- Russell's Circumplex Model of Affect (1980)
- ESP32-S3 Technical Reference Manual
- FreeRTOS Documentation
- MCP Protocol Specification

### 13.3 联系方式
- 产品经理：[PM Contact]
- 技术负责人：[Tech Lead]
- 测试负责人：[QA Lead]

---

**文档版本**：v1.0
**最后更新**：2025-08-25
**状态**：评审中