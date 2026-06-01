# AI 大模型赋能附加计划

本文档是附加规划，非当前运行依赖。它说明 GPT、DeepSeek、Mimo 等大模型如何为 `04_camera_pet_battle` 赋能，重点围绕宠物人格/文案生成和好友社交/互动事件增强。

当前固件仍必须保持 CoreS3 本地离线可运行。AI 增强不改变现有拍照、轻量识别、宠物生成、背包、对战和 RAM-only 好友流程；不修改公共头文件、UDP 对战包结构或存档结构。

## 基本边界

- 不把 API key、私有 token 或真实凭证写入固件、公开仓库或默认配置。
- 不绑定具体商业大模型；GPT、DeepSeek、Mimo 都作为可替换 provider。
- 不默认上传图片；图片或缩略图增强必须由用户手动确认后才启用。
- 不承诺 CoreS3 本地运行 YOLO、CLIP 或视觉大模型。
- 无网络、无 API key、模型超时或模型失败时，App/网页端必须回退到本地模板文案。
- 第一阶段只在 App、网页端或电脑端工具展示 AI 内容，不写入 `SavedPet`、`BackpackStorage` 或公共协议类型。

## 推荐架构

CoreS3 继续负责真实玩法：

1. 本地拍照。
2. 本地轻量限定类别识别。
3. 本地宠物生成。
4. Preferences 背包。
5. Wi-Fi AP + UDP 对战。
6. RAM-only 好友和连战状态。

App、网页端或电脑端工具负责可选增强：

1. 读取 `/api/v1/status`、`/api/v1/backpack`、`/api/v1/battle`、`/api/v1/logs`。
2. 组合宠物快照或好友快照。
3. 由用户选择 GPT、DeepSeek、Mimo 或本地模板。
4. 将快照发给选定 provider。
5. 解析结构化 JSON。
6. 展示人格卡、故事卡、好友事件卡和下一步互动建议。

推荐数据流：

```text
CoreS3 local gameplay
  -> local HTTP JSON
  -> App / browser / desktop tool
  -> optional AI provider
  -> structured JSON
  -> display-only persona and social cards
```

## 模型分工建议

GPT 适合：

- 图片或缩略图理解。
- 宠物命名。
- 捕捉故事。
- 进化提示。
- 复杂结构化 JSON。
- 多轮风格统一。

DeepSeek 适合：

- 低成本文本生成。
- 中文对战总结。
- 好友事件文案。
- 日志摘要。
- 规则解释。
- 无图片输入时的结构化文本增强。

Mimo 先作为可替换模型位置保留：

- 如果实际使用的是 App 原型或应用生成平台，可辅助制作 App 原型、页面文案和交互说明。
- 如果实际使用的是支持图片输入的模型，可接入图像增强识别。
- 在确认具体 API、费用、上下文长度、图片能力和 JSON 约束前，不写死 Mimo 能力。

## 宠物人格增强

宠物人格增强用于让每只宠物更像一个有性格的伙伴，而不是只显示元素、等级和战绩。

### 输入快照

输入来自现有本地数据，不要求新增公共结构：

```json
{
  "source": {
    "imageTraits": {
      "brightness": 128,
      "saturation": 72,
      "contrast": 41
    },
    "recognition": {
      "objectLabel": "杯子",
      "materialLabel": "陶瓷",
      "elementHint": "土",
      "confidence": 0.78
    }
  },
  "pet": {
    "species": "岩陶兽",
    "element": "土",
    "level": 3,
    "stage": "幼体",
    "xp": 22,
    "wins": 1,
    "battles": 2,
    "growthGoal": "再战一次"
  },
  "context": {
    "bagCount": 2,
    "activePet": true
  }
}
```

### 输出 JSON

模型应只返回展示用字段：

```json
{
  "petName": "小陶丘",
  "personality": "慢热、护主、喜欢安静观察",
  "catchStory": "它像刚从杯沿滚落的一粒暖土，听到拍照声后悄悄探头。",
  "evolutionHint": "多参加对战并保持胜率，它会向更坚硬的守护形态进化。",
  "idleLine": "今天也想待在你身边晒一会儿太阳。",
  "battleLine": "别急，我会稳稳挡住这一击。",
  "flavorText": "由陶瓷质感和土元素倾向激发的守护型伙伴。"
}
```

### 使用规则

- 输出只在 App/网页端显示。
- 不覆盖固件内的 `species_name`、等级、元素、战绩或成长逻辑。
- 不把 AI 生成昵称写入 `SavedPet`。
- 如果后续要持久化 `petName`、人格或故事，必须先提出接口变更建议。

### 叙事素材边界

- 可以参考收集养成、伙伴冒险、竞技复赛、社团日常、轻喜剧反差等常见游戏和动画叙事结构。
- 不使用真实游戏或动漫的角色名、专有地名、组织名、招式名、原台词或可识别剧情桥段。
- 生成内容要服务项目自己的五行元素、拍照捕捉、背包养成、对战和好友羁绊系统。
- 建议额外生成 `likes`、`quirk`、`growthArc`、`dailyRitual`、`signatureMove` 等展示字段，让宠物更像可长期养成的伙伴。

## 好友社交增强

好友社交增强用于把最近对手、好友分数、连战奖励和对战日志变成玩家能理解的关系事件。

### 输入快照

```json
{
  "relationship": {
    "recentOpponent": "A1B2C3",
    "friendshipScore": 64,
    "bondLabel": "好友",
    "rematchStreak": 2,
    "friendBattleCount": 3
  },
  "battle": {
    "result": "win",
    "scoreDiff": 12,
    "xpReward": 18,
    "friendBonusXp": 10
  },
  "pets": {
    "mine": {
      "name": "小陶丘",
      "element": "土",
      "level": 3
    },
    "opponent": {
      "name": "焰尾兽",
      "element": "火",
      "level": 4
    }
  }
}
```

### 输出 JSON

```json
{
  "eventTitle": "再战后的默契",
  "eventText": "小陶丘和焰尾兽已经认得彼此的节奏。第二次交锋后，它们不再只是对手，更像是在互相试探成长速度。",
  "relationshipLabel": "稳定好友",
  "nextActionHint": "再次对战可以冲击密友阶段，并获得更高连战奖励。",
  "rematchFlavorText": "它记住了上一次的火光，这次防守更稳了。"
}
```

### 使用规则

- 好友页展示羁绊动态、最近对手故事和下一次互动建议。
- 对战页可展示短句，不显示 HOST/CLIENT、UDP 包、设备 MAC 或底层连接细节。
- 事件只解释已有数据，不改变好友分数、连战奖励或对战结果。
- 当前好友记录仍是 RAM-only；持久化好友、账号、社区关系链属于后续独立功能。

### 事件素材边界

- 可以参考“宿敌复赛”“共同训练”“赛后互相认可”“下一次约战”等通用关系结构。
- 不复制具体动画集数、游戏任务、角色关系或台词。
- 建议额外生成 `memoryTag`、`relationshipMoment`、`sharedGoal` 等字段，方便好友页展示长期羁绊动态。

## Provider 接入原则

客户端可以抽象出统一 provider，不把模型细节写进固件：

```text
enrichPet(snapshot, provider) -> PersonaProfile
enrichSocial(snapshot, provider) -> SocialEventCard
```

最小 provider 配置：

- `providerName`：`gpt`、`deepseek`、`mimo` 或 `local-template`。
- `baseUrl`：用户本地配置或后端配置。
- `modelName`：用户选择。
- `apiKey`：只保存在用户本地安全配置或后端，不进入仓库。
- `allowImageUpload`：默认 `false`。

失败处理：

- 超时：回退到本地模板。
- JSON 无效：忽略 AI 结果，显示本地模板。
- 缺字段：只展示有效字段，其余用本地模板补齐。
- 用户未配置 API key：使用本地模板。
- 用户未确认图片上传：只发送文本快照。

## 当前 `/app` 网页接入

固件内置 `/app` 页面已经提供可选 AI 面板，位于“更多 / AI 大模型”。它由手机浏览器直接发起请求，不让 CoreS3 保存 API key，也不让固件主动访问云端。

支持的模式：

- `本地模板`：默认模式，不联网，不需要 API key。
- `GPT / OpenAI`：默认使用 OpenAI-compatible chat completions 请求；用户可填写模型名、API key 和可选接口地址。
- `DeepSeek`：默认使用 DeepSeek chat completions 请求；用户可填写模型名、API key 和可选接口地址。
- `Mimo / Anthropic`：电脑桥接工具页面可填写 `https://api.xiaomimimo.com/anthropic`，实际请求会按官方 Anthropic Messages 格式发送到 `/v1/messages`，认证头使用 `api-key`，默认模型为 `mimo-v2.5`，用户也可改成 `mimo-v2.5-pro`。
- `自有代理`：推荐正式使用方式；浏览器只请求用户自己的 HTTPS 代理，由代理安全保存真实 provider key。

当前网页只发送文本 JSON 快照，不上传图片或缩略图。浏览器直连商业模型可能因为 CORS 或密钥暴露风险失败；正式部署时应优先使用电脑端桥接工具或自有代理。

## 电脑桥接工具的永久保存

`tools/ai_bridge/bridge.py` 会把每次 AI 生成或本地模板回退结果追加保存到电脑本地：

```text
~/.m5_ai_bridge/ai_records.jsonl
```

保存内容包括：

- 生成时间。
- 类型：`pet` 或 `social`。
- provider 和模型名。
- 是否为本地模板或失败回退。
- 当次文本快照。
- 生成后的结构化 JSON。

不会保存：

- API Key。
- 图片、缩略图或原始摄像头数据。
- 固件存档结构。
- UDP 对战包。

网页会显示“永久保存记录”区域，可刷新查看最近生成的卡片。该保存只发生在电脑端，不改变 CoreS3 离线运行能力。

## 分阶段落地

### Phase 1：附加文档和 JSON 示例

- 只新增本文档。
- 不改固件源码。
- 不改现有 README、架构、协议或 App HTTP 文档。
- 不接真实模型。

### Phase 2：本地模板

- App/网页端增加本地模板文案。
- 生成宠物人格卡和好友事件卡。
- 不联网，不需要 API key。

### Phase 3：可选 AI Provider

- App、网页端或电脑端工具加入可选 AI 开关。
- 用户手动配置 GPT、DeepSeek、Mimo provider。
- 默认只发送文本 JSON 快照。

### Phase 4：图片增强

- 仅在用户明确确认后上传缩略图或图片。
- GPT 可优先用于图片理解。
- DeepSeek 和 Mimo 是否用于图片增强，以实际 API 能力为准。

### Phase 5：账号和社区

- 再讨论账号、云同步、好友持久化、社区分享和排行榜。
- 这些能力会改变数据边界，必须另起接口变更建议。

## 验收标准

- 本文档位于 `docs/addons/ai-persona-social-roadmap.md`。
- 文档明确说明 AI 是可选增强，不是运行依赖。
- 文档不包含真实 API key、私有 token 或固定云端凭证。
- 文档明确说明默认不上传图片。
- 文档没有要求修改公共头文件、UDP 对战包或固件存档结构。
- 文档说明模型失败时必须回退到本地模板。
