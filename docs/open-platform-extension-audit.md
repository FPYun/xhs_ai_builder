# Open Platform Optimization Audit

## 2026-06-01 Sample API Update

Implemented from the remaining `/app` recognition debug route:

- Firmware logs a `scene` column in `/samples/manifest.csv` using the same scene
  buckets as the training script where possible.
- `/api/v1/sampling` adds RAM-only manual sample label/scene selection.
- `/api/v1/capture/quality` adds a pre-shot quality probe for the `/app` meter.
- `/api/v1/samples` summarizes SD sample rows by object class and scene.
- `/api/v1/samples/manifest` downloads the manifest for PC retraining.
- `/api/v1/samples/file?path=/samples/...` streams one allowed local sample
  artifact under `/samples/`.
- `/app` has a sample-debug panel on the capture page for quality, manual
  labels, sample counts, and manifest export.

Still remaining:

- Device-screen sampling label selection is still pending; `/app` now covers
  the same RAM-only operator label path.
- PC-side or richer `/app` debug view for burst candidates and per-frame
  recognition distances is still pending.

本文按外部开源平台项目常见做法，对当前 CoreS3 宠物项目做一次去重扫描：
已有实现或已有足够证据的内容不再放入待办；只保留仍需实施或验证的缺口。

## 已从待办删除

这些内容已经有代码或文档证据，不再作为新路线重复建设：

| 原方向 | 删除原因 | 当前证据 |
| --- | --- | --- |
| 基础拍照识别链路 | 已有 8 类本地轻量识别、失败门控和宠物生成。 | `docs/vision-generation-mapping.md` |
| 基础捕捉质量提示 | 已有失败页质量说明、`/app` 捕捉质量卡，以及 3 帧 burst 诊断日志。 | `docs/player-flow-ui.md`, `04_camera_pet_battle.ino` |
| SD 音频包 | 已有 `/audio/ui/`, `/audio/battle/`, `/audio/music/` 可选 raw 音频覆盖和回退。 | `docs/sd-card-file-boundary.md`, `sd_card_payload/` |
| 基础 `/app` 页面 | 已有状态、背包、识别、对战、日志、设置和音效测试页面。 | `docs/app-http-api.md`, `04_camera_pet_battle.ino` |
| 基础宠物状态 | 已有等级、阶段、XP、胜率、成长等待、当前选中、视觉变体名。 | `docs/usage.md`, `docs/taskbook-compliance.md` |
| 基础对战流程 | 已有本地 Wi-Fi AP + UDP 匹配、对战结算、三回合展示和友情/再战提示。 | `docs/player-flow-ui.md`, `docs/battle-link.md` |
| 公开样本训练入口 | 已有 COCO/Open Images 下载、bbox crop、硬负样本和原型模型导出。 | `scripts/train_vision_feature_model.py` |

## 保留的剩余路线

### 1. 采样模式和实时质量仪表

已有：burst 候选帧、质量分、主体分、失败原因、SD `/samples` 缩略图、`/app` 样本摘要。

本轮已补：

- `/app` 捕捉页可以切换自动标签/人工标签，并选择 8 类标签、`negative`
  和场景标签。
- 采样模式只影响 `/samples/manifest.csv` 的训练标签，识别、捕捉和宠物生成
  仍使用真实 `RecognitionResult`。
- `/api/v1/capture/quality` 提供拍照前质量探测：主体分、质量分、亮度、饱和度、
  对比度、中心差、接近传感器和场景提示。
- `/samples/manifest.csv` 新增 `distanceHint`，记录 `near`、`too_far`、
  `flat_background`、`visual_centered` 等距离/主体提示，方便筛选训练样本。
- USB 串口新增 `SAMPLE` 控制，可以在无浏览器调试时切换采样开关、
  标签和场景；它与 `/app` 使用同一个 RAM-only 采样状态。

剩余：

- CoreS3 设备屏幕上的采样标签选择还未实现，当前先由 `/app` 控制。

### 2. 训练脚本按场景统计

本轮已补：`scripts/train_vision_feature_model.py` 会在 `manifest.csv` 写入 `scene`，并在 `report.json` 输出：

- `scene_sample_counts`
- `scene_eval`

场景覆盖：

- `white_wall`
- `white_paper`
- `desktop`
- `glare`
- `dark`
- `bright`
- `low_texture`
- `hand_cover`
- `unknown`

剩余：

- 用真实 CoreS3 SD 样本复训，确认白墙、白纸、桌面、反光、暗光的误识别率。

### 3. `/app` 识别调试面板和样本导出

已有：`/api/v1/recognition/last`、`/api/v1/storage`、`/api/v1/logs`。

本轮已补：

- `/api/v1/samples`：列出 `/samples/manifest.csv` 摘要、每类/每场景计数和最近样本行。
- `/api/v1/samples/manifest`：下载 manifest。
- `/api/v1/samples/file?path=...`：只允许下载 `/samples/` 下的 `.ppm` 缩略图或 `.csv` 文件。
- `/app` 捕捉页增加“识别调试”面板，显示样本统计和 manifest 下载按钮。
- `/app` 最近样本行显示 `distanceHint`，辅助判断样本是否靠近、主体是否平坦、
  或仅靠视觉中心差判断。
- `/app` 捕捉页显示最近一次拍照 burst 的逐帧候选，包含 quality、
  presence、confidence、bestDistance、margin、分类来源、画面特征和失败原因。

剩余：

- 设备屏幕采样标签选择和更完整的场景标注仍未实现。

### 4. 宠物状态系统和图鉴

已有：基础等级、阶段、XP、战绩、成长目标、视觉变体名和背包详情。

本轮已补：

- `/api/v1/encyclopedia` 返回 `5 x 3 x 3 = 45` 个本地视觉模板。
- `/app` 增加“图鉴”页，按当前背包实时标记已拥有模板和最高等级。
- 宠物 JSON 增加 RAM-only `care` 显示对象：饱食、精力、亲和、专注、
  心情和状态提示，全部由现有字段派生。
- `/app` 背包和当前伙伴卡展示饱食、精力、亲和与状态提示。
- 图鉴不新增持久化字段，不修改 `SavedPet`，也不进入 UDP 对战包。

剩余：

- 若要把饱食、精力、亲和等状态持久化，必须先提交公共接口变更建议，
  不能直接改背包格式。

### 5. SD 声音包、皮肤包、宠物动作包

已有：SD 声音包、`sd_card_payload/manifest.csv`、串口 `SDPUT` 上传脚本。

本轮已补：

- `sd_card_payload/skins/manifest.csv` 和 `sd_card_payload/actions/manifest.csv`
  作为皮肤包/动作包占位索引。
- `/api/v1/storage` 增加 `addonManifests`，报告 `/skins/manifest.csv` 和
  `/actions/manifest.csv` 是否存在。
- `sd_card_payload/skins/palettes/*.csv` 提供五行 `body` / `accent` 示例调色板。
- 固件会可选读取 `/skins/palettes/default.csv` 和
  `/skins/palettes/<element>.csv`，覆盖本机宠物绘制颜色；缺失或格式错误时
  回退内置颜色。
- `sd_card_payload/actions/*.csv` 提供 `idle`、`wild`、`bag`、
  `battle_clash`、`result` 动作参数示例。
- 固件会按当前屏幕可选读取 `/actions/*.csv` 的 `bob`、`sparkle`、
  `tilt`，只改变本地绘制姿态、闪光和动作线；缺失或格式错误时回退
  固件内置静态绘制。

剩余：

- 资源 manifest 还需增加校验、版本兼容和 fallback 行为字段。

### 6. 对战技能和回放日志

已有：三回合对战动画、结算、友情、再战、RAM 日志。

本轮已补：

- RAM-only 对战回放环形日志，最近 6 局记录 `battleId`、结果、分数、
  力/克/心差值、对手基础 species/element/level、XP、友情奖励和成长标记。
- 每个五行元素有 3 个本地技能名原型，按种子、对局编号和设备 ID 派生，
  仅用于结算和战报展示，不参与胜负计算。
- `/api/v1/battle` 内联 `replays`，并新增 `/api/v1/battle/replays` 供 App
  或调试工具单独读取最近战报。
- `/app` 对战页展示最近对局，不修改 `BattlePetPacket`、UDP 包结构或背包
  存储。

剩余：

- 回放日志当前只记录本地视角，不跨板同步；若要跨板同步必须先做接口
  变更评审。
- 当前技能是显示原型，不影响分数。若要让技能改变结算或同步技能选择，
  必须先输出 `BattlePetPacket` 接口变更建议。

### 7. Edge Impulse / FOMO 实验分支

当前状态：仅保留路线建议，未接入主线。

本轮已补：

- 新增 `docs/experimental-recognition-routes.md`，明确 Edge Impulse/FOMO 只能
  作为独立实验分支，输出必须降级映射到现有 `RecognitionResult`。
- 定义编译、heap、推理耗时、白墙/白纸/桌面 negative 误识别率和 8 类准确率
  验收指标。
- 新增 `experiments/edge_impulse_fomo/`，包含 label map 和 acceptance log
  模板，便于后续实验记录模型尺寸、heap、推理耗时和误识别率。
- 新增 `experiments/edge_impulse_fomo/edge_output_adapter.py` 和
  `serial-hint-protocol.md`，先把 Edge Impulse 分类或 FOMO `bounding_boxes`
  输出降级成现有 8 类提示，并验证 negative/低置信度 fallback。
- 固件串口新增 `EDGE_HINT <class> <confidence> <presence>` 实验入口，作为
  下一次拍照的一次性 RAM-only 提示；它仍必须经过 CoreS3 当前画面的主体/
  背景门控，不持久化 bbox/logits/model metadata。

剩余：

- 导入真实 Edge Impulse 导出库并建立独立编译工程。
- 用 CoreS3 实拍样本填充 acceptance log。
- 通过前不能关闭当前轻量特征模型 fallback。

### 8. HuskyLens 外接识别模块

当前状态：未实现。

本轮已补：

- `docs/experimental-recognition-routes.md` 记录 HuskyLens 作为可选协处理器
  的边界：I2C/UART 结果只映射到现有 8 类，不作为 CoreS3 本地运行前提。
- 明确模块缺失、超时、未映射 ID 时必须回退当前本地识别路径。
- 新增 `experiments/huskylens/`，包含 ID 映射表和 acceptance log 模板。
- 新增 `experiments/huskylens/huskylens_bridge.py` 和
  `serial-hint-protocol.md`，用于在接实物前验证 ID 映射、低置信度 fallback
  和未来串口提示边界。
- 固件串口新增 `HUSKY_HINT <class> <confidence> <presence>` 实验入口，允许
  桌面桥接器先验证 ID 映射到 8 类后的主线行为；不引入 HuskyLens Arduino
  依赖，也不改变公共存储或 UDP 包。

剩余：

- 接入实物模块后补 I2C/UART 读数代码和实机日志。
- 填写接线、供电、波特率或 I2C 地址的实测值。

### 9. ESP-IDF + ESP-DL / ESP-WHO 高阶版本

当前状态：只适合未来分支，不适合直接塞入 Arduino sketch。

本轮已补：

- `docs/experimental-recognition-routes.md` 记录 ESP-IDF PoC 的验收顺序：
  先 GC0308 取帧和内存稳定，再测小模型推理耗时，最后才评估公共逻辑复用。
- 明确 ESP-DL/ESP-WHO 不进入当前 Arduino 主线，不新增依赖。
- 新增 `experiments/esp_idf_vision_poc/`，包含 PoC 里程碑和 acceptance
  log 模板。
- `experiments/esp_idf_vision_poc/` 现在包含独立 ESP-IDF 工程骨架：
  `CMakeLists.txt`、`sdkconfig.defaults`、`main/main.c` 和构建说明。
  当前只做输出契约和 heap 日志 smoke test，尚未接相机和模型。

剩余：

- 在安装 ESP-IDF 后运行 `idf.py build` 并记录结果。
- 先验证 GC0308 取帧、PSRAM、推理耗时、模型大小。
- 通过后再评估是否迁移公共逻辑。

### 10. OSHWHub / OSHWHLab 开源硬件套件和 Hugging Face 数据集

已有：项目文档、固件、SD 音频 payload、训练脚本。

本轮已补：

- 新增 `release/oshw/` 发布资料骨架：BOM、验证日志模板、公开演示视频
  shot list、Hugging Face 数据集卡模板。
- README 增加 `experiments/` 和 `release/oshw/` 目录说明。
- 新增 `scripts/check-open-release-readiness.py`，自动检查发布包必需文件、
  填写后的验收日志、Hugging Face 数据集卡、样本 manifest/report、公开照片
  和明显 API key/Token 标记。
- 新增 `scripts/build-hf-dataset-card.py`，从本地 `manifest.csv` 和
  `report.json` 生成 Hugging Face 数据集卡草稿；草稿仍保持发布检查
  `PENDING`，直到 license/privacy 审核完成。
- 新增 `scripts/audit_vision_scene_coverage.py`，检查白墙、白纸、桌面、
  反光、暗光是否各有足够 negative 样本；发布检查会把缺失场景保持为
  `PENDING`。

剩余：

- OSHWHub/OSHWHLab 发布包仍缺实物照片、外壳或支架、接线/装配图和实测
  视频素材。
- Hugging Face 数据集包仍缺可发布的 CoreS3 实拍样本、manifest、license
  确认和训练报告。
- 发布前必须剔除 API key、私人照片、不可再分发素材和未授权音频。

## 推荐执行顺序

1. 采样模式 + `/api/v1/samples` + `/app` 调试页。
2. CoreS3 实拍场景样本复训，并用 `scene_eval` 判断白墙/白纸/桌面误识别率。
3. 图鉴页和 RAM-only 宠物状态。
4. 战报日志和 RAM-only 技能原型。
5. SD 皮肤/动作包 manifest。
6. Edge Impulse/FOMO 与 HuskyLens 独立实验分支。
7. ESP-IDF 高阶分支。
8. 开源硬件发布包和 Hugging Face 数据集包。

## 接口结论

当前剩余路线不需要立即修改公共头文件。任何涉及以下内容的持久化或通信同步，都必须先输出接口变更建议：

- 新宠物状态字段。
- 图鉴持久化字段。
- 技能 ID 或技能冷却。
- 对战回放跨板同步字段。
- HuskyLens bbox、class logits 或外部识别元数据。
- 样本 metadata 写入公共存储结构。
