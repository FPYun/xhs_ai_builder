# SD Card File Boundary

本文档区分当前项目中“必须编译/烧录到 CoreS3 单片机 Flash 的内容”和“可以放到 SD 卡的可选内容”。完整的内存和程序位置规划见 `docs/memory-program-placement.md`。

## 当前结论

- SD 卡可以为空；插入 SD 卡会让 `/api/v1/status` 的 `sdCardPresent` 返回 `true`，并可选覆盖场景音效。
- 捕捉、背包、识别、对战、手机 `/app` 页面和 HTTP API 当前都必须在无 SD 卡时可运行。
- 任何把模型、网页、背包或对战协议迁移到 SD 卡的改动，都属于后续功能变更，不是当前运行前提。
- SD 音频已经实现为可选覆盖：固件会优先读取 `/audio/.../*.raw`，缺失、为空或超过大小上限时回退到内置合成音效。
- SD 皮肤调色板已经实现为可选覆盖：固件会读取 `/skins/palettes/default.csv`
  和 `/skins/palettes/<element>.csv` 的 `body` / `accent` RGB 行，缺失或格式错误时回退内置颜色。
- SD 样本导出已经实现为只读 HTTP 接口：`/api/v1/samples` 汇总
  `/samples/manifest.csv`，`/api/v1/samples/manifest` 下载 manifest，
  `/api/v1/samples/file` 只允许下载 `/samples/` 下的 `.csv` 或 `.ppm`。

## 必须在单片机固件内的内容

这些内容必须通过 Arduino 编译并烧录进 CoreS3 Flash；烧录后不是以普通文件形式放在 SD 卡上。

| 路径 | 原因 |
| --- | --- |
| `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino` | 主程序、UI、Wi-Fi AP、HTTP API、UDP 对战、SD 检测入口。 |
| `arduino_demos/04_camera_pet_battle/pet_model.h` | 公共宠物和背包存储类型，当前不能放到 SD 卡替代。 |
| `arduino_demos/04_camera_pet_battle/vision_types.h` | 公共识别结果类型，当前由固件直接使用。 |
| `arduino_demos/04_camera_pet_battle/ui_types.h` | 公共 UI 状态和动作类型。 |
| `arduino_demos/04_camera_pet_battle/battle_protocol.h` | `BattlePetPacket` UDP 对战包结构，必须保持固件内一致。 |
| `arduino_demos/04_camera_pet_battle/vision_model_data.h` | 当前轻量识别模型数据编译进固件；放 SD 需要新增加载和回退逻辑。 |
| `arduino_demos/04_camera_pet_battle/trainer_intro_audio.h` | 当前启动音频样本编译进固件；SD `/audio/music/intro.raw` 只能作为可选覆盖。 |
| 固件内置 `/app` HTML/CSS/JS 字符串 | 当前手机网页由固件直接服务，不能仅放 SD 卡。 |

## 可以放到 SD 卡的可选内容

这些内容适合作为可选扩展数据；没有它们时固件仍必须正常运行。

| 建议 SD 路径 | 内容 | 当前状态 |
| --- | --- | --- |
| `/audio/ui/*.raw` | UI 音效资源。 | 已实现可选读取；缺失时回退内置音效。 |
| `/audio/battle/*.raw` | 对战音效资源。 | 已实现可选读取；缺失时回退内置音效。 |
| `/audio/music/*.raw` | 启动或背景音资源。 | 已实现可选读取；固件内置启动音频仍保留。 |
| `/skins/manifest.csv` | 皮肤包、调色板或绘制参数索引。 | 已有 manifest 和 `/api/v1/storage` 存在性报告。 |
| `/skins/palettes/*.csv` | 五行宠物 `body` / `accent` 调色板。 | 已实现可选读取；缺失时回退内置颜色。 |
| `/actions/manifest.csv` | 宠物动作包、时间轴或动作参数索引。 | 已实现可选动作参数读取；缺失时回退固件内置静态绘制。 |
| `/actions/*.csv` | idle/wild/bag/battle/result 屏幕的 `bob`、`sparkle`、`tilt` 绘制参数。 | 已实现本地绘制层读取；只影响屏幕表现，不进入存档或 UDP。 |
| `/captures/` | 拍照截图或原始采集样本。 | 未来可选导出，不是当前运行依赖。 |
| `/samples/` | 训练样本、人工标签、样本索引。 | 已实现采样写入和只读 HTTP 导出；电脑端训练使用。 |
| `/logs/` | 运行日志、App 事件日志、对战记录导出。 | 未来可选导出。 |
| `/pets/` | 宠物快照、战绩、可导出记录。 | 未来可选导出；当前背包仍用固件偏好存储。 |
| `/models/` | 训练导出模型或模型候选文件。 | 未来评估；当前模型必须在 `vision_model_data.h`。 |

## 不能仅放到 SD 卡的内容

- `scripts/*.ps1`、`scripts/*.py`：开发机工具，只在电脑上运行，不属于单片机运行文件。
- `docs/*.md`、`README.md`：项目文档，只用于开发和验收。
- `firmware/official/*.bin`：官方固件归档，只用于电脑端烧录或恢复；不能靠放入 SD 卡让当前 Demo 运行。
- 公共头文件和 UDP 包定义：不能用 SD 卡上的文件覆盖，否则会破坏编译期类型和协议一致性。
- `scripts/upload-sd-audio.py`：电脑端串口上传工具，只通过 USB 串口调用固件 `SDPUT`，不是 SD 卡运行文件。

## 后续迁移规则

- 先保持固件内默认资源可用，再尝试从 SD 卡加载可选资源。
- SD 读取失败只能降级，不能阻塞捕捉、背包、对战或手机控制。
- 模型、宠物存储、App 页面或协议字段如果要迁移到 SD 卡，必须先提交接口变更建议。
