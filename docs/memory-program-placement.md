# Memory and Program Placement Plan

本文档规划后续“内存位置”和“程序位置”。目标是让 CoreS3 在无 SD 卡、无外网、无手机 App 的情况下仍能本地运行；SD 卡、手机网页、电脑工具和云端能力都只能作为可选扩展。

## 当前分层

| 层级 | 放置内容 | 当前规则 |
| --- | --- | --- |
| CoreS3 Flash / app partition | 主程序、公共类型、UDP 协议、HTTP API、内置 `/app` 页面、当前轻量识别模型、默认启动音频。 | 必须随固件编译烧录，不能只放 SD 卡。 |
| ESP32 Preferences / NVS | 背包 `BackpackStorage`、当前选择、成长后的宠物数据。 | 小容量关键存档，继续保持固件可直接读取。 |
| SRAM / heap | 当前屏幕状态、背包内存副本、战斗包、识别输入缓冲、HTTP JSON、App 日志、SD 音频临时缓冲。 | 只放运行时状态和短生命周期缓冲，避免长期大块分配。 |
| Camera / display internal buffers | 摄像头帧和显示绘制数据。 | 由 M5CoreS3/M5GFX 管理，业务代码只读取和绘制。 |
| SD card | 音频资源、截图、样本、日志、宠物快照、未来模型候选文件。 | 可选数据层；读取失败必须降级。 |
| Development PC | 编译脚本、训练脚本、官方固件归档、文档、数据集整理工具。 | 不属于单片机运行文件。 |
| Phone / computer App | 手机 UI、电脑调试工具、HTTP 客户端。 | 通过 Wi-Fi HTTP/USB 串口访问固件，不替代固件主逻辑。 |

## 必须留在固件内的位置

- `04_camera_pet_battle.ino`：主循环、UI、相机、Wi-Fi AP、UDP 对战、HTTP API、SD 检测。
- `pet_model.h`、`vision_types.h`、`ui_types.h`、`battle_protocol.h`：公共类型和协议边界。
- `vision_model_data.h`：当前本地识别模型。后续如果支持 SD 模型，也必须保留固件内默认模型作为回退。
- `trainer_intro_audio.h`：当前内置启动音频。后续 SD 音频只能覆盖或补充，不能成为启动前提。
- 固件内置 `/app` HTML/CSS/JS：当前手机网页直接由 WebServer `send_P()` 服务。后续可增加 SD 版本，但必须保留固件内最小控制页。

## 适合迁移或扩展到 SD 卡的位置

- `/audio/ui/`、`/audio/battle/`、`/audio/music/`：外置音频资源。当前已有路径和读取逻辑，受单个音频文件大小上限保护。
- `/captures/`：拍照截图或采集样本。
- `/samples/`：训练样本、人工标签、样本索引。
- `/logs/`：运行日志、HTTP App 操作日志、对战日志导出。
- `/pets/`：宠物快照、战绩导出、可分享记录。当前背包主存档仍在 Preferences。
- `/models/`：未来模型候选文件。只能作为可选加载源，不能替代固件内默认模型。
- `/app/`：未来可选的外置网页资源。必须先实现版本检查和缺失回退，再允许读取。

## 运行时内存规则

- 全局静态变量只保存常驻状态：背包副本、当前宠物、战斗状态、识别输入、App 小日志。
- HTTP 响应使用短生命周期 `String`，继续按接口控制 `reserve()` 大小，避免无界拼接。
- SD 音频读取只允许短生命周期堆缓冲；单文件上限由 `kExternalSoundMaxBytes` 控制，播放后立即释放。
- 不在 CoreS3 上训练模型，不把完整样本集加载进 RAM。
- 不把照片、日志、模型文件长期缓存到 RAM；需要导出时写 SD 或通过电脑/手机读取。
- 大对象必须有明确上限：背包容量、日志条数、音频大小、HTTP JSON 大小、样本批次大小。

## 后续迁移顺序

1. 保持当前固件内默认资源和 Preferences 存档不变。
2. 先完善 SD 资源目录检测和只读资源加载，例如音频文件。
3. 再增加日志、截图、样本导出；失败时只记录错误，不影响玩法。
4. 再评估 SD `/app/` 外置网页；必须保留固件内最小网页。
5. 最后评估 `/models/` 可选模型加载；必须保留 `vision_model_data.h` 回退模型，并提交接口变更建议。

## 禁止事项

- 不把公共头文件或 `BattlePetPacket` 定义放到 SD 卡运行时覆盖。
- 不让 SD 卡成为捕捉、背包、识别、对战或手机控制的硬依赖。
- 不把云端 API key、账号 token 或真实凭证写进固件、SD 卡样例或仓库。
- 不在 CoreS3 上训练模型；训练继续放在电脑端脚本。
- 不因为 SD 卡文件缺失而阻塞 UI 或 MATCH 对战。
