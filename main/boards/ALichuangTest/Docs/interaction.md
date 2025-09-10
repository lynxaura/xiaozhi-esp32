这是一个AI玩具的状态机，针对不同的事件，会有不同的反应。
反应包含动画、音效、振动、动作四种类型。
每一个动画包含了很多帧文件，所以每个动画都是一个文件夹，存放于sd卡中。
音频是p3文件。

1. 紧急事件反应
对应PRD的4.1第一层：
Idle状态：中断idle状态的动画和声音，完整播放所有反应，同步上报服务器
Listening状态：中断Listening状态的动画和声音，完整播放所有反应，同步上报服务器
Speaking状态：中断speaking状态的动画和声音，完整播放所有反应，同步上报服务器
触发条件：识别事件后，中断当前状态的动画和声音，直接呈现本地动作，同步上报服务器，LLM也会给反应

事件
motion_free_fall
motion_shake_violently
motion_flip
motion_upside_down

2. 事件-象限匹配反应
VA象限指的是心理学中的二维情感模型，将情感状态投射到一个二维坐标系中，
- X轴 - 情感效价Valence：从-1.0（消极）到 +1.0（积极）
- 负值：不愉快、难过、害怕
- 零值：中性、平静
- 正值：愉快、开心、满足
  - 默认值：+0.2（轻微积极，表示友好的初始状态）

- Y轴 - 唤醒度 Arousal：从-1.0（低唤醒）到 +1.0（高唤醒）
- 负值：困倦、无聊、放松
- 零值：正常、平稳
- 正值：兴奋、紧张、激动
  - 默认值：+0.2（轻微活跃，表示有生命力的初始状态）

对应PRD的4.1第二层：根据事件和所处VA象限决定，会播放1次或多次，由配置文件决定。
Idle状态时：完整播放所有反应：音效、动画、振动、动作。
Listening状态：完整播放所有反应：音效、动画、振动、动作。
Speaking状态时：播放动画、振动、动作，不播放音效。

事件	情感条件
motion_shake_q1	Q1
motion_shake_q2	Q2
motion_shake_q3	Q3
motion_shake_q4	Q4
motion_pickup_q1	Q1
motion_pickup_q2	Q2
motion_pickup_q3	Q3
motion_pickup_q4	Q4
touch_tap_q1	Q1 (兴奋)
touch_tap_q2	Q2 (恐惧/压力)
touch_tap_q3	Q3 (悲伤/无聊)
touch_tap_q4	Q4 (满足/平静)
touch_long_press_q1	Q1
touch_long_press_q2	Q2
touch_long_press_q3	Q3
touch_long_press_q4	Q4
touch_cradled_q1	Q1
touch_cradled_q2	Q2
touch_cradled_q3	Q3
touch_cradled_q4	Q4
touch_tickled_q1	Q1
touch_tickled_q2	Q2
touch_tickled_q3	Q3
touch_tickled_q4	Q4

3. Speaking状态下各情感匹配反应
对应PRD4.1第三层，在后端服务器向玩具段推送音频流时，会同步将这段音频播放时应该呈现的情感状态发送到音频端（通过MCP）。
规则：
Speaking状态触发，循环播放
Speaking状态下，如果发生第二层事件（事件-象限匹配行为中所列事项），播放对应的动画，不打断语音；
Speaking状态，如发生第一层事件（紧急事件），打断当前Speaking状态动画及声音，按照紧急事件反应处理。

talk_calm  
talk_happy 
talk_sad
talk_angry 
talk_scared 
talk_curious 
talk_shy 
talk_content

4. VA象限状态待机反应
IDLE状态触发，根据所处VA象限有不同的反应。循环播放。
对应PRD4.1第四层

idle_q1
idle_q2
idle_q3
idle_q4

5. 聆听状态反应
Listening状态触发，根据所处VA象限有不同的反应。循环播放。
基于系统状态循环，如果在Listening状态，就一直播放。如果在listening状态下有事件发生，事件-象限匹配动画及声音播放，播放完后回到聆听表情。

listening_q1
listening_q2
listening_q3
listening_q4

6. 系统/功能动画 (System/Utility Animations)
 向用户传达非情感类的、功能性的系统状态信息。主要为动画、声音和振动
 事件名称 	触发逻辑
system_boot_up	设备开机
system_shut_down	设备关机或电量耗尽
system_charging	连接充电器
system_low_battery	电量低于10%
system_connecting	正在连接Wi-Fi或蓝牙
system_error	发生软件错误或连接失败
photo_taken	照片拍摄
DETACHED	身体连接断开
ATTACHED	身体连接成功