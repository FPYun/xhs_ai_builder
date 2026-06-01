# M5 Camera Pet Battle

一个运行在 M5Stack CoreS3 上的本地离线宠物捕捉与对战 Demo。

CoreS3 负责拍照、轻量识别、五行宠物生成、背包、养成和双板对战；电脑和手机页面用于查看状态、调试、导出样本和扩展 AI 周边体验。当前固件不依赖云端、不依赖路由器、不依赖 SD 卡，也没有语音识别。

## 当前版本状态

| 模块 | 当前状态 |
| --- | --- |
| 硬件平台 | M5Stack CoreS3 / ESP32-S3 |
| 主程序 | `arduino_demos/04_camera_pet_battle/04_camera_pet_battle.ino` |
| 识别方式 | 本地轻量 8 类特征识别，不是通用视觉大模型 |
| 宠物系统 | 五行元素、物种变体、等级、XP、战绩、成长状态 |
| 背包存储 | ESP32 `Preferences`，最多 6 只宠物 |
| 对战通信 | Wi-Fi SoftAP + UDP，双板本地自动配对 |
| 手机页面 | 连接 CoreS3 AP 后访问设备内置 `/app` |
| 电脑桥接 | USB 串口 + 本地网页，可选 AI 人格/周边生成 |
| SD 卡 | 可选资源和样本存储；无 SD 卡仍可运行 |
| 云端能力 | 仅作为电脑桥接的可选增强，不写入固件 |
| 语音识别 | 当前没有实现，也不会在本版本恢复 |

## 核心体验

- 拍照捕捉：CoreS3 相机采集画面，识别明确居中的物体后生成野生宠物。
- 失败门控：白墙、白纸、反光、暗光等低质量场景会进入捕捉失败，不强行生成宠物。
- 背包养成：查看、选择、放生确认、等级、XP、战绩、胜率和成长提示。
- 双板对战：两块 CoreS3 同时进入 MATCH 后自动发现、连接、交锋并结算 XP。
- 好友互动：最近对手、友情分、连战奖励和再战提示保持在本地 RAM 状态。
- 声音系统：内置场景音效，可静音；SD 卡可选覆盖部分 `.raw` 音效。
- 样本导出：可通过 SD 卡和 HTTP 接口导出样本摘要，为后续训练服务。
- AI 周边工坊：电脑端可选调用本地配置的大模型，生成宠物人格、2D 形象、语音文件和周边文案；这些资产保存到电脑本地，不进入固件主链路。

## 快速编译与烧录

编译主程序：

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_full_flash_ready
```

烧录到 CoreS3：

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_full_flash_ready `
  -Upload `
  -Port COM7
```

`COM7` 需要替换成实际端口。双板对战需要两块 CoreS3 分别烧录同一主程序。

更完整的烧录说明见 [docs/flashing.md](docs/flashing.md)，现场验收清单见 [docs/device-acceptance.md](docs/device-acceptance.md)。

## 页面入口

### CoreS3 内置手机页

1. 烧录并启动 CoreS3。
2. 手机连接设备 AP，SSID 通常为 `M5PET-xxxxxx`。
3. 浏览器打开设备页面：`http://10.23.x.1/app`，具体 IP 以屏幕或串口状态为准。

这个页面来自固件本身，不需要互联网。

### 电脑 AI 桥接页

电脑通过 USB 串口连接 CoreS3，并运行：

```powershell
E:\Anaconda\python.exe .\tools\ai_bridge\bridge.py --host 127.0.0.1 --http-port 8790
```

电脑浏览器打开：

```text
http://127.0.0.1:8790/
```

如果需要让同一局域网手机访问电脑桥接页，可另开一个监听：

```powershell
E:\Anaconda\python.exe .\tools\ai_bridge\bridge.py --host 0.0.0.0 --http-port 8791
```

手机浏览器打开：

```text
http://<电脑局域网 IPv4>:8791/
```

注意：电脑桥接页可以使用本地配置的 GPT、DeepSeek 或 Mimo API key；API key 不应写入仓库、固件或公开文档。

## 串口验收命令

烧录后建议用串口监视器检查：

```text
STATUS
ACCEPTANCE
BAGSTATUS
SAMPLE status
SDINFO
```

- `STATUS`：当前屏幕、按钮、背包、宠物、好友和对战摘要。
- `ACCEPTANCE`：面向现场验收的流程、对战、好友和运行状态摘要。
- `BAGSTATUS`：背包宠物 JSON，用于电脑桥接页读取每只宠物。
- `SAMPLE status`：当前样本标签和场景配置。
- `SDINFO`：SD 卡容量、资源和样本文件状态。

## 仓库结构

```text
arduino_demos/
  04_camera_pet_battle/        CoreS3 主程序
docs/                          架构、使用、协议、烧录、验收和路线文档
scripts/                       编译、烧录、训练、发布检查和 SD 上传工具
tools/ai_bridge/               USB 串口 + 本地网页 + 可选 AI 周边工坊
sd_card_payload/               可选 SD 音效、皮肤和动作资源包
experiments/                   Edge Impulse/FOMO、HuskyLens、ESP-IDF 实验骨架
release/oshw/                  开源硬件和 Hugging Face 数据集发布材料草稿
v0.1/                          模块任务书和协作边界
```

## 重要文档

- [使用说明](docs/usage.md)
- [烧录说明](docs/flashing.md)
- [系统架构](docs/architecture.md)
- [协议与公共类型边界](docs/protocol.md)
- [本地 HTTP API 与 `/app`](docs/app-http-api.md)
- [玩家流程与 UI](docs/player-flow-ui.md)
- [设备验收清单](docs/device-acceptance.md)
- [模块任务总表](docs/module-tasks.md)
- [产品化路线](docs/app-cloud-roadmap.md)
- [SD 卡边界](docs/sd-card-file-boundary.md)
- [开源发布包](docs/open-source-release-package.md)
- [v0.2 发布说明](docs/release-v0.2.md)

## 当前边界

- 不重新定义公共类型，不私自修改公共头文件字段、枚举值或 UDP 包结构。
- 不修改 `BattlePetPacket`。
- 不恢复语音识别。
- 不引入新依赖。
- 不写死云端 API key。
- CoreS3 当前仍保持本地离线可运行。
- 云端识别、账号、社区、BLE 发现、真实 3D 模型生成和 App Store 发布仍属于后续规划。

## 发布前检查

```powershell
.\scripts\compile-demo.ps1 `
  -Sketch .\arduino_demos\04_camera_pet_battle\04_camera_pet_battle.ino `
  -BuildRoot C:\tmp\m5_arduino_build_full_flash_ready

python .\tools\ai_bridge\bridge.py --self-test
python .\scripts\check-open-release-readiness.py
```

当前仓库是可运行的项目快照，不是最终量产固件。公开发布样本或数据集前，还需要补齐真实 CoreS3 样本、license/privacy 审核、实物照片和完整硬件验收记录。
