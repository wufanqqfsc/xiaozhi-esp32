# 贾维斯百度 GIF 搜索与显示功能实现计划

## 目标

通过前后端协同，让贾维斯能够自动在 `https://image.baidu.com/` 上搜索用户要求的 GIF 图片，下载并在 ESP32 设备上显示。

## 架构流程

```
用户语音 → "帮我搜一个猫咪的GIF"
    ↓
STT → LLM → 调用 `search_and_display_gif` 工具 (后端 GlobalFunction)
    ↓
后端:
  1. 调用百度图片搜索 API → 获取候选 GIF URL 列表
  2. 下载验证图片 (格式/大小检查) → 保存到本地临时目录
  3. 通过本地 HTTP 端点提供图片访问
  4. 调用设备 MCP `self.screen.display_gif` → 设备下载并显示
    ↓
设备: 从后端 HTTP 端点下载图片 → LVGL 显示 (Jarvis 视图上叠加 300x300 圆形区域)
    ↓
LLM → 语音回复用户: "已经为您找到并显示了猫咪的GIF图片"
```

## 现有基础设施分析

### 已有的设备端 MCP 工具
- **`self.screen.display_gif`** (mcp_server.cc:185-229)
  - 输入: `url` (string)
  - 功能: HTTP GET 下载图片 → 创建 `LvglAllocatedImage` → `ShowImageOnActiveView()` 显示
  - 支持 PNG/JPG/GIF，GIF 在 Jarvis 视图激活时支持动画播放
  - 显示时长: 5 秒
  - **无需修改**

### 已有的后端工具注册机制
- **`ToolsGlobalRegistry.GlobalFunction`** 接口 (ToolsGlobalRegistry.java:109-121)
  - 实现 `@Component` 的 `GlobalFunction` 会被 `SystemToolRegistrar` 自动发现并注册
  - `FunctionToolCallback.builder()` 构建 LLM 可调用的工具
  - `returnDirect=false` 让 LLM 根据工具结果生成自然语言回复
- **参考实现**: SessionExitFunction.java

### 已有的设备 MCP 调用机制
- **`DeviceMcpService.callDeviceTool(deviceId, toolName, args)`** (DeviceMcpService.java:90-115)
  - 从 ToolContext 获取 `sessionId` 和 `deviceId`
  - 通过 WebSocket 发送 MCP `tools/call` 到设备
  - 15 秒超时等待设备响应

### 设备图片显示机制
- **`FortuneWatchfaceView::ShowImage()`** — Jarvis 视图激活时，创建 `LvglGif` 控制器播放 GIF 动画
- **`AttitudeDisplay::ShowImageOnActiveView()`** — 根据当前视图自动路由
- 屏幕尺寸: 360×360，图片显示区域: 300×300 圆形

## 实现方案

### 文件 1: `BaiduImageSearchFunction.java` (新建)

**路径**: `xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/dialogue/llm/tool/function/BaiduImageSearchFunction.java`

**职责**: LLM 工具 — 搜索百度图片、下载到本地、调用设备显示

**工具定义**:
- 工具名: `search_and_display_gif`
- 描述: "搜索网络图片并在设备屏幕上显示。当用户要求查看某个主题的图片、GIF动图时调用此工具。"
- 参数:
  - `query` (string, 必填): 搜索关键词，如"猫咪 GIF"、"搞笑表情包"
  - `gif_only` (boolean, 可选, 默认 false): 是否只搜索 GIF 动图

**核心逻辑**:
1. 调用百度图片搜索 API:
   - URL: `https://image.baidu.com/search/acjson?tn=resultjson_com&word={query}&pn=0&rn=20&ie=utf-8`
   - 解析 JSON 响应中的 `data` 数组
   - 每项包含 `thumbURL`、`objURL`、`type`、`width`、`height` 等字段
2. 筛选候选图片:
   - 如果 `gif_only=true`，只保留 `type` 为 `"gif"` 或 URL 以 `.gif` 结尾的结果
   - 优先选择宽高 ≤ 500px、文件大小 ≤ 500KB 的图片 (适配 ESP32 内存)
3. 下载验证:
   - 依次尝试下载候选图片 (最多 5 个)
   - 验证 HTTP 200 响应、Content-Type 为 image/*、大小 ≤ 500KB
   - 成功则保存到临时目录 `/tmp/xiaozhi-images/`
4. 调用设备显示:
   - 构造本地 URL: `http://{serverAddress}/api/images/temp/{filename}`
   - 调用 `DeviceMcpService.callDeviceTool(deviceId, "self.screen.display_gif", Map.of("url", localUrl))`
5. 返回结果给 LLM:
   - 成功: 返回图片描述信息 (来源、尺寸、类型)
   - 失败: 返回错误原因

**依赖注入**:
- `DeviceMcpService` — 调用设备 MCP 工具
- `SessionManager` — 获取 ChatSession
- `ServerAddressProvider` — 获取后端服务地址

### 文件 2: `TempImageController.java` (新建)

**路径**: `xiaozhi-esp32-server-java/xiaozhi-dialogue/src/main/java/com/xiaozhi/communication/controller/TempImageController.java`

**职责**: 提供临时图片文件的 HTTP 访问端点

**端点**:
- `GET /api/images/temp/{filename}` → 返回图片文件
  - 从 `/tmp/xiaozhi-images/` 目录读取文件
  - 设置正确的 Content-Type (image/gif, image/jpeg, image/png)
  - 设置 Cache-Control 头避免设备缓存

**清理机制**:
- 图片文件在创建时记录时间戳
- 可选: 定时清理超过 10 分钟的临时文件 (简化方案: 不实现自动清理，依赖系统 /tmp 清理)

### ESP32 端: 无需修改

现有 `self.screen.display_gif` MCP 工具已完整支持:
- HTTP/HTTPS 下载图片
- GIF/PNG/JPG 格式识别
- Jarvis 视图叠加显示 (300×300 圆形区域, 金色边框)
- GIF 动画播放 (通过 `LvglGif` 控制器)
- 5 秒自动隐藏

## 关键设计决策

### 1. 后端下载而非设备直连百度
**原因**:
- 百度图片 URL 可能涉及重定向、Cookie 验证，ESP32 HTTP 客户端可能无法处理
- 后端可验证图片格式和大小，避免设备内存溢出
- 本地 HTTP 端点对设备更可靠 (局域网内传输，低延迟)

### 2. 使用 GlobalFunction 而非设备端 MCP 工具
**原因**:
- 百度搜索 API 需要复杂 HTTP 请求和 JSON 解析，ESP32 难以实现
- 后端有完整的 HTTP 客户端和 JSON 库
- 后端可直接调用设备 MCP `self.screen.display_gif`，复用现有设备能力

### 3. `returnDirect=false`
**原因**:
- 让 LLM 根据工具返回结果生成自然语言回复 (如 "已经为您找到并显示了猫咪的GIF")
- 而非直接返回工具执行结果

### 4. 图片大小限制 500KB
**原因**:
- ESP32 PSRAM 有限 (8MB)，但 SRAM 仅约 38KB
- 500KB GIF 足以保证动画质量，同时避免内存分配失败
- 参考现有 `self.screen.display_gif` 使用 `heap_caps_malloc(content_length, MALLOC_CAP_8BIT)`

## 验证步骤

1. **编译验证**:
   - 后端: `cd xiaozhi-esp32-server-java && mvn compile -pl xiaozhi-dialogue -am` (JDK 21)
   - ESP32: 无需重新编译 (现有固件已烧录)

2. **后端服务重启**:
   - 重启 `xiaozhi-dialogue` (8092) 进程使新工具生效

3. **功能测试**:
   - 唤醒 Jarvis
   - 说: "帮我搜索一个猫咪的GIF图片"
   - 验证: LLM 调用 `search_and_display_gif` 工具 → 设备屏幕显示 GIF
   - 说: "搜索一个搞笑的表情包"
   - 验证: 非 GIF 图片也能正常显示

4. **边界测试**:
   - 搜索不存在的关键词 → 应回复未找到
   - 搜索大量结果的关键词 → 应正确筛选并显示
   - 网络不稳定 → 应有合理的错误处理和提示