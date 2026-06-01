# Module Tasks

本文档记录后续模块协作任务。所有模块必须遵守公共约束：不重新定义公共类型，不私自修改公共头文件字段、枚举值或 UDP 包结构，不恢复语音识别，不引入新依赖，不写死云端 API key，并保持 CoreS3 本地离线可运行。

## 公共类型边界

已经拆分的公共类型：

- `pet_model.h`：`ElementType`、`PetGenes`、`SavedPet`、`BackpackStorage`、`kMaxBackpackPets`、`kBagMagic`、`kBagVersion`
- `vision_types.h`：`ObjectClass`、`ImageTraits`、`RecognitionResult`、`SubjectPresence`、`PetHint`
- `ui_types.h`：`ScreenMode`、`UiAction`、`ButtonSlot`
- `battle_protocol.h`：`BattleLinkRole`、`BattlePetPacket`、`kBattleMagic`、`kBattleVersion`

禁止事项：

- 不在 `.ino` 或模块文件中重新定义上述类型。
- 不私自修改公共头文件字段、枚举值或 UDP 包结构。
- 不修改 `BattlePetPacket`。
- 如果必须改公共接口，先输出接口变更建议，由架构模块合并。

## 产品化与文档模块

职责：

- 维护 README 和 docs。
- 规划 App 客户端。
- 规划云端大模型识别。
- 规划 SD 卡存储。
- 规划数据集和模型训练。
- 维护协议与接口文档。
- 维护 GitHub 版本说明。

当前交付：

- App 信息架构。
- 页面清单。
- 本地 MVP 功能列表。
- 未来云端和社区规划。
- 文档更新建议。
- 固件配合接口建议。

验收标准：

- 文档明确当前无语音识别。
- 文档明确当前音效可静音。
- 文档明确当前本地识别是轻量限定类别识别，不是通用视觉大模型。
- 文档明确当前对战使用 Wi-Fi AP + UDP。
- 文档明确当前背包使用 `Preferences`。
- 文档明确当前不依赖 SD 卡。
- 文档明确当前不依赖网络。
- 文档明确云端、App、SD 卡和大模型是未来预留方向。

## 玩家流程、背包养成与 UI 模块

任务来源：`v0.1/module3.txt` 中的玩家流程任务书。该模块只设计和记录
玩家体验；任何需要持久化字段、公共枚举、战斗包或协议变化的需求，都先
输出接口变更建议。

目标：

- 优化 `IDLE`、`PHOTO`、`WILD`、`CAPTURE_FAIL`、`BAG`、`MATCH`、
  `BATTLE`、`RELEASE_CONFIRM` 等界面流程。
- 完善背包展示：等级、XP、元素、胜率、当前选中状态。
- 规划升级逻辑、XP 奖励、胜负奖励和养成闭环。
- 规划战斗流程：宠物出场、战斗过程、结果、退场。
- 规划对战音效触发点，同时保留 `kAudioMuted` 静音开关。
- 设计本地友情机制：再战奖励、最近对手、友情提示。
- 玩家界面隐藏 `HOST`、`CLIENT` 等底层通信概念。
- 设备小字号信息区使用 glyph-safe ASCII 标签，避免 CoreS3 小字体缺字时
  显示方框乱码；大标题和底部按钮继续保留中文。

输出文档：

- `docs/player-flow-ui.md`

验收标准：

- 玩家能从拍照、捕捉、背包、`MATCH`、`BATTLE` 回到主流程。
- UI 信息结构清楚，按钮文案和动作一致。
- 战斗有 `READY`、`CLASH`、`RESULT` 等阶段过程，而不是只显示最终结果。
- 背包能体现养成进度和战斗记录。
- IDLE/WILD/BAG/MATCH/BATTLE 的小字号状态行不出现缺字方框，使用
  `SUBJ`、`REC`、`BAG`、`WIN`、`GROW`、`P/A/S`、`P1/P2` 等短标签。
- 静音开关仍有效。
- 不修改通信协议，不修改 BattlePetPacket，不改公共枚举值。

## iPhone App 规划模块

职责：

- 定义 iPhone App 产品规格。
- 定义宠物卡片、战绩、友情值和连接教程体验。
- 不直接实现 App。

MVP 页面：

- 首页。
- 背包。
- 宠物详情。
- 捕捉。
- 对战。
- 数据导出。
- 连接教程。
- 设置。

MVP 功能：

- 查看背包。
- 查看宠物详情。
- 触发拍照。
- 查看识别结果。
- 查看对战状态。
- 导出日志和样本。
- 查看宠物卡片、战绩和友情值。

## 本地连接工具模块

职责：

- 优先规划电脑端工具。
- 第一阶段使用 USB 串口。
- 不要求云端、不要求 SD 卡、不要求外网。

候选技术：

- Python + PySide。
- Tauri。
- Electron。

数据交换建议：

- 设备状态。
- 背包摘要。
- 宠物详情。
- 最近识别结果。
- 对战状态。
- 日志。
- 样本索引。

## 通信协议模块

职责：

- 维护当前 Wi-Fi AP + UDP 对战协议说明。
- 规划未来 App 管理协议。
- 不修改 `BattlePetPacket`。

建议：

- 对战协议继续使用当前 `BattlePetPacket`。
- App 管理协议单独设计，不和对战包混用。
- 新协议需要独立 magic、version、command 和 payload 约束。

候选命令：

- `GET_STATUS`
- `GET_BACKPACK_SUMMARY`
- `GET_PET_DETAIL`
- `CAPTURE_ONCE`
- `GET_LAST_RECOGNITION`
- `GET_BATTLE_STATUS`
- `EXPORT_LOG`
- `EXPORT_SAMPLE_INDEX`

## 通信、烧录与硬件验证模块

任务来源：`v0.1/module4.txt`。该模块接收当前工作树的可烧录固件和
交付材料，负责最终编译、烧录、端口记录和实机验收；不得在烧录前修改
公共头文件、背包格式或 UDP 包结构。

当前已放入模块的烧录输入：

- 主程序：`arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino`
- 编译脚本：`scripts/compile-demo.ps1`
- 当前建议编译根目录：`C:\tmp\m5_arduino_build_full_flash_ready`
- 最近编译记录：`release/oshw/verification-log.csv`
- 烧录交付说明：`release/oshw/delivery-notes.md`
- 本地 AI/网页桥接工具：`tools/ai_bridge/bridge.py`
- 发布包检查：`scripts/check-open-release-readiness.py`
- SD 资源包检查：`scripts/validate-sd-payload.py`
- HF 数据集发布预审：`scripts/audit_hf_dataset_publishability.py`
- HF 脱敏 manifest 导出：`scripts/export_hf_dataset_manifest.py`
- HF 脱敏 manifest：`release/oshw/hf-dataset-manifest.csv`

烧录前已完成的功能模块：

- 捕捉识别、失败门控、8 类识别到五行宠物生成链路。
- 宠物库视觉变体、图鉴、宠物状态、背包和放生确认。
- `/app` 识别调试面板、采样模式、实时质量仪表和样本导出。
- `/api/v1/status`、`/api/v1/storage`、`/api/v1/sampling`、
  `/api/v1/samples`、`/api/v1/samples/manifest`、
  `/api/v1/samples/file` 等本地 HTTP 验收入口。
- 串口 `STATUS`、`ACCEPTANCE`、`BAGSTATUS`、`SAMPLE`、`EDGE_HINT`、
  `HUSKY_HINT`、`SDINFO`、`SDPUT` 验收和调试命令。
- 电脑网页 `http://127.0.0.1:8790/` 和手机局域网页面
  `http://<电脑局域网 IPv4>:8791/` 的桥接入口；其中 AI 周边工坊只在
  电脑端保存资产，不写入固件或 SD 卡。
- SD 声音包、皮肤包、动作包及资源校验。
- 对战技能展示、回放日志、外部识别 hint 兼容路径。
- 对战分数面板已面向玩家显示“我方/对手”，不暴露 HOST/CLIENT。
- Edge Impulse/FOMO、HuskyLens、ESP-IDF 视觉实验骨架。
- OSHWHub/OSHWHLab 与 Hugging Face 发布材料草稿。

烧录模块仍需现场填写：

- `release/oshw/verification-log.csv` 中的端口、flash_ok、App、SD、
  捕捉、对战和双板结果。
- 两块 CoreS3 的实际烧录端口和烧录日志。
- 串口 `ACCEPTANCE` 四行输出截图或复制记录。
- `STATUS`、`BAGSTATUS`、`SAMPLE status`、`SDINFO` 的现场输出。
- `release/oshw/photos/` 下的公开实物照片。
- 白纸和反光场景的真实 CoreS3 negative 样本补采。
- Hugging Face 样本 license/privacy 审核结果。

## 云端增强模块

职责：

- 规划可选云端增强。
- 不绑定具体商业大模型。
- 不写死 API key。
- 不上传用户图片，除非未来 UI 明确授权。

输入可包括：

- 图片或缩略图。
- `ImageTraits`。
- `RecognitionResult`。
- 设备 ID。
- 背包上下文。

输出可包括：

- `objectLabel`
- `materialLabel`
- `elementHint`
- `speciesBias`
- `confidence`
- `petName`
- `evolutionHint`
- `flavorText`

固件接口建议：

- 复用 `fetch_remote_pet_hint`。
- 复用 `merge_generation_inputs`。
- 复用 `save_pet_snapshot`。
- 云端失败时回退本地结果。

## SD 卡模块

职责：

- 规划可选 SD 卡存储。
- 不把 SD 卡作为运行前提。
- 不声称 SD 卡能提升模型推理速度。

目录建议：

```text
/pets/
/captures/
/samples/
/logs/
/models/
```

保存内容：

- 样本。
- 日志。
- 截图。
- 宠物记录。
- 模型文件。

## 数据集与训练模块

职责：

- 规划 8 类样本采集。
- 规划电脑端训练。
- 规划导出 `vision_model_data.h`。
- 评估 TFLite Micro、Edge Impulse、ESP-DL。

样本类别：

- `plant_leaf`：植物叶片。
- `food_fruit`：食物或水果。
- `paper_book`：纸张或书本。
- `electronics_screen`：电子设备或屏幕。
- `metal_key_coin`：金属、钥匙或硬币。
- `fabric_cloth`：布料或衣物。
- `cup_bottle_water`：杯子、瓶子或水容器。
- `toy_figure`：玩具或手办。

禁止承诺：

- 不承诺 CoreS3 本地跑 YOLO。
- 不承诺 CoreS3 本地跑 CLIP。
- 不承诺 CoreS3 本地跑视觉大模型。
