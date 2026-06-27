# 黑胶唱片音乐播放器 (Vinyl Music Player)

> 状态：开发中
> 创建：2026-06-27
> 对应分支：与 `feat(display): image_overlay_card_` 配套

## 1. 目标

通过 HTTP API 远程控制 ESP32 设备播放本地或远程音乐文件，黑胶唱片 UI 旋转展示，控件齐备。

## 2. 功能

| 功能 | 说明 |
|------|------|
| HTTP `POST /api/audio/play` | 接受 `{"path":"/sdcard/x.mp3"}` 或 `{"url":"http://...","loop":true}` |
| HTTP `POST /api/audio/control` | `{"action":"pause\|resume\|stop"}` |
| HTTP `GET /api/audio/status` | 返回当前状态、进度、文件名 |
| MCP `self.audio.play` / `self.audio.pause` / `self.audio.stop` | AI 大模型调用 |
| 异步播放 | HTTP 立即返回 loading，下载+解码在后台 |
| 黑胶唱片 UI | 仿 DebugInfoCard 300x300 浮层，33⅓ RPM 旋转 |
| 播放控制 | 播放/暂停、关闭按钮、进度条 |

## 3. 架构

```
HTTP API request
   ↓ (走现有 httpd handler)
sdcard_log_http.cc::handle_audio_play
   ↓ 投递到 AsyncAudioQueue
MusicPlayer (LVGL main thread, 单例)
   ├─ State: Idle → Loading → Playing → Paused/Stopped
   ├─ URL: HTTP GET → /sdcard/tmp/<hash>.<ext>
   ├─ File: 直接打开
   ├─ Decode: esp_audio_dec_process (mp3/wav/aac)
   └─ AudioCodec::OutputData (I2S)

MusicPlayerView (LVGL widget)
   ├─ overlay_card_ (复用 DEBUG_INFO_CARD_* 常量)
   ├─ vinyl_canvas_ (lv_canvas 预渲染)
   ├─ play_pause_btn_ / close_btn_ / progress_bar_
   └─ lv_anim_t 旋转（33⅓ RPM = 9.9°/50ms）
```

## 4. 关键设计决策

| 决策点 | 方案 | 理由 |
|--------|------|------|
| 音频格式 | MP3 / OGG / WAV 全支持 | esp_audio_codec 已在依赖中 |
| 旋转速度 | 33⅓ RPM | 用户选择真实黑胶速度 |
| MCP 工具 | 实现 | 与 self.display.* 保持一致 |
| View 位置 | 独立模块 main/display/music_player_view.cc | 解耦 |
| 异步模式 | 复用现有 queue + LVGL timer 模式 | 避免 HTTP handler 抛异常 |
| 状态广播 | MusicPlayer::SetStateListener | UI 实时同步 |

## 5. Task 拆分

| # | 状态 | 内容 | 文件 |
|---|------|------|------|
| T1 | ⏳ | MusicPlayer 核心（解码+播放+状态机） | main/audio/music_player.{h,cc} |
| T2 | ⏳ | MusicPlayerView 黑胶唱片 UI | main/display/music_player_view.{h,cc} |
| T3 | ⏳ | HTTP endpoints 注册 | main/sdcard_log_http.cc |
| T4 | ⏳ | 业务逻辑层 | main/http_api_unified.{h,cc} |
| T5 | ⏳ | MCP 工具注册 | main/mcp_server.cc |
| T6 | ⏳ | CMake 集成 | main/CMakeLists.txt |
| T7 | ⏳ | HTTP API 文档 | docs/HTTP_API.md |
| T8 | ⏳ | 编译 + 烧录 + 验证 | - |

## 6. 风险与备选

| 风险 | 备选 |
|------|------|
| esp_audio_dec 流式 API 复杂 | 走 `simple_dec/esp_wav_dec.h` 简易版本，WAV 优先 |
| SD 卡读大文件慢 | 整文件读入 SPIRAM（heap_caps_malloc MALLOC_CAP_SPIRAM） |
| HTTP 下载超时 | 5s connect, 30s receive timeout；记录进度到日志 |
| AudioCodec 忙（用户在听 TTS） | 走 AudioService::PushPacketToDecodeQueue，不直接 I2S |

## 7. 参考代码

- [AudioCodec API](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/audio/audio_codec.h)
- [image_overlay_card_ 模式](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc)
- [display_resource_from_file 异步](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/sdcard_log_http.cc)
- [esp_audio_dec API](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/managed_components/espressif__esp_audio_codec/include/decoder/esp_audio_dec.h)
