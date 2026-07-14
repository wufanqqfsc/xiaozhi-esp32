# GIF 图片显示：现有组件扫描与整合方案

> 分析时间：2026-07-14  
> 目标板：Waveshare ESP32-S3-Touch-LCD-1.85B（360×360）  
> 关联文档：[ui_voice_interaction_memory_analysis_20260714.md](ui_voice_interaction_memory_analysis_20260714.md)、[plan_baidu_gif_search_display.md](plan_baidu_gif_search_display.md)

---

## 一、组件地图

```
                    ┌─────────────────────────────────────┐
                    │     gifdec.c/h（底层解码器）          │
                    │  gd_open_gif_data / gd_get_frame    │
                    └──────────────┬──────────────────────┘
                                   │
                    ┌──────────────▼──────────────────────┐
                    │     LvglGif（唯一动画控制器）         │
                    │  lv_timer + canvas ARGB8888         │
                    └──────────────┬──────────────────────┘
           ┌───────────────────────┼───────────────────────┐
           │                       │                       │
    LcdDisplay              AttitudeDisplay         FortuneWatchfaceView
    (表情 GIF)              (罗盘预览)              (JARVIS 叠加层)
    unique_ptr              raw ptr 同名成员         raw ptr
```

| 层级 | 文件 | 职责 |
|------|------|------|
| **解码内核** | `main/display/lvgl_display/gif/gifdec.{c,h}` | LZW 解码、帧渲染、`canvas` 分配（`lv_malloc(sizeof(gd_GIF) + 5×W×H)`） |
| **动画封装** | `main/display/lvgl_display/gif/lvgl_gif.{cc,h}` | `LvglGif`：定时器驱动、`SetFrameCallback`、循环控制 |
| **图像包装** | `main/display/lvgl_display/lvgl_image.{cc,h}` | `LvglAllocatedImage` / `LvglRawImage` / `LvglSdCardImage`，`IsGif()` 魔数检测 |
| **基类 Display** | `main/display/display.{cc,h}` | `SetPreviewGif()` 默认 no-op |
| **通用 LCD** | `main/display/lcd_display.{cc,h}` | 表情 GIF（`SetEmotion`），`unique_ptr<LvglGif>` |
| **罗盘 UI** | `main/display/attitude_display.{cc,h}` | 3 条 GIF 路径 + `ShowImageOnActiveView` 路由 |
| **JARVIS HUD** | `main/display/fortune_watchface_view.{cc,h}` | `ShowImage()` / `HideImage()` |
| **MCP 入口** | `main/mcp_server.cc` | `self.screen.display_gif`（URL 下载） |
| **HTTP 入口** | `main/sdcard_log_http.cc` | SD 卡 display API，**两套 GIF 逻辑** |
| **HTTP 入口** | `main/http_api_unified.cc` | 资源显示 API，走 `ShowImageOnActiveView` |

---

## 二、各实现路径详情

### 路径 1：`LvglGif` 完整动画（正确实现）

| 调用方 | 入口 | 内存 | 动画 | FrameCallback |
|--------|------|------|------|---------------|
| `LcdDisplay::SetEmotion` | 表情名 → emoji 集合 | 资产内嵌 bytes | ✅ | ✅ `lv_image_set_src` |
| `AttitudeDisplay::SetPreviewGif` | SD 卡文件路径 | `malloc(fsize)` ⚠️ 内部 SRAM | ✅ | ✅ `preview_gif_` |
| `FortuneWatchfaceView::ShowImage` | `IsGif()==true` | `image_cache_` 持有原始 bytes | ⚠️ 部分 | ❌ **未设置** |

`SetPreviewGif` 是罗盘侧唯一完整动画路径，但与 MCP/HTTP 主路径脱节。

### 路径 2：`SetPreviewImage` 静态首帧（动画被禁用）

`main/display/attitude_display.cc` 中 `SetPreviewImage` / `SetPreviewImageUnlocked`：

```cpp
if (is_gif) {
    // GIF：改用 lv_image widget 显示第一帧（LVGL 9.x 已移除 lv_gif，暂不支持动画）
    lv_image_set_src(preview_gif_, img_dsc);
}
```

`ShowImageOnActiveView` 在非 JARVIS 时走 `SetPreviewImageUnlocked`，**MCP `display_gif` 在罗盘视图下只显示静态首帧**。

### 路径 3：`sdcard_log_http` 直接调 `gifdec`（重复实现）

| 分支 | 条件 | 行为 | 内存 |
|------|------|------|------|
| A | `loop=true` | `LvglAllocatedImage` → `ShowImageOnActiveView` | 原始 GIF（PSRAM）+ `LvglGif` 解码缓冲 |
| B | `loop=false` | 手动 `gd_open_gif_data` → 拷第一帧 ARGB8888 | 原始 GIF + canvas 副本，**丢弃动画** |

路径 B 与 `LvglGif` 功能重叠，且多占一份 `W×H×4` canvas。

### 路径 4：数据入口（下载/读取）

| 入口 | 分配方式 | 上限 | 后续 |
|------|----------|------|------|
| `mcp_server display_gif` | `heap_caps_malloc(..., MALLOC_CAP_8BIT)` | 无硬限 | 可能落内部 SRAM |
| `sdcard_log_http` loop | `MALLOC_CAP_SPIRAM` 优先 | 512KB | ✅ |
| `http_api_unified` | PSRAM 优先 | 文件大小 | ✅ |
| `SetPreviewGif` | `malloc` | 无硬限 | ❌ 内部 SRAM |

---

## 三、内存占用模型（单张 GIF 显示时）

以 200×200 GIF、文件 80KB 为例：

| 阶段 | 占用 | 位置 |
|------|------|------|
| 原始 GIF bytes | 80 KB | PSRAM（MCP/HTTP）或 SRAM（`SetPreviewGif`/MCP） |
| `gd_GIF` 结构 + canvas + frame + LZW | ~80 + 5×200×200 ≈ **280 KB** | `lv_malloc`（通常堆） |
| 路径 B 静态帧副本 | 200×200×4 = **160 KB** | PSRAM（额外一份） |
| LVGL image cache | 解码后可能再缓存 | PSRAM 2MB 池 |
| **峰值（路径 B）** | 80 + 280 + 160 ≈ **520 KB** | 重复严重 |
| **峰值（整合后理想）** | 80 + 280 ≈ **360 KB** | 单份 canvas |

### 多个 `gif_controller_` 并存

| 位置 | 类型 | 说明 |
|------|------|------|
| `LcdDisplay` | `std::unique_ptr<LvglGif>` | 表情 GIF 专用 |
| `AttitudeDisplay` | `LvglGif*` raw | **遮蔽**父类同名成员；`SetEmotion` 已 no-op |
| `FortuneWatchfaceView` | `LvglGif*` raw | JARVIS 叠加层独立持有 |

JARVIS 与罗盘各持一份，**无法互斥**，切换视图时若未 `HideImage` 可能重叠存活。

---

## 四、问题汇总

| # | 问题 | 影响 |
|---|------|------|
| 1 | **4 条显示路径**（`SetPreviewGif` / `SetPreviewImage` / `FortuneWatchface::ShowImage` / `sdcard_log_http` 直解） | 行为不一致、难维护 |
| 2 | **MCP 主路径在罗盘视图 GIF 不动画** | 用户语音搜 GIF 体验差 |
| 3 | **`sdcard_log_http` loop=false 重复解码** | 多占 1 份 canvas，代码重复 |
| 4 | **分配策略不统一**（`malloc` vs `MALLOC_CAP_SPIRAM` vs `MALLOC_CAP_8BIT`） | 挤占内部 SRAM |
| 5 | **双 `gif_controller_`（Attitude + FortuneWatchface）** | 切换视图可能泄漏/重叠 |
| 6 | **JARVIS `ShowImage` 缺 `SetFrameCallback`** | 动画可能不刷新 |
| 7 | **`LvglGif` 不拷贝原始数据**，依赖 `image_cache_` 常驻 | 设计正确但各路径生命周期不统一 |
| 8 | **无全局「正在播放」互斥** | 占卜 + GIF + 表情 GIF 可叠加 |

---

## 五、整合方案（推荐）

### 5.1 目标架构

```
所有入口 ──► AttitudeDisplay::ShowImageOnActiveView()
                    │
                    ▼
            GifPreviewPlayer（新建，单例或 AttitudeDisplay 成员）
              - 唯一 LvglGif*
              - 唯一 image_cache_ (LvglAllocatedImage)
              - 唯一 lv_timer hide_timer
              - 绑定目标 lv_obj_t*（罗盘 preview_gif_ 或 JARVIS image_widget_）
                    │
                    ▼
              LvglGif + gifdec（不动）
```

### 5.2 Phase 1：统一播放内核（P0，改动小）

1. **新增 `GifPreviewPlayer` 类**（建议路径：`main/display/lvgl_display/gif/gif_preview_player.h`）：

```cpp
class GifPreviewPlayer {
public:
    bool Show(lv_obj_t* widget, std::unique_ptr<LvglImage> image,
              uint32_t timeout_ms, bool loop = true);
    void Hide();
    bool IsActive() const;
private:
    std::unique_ptr<LvglImage> image_cache_;
    std::unique_ptr<LvglGif> gif_controller_;
    lv_timer_t* hide_timer_ = nullptr;
    lv_obj_t* widget_ = nullptr;
};
```

封装：加载、`SetFrameCallback`、`Start/Stop`、超时隐藏、资源释放；**全局只允许一个实例活跃**。

2. **`AttitudeDisplay::ShowImageOnActiveView` 改为唯一入口**：
   - JARVIS 可见 → `player.Show(jarvis_image_widget_, ...)`
   - 否则 → `player.Show(preview_gif_, ...)`
   - 删除 `SetPreviewGif` / `SetPreviewImage` 中的 GIF 分支逻辑

3. **`FortuneWatchfaceView::ShowImage` 删除自有 `gif_controller_`**，改为由 `AttitudeDisplay` 统一管理 overlay，或把 overlay 管理权上收至 `AttitudeDisplay`。

### 5.3 Phase 2：统一数据加载（P0）

4. **新增 `LoadImageFromBuffer(uint8_t* data, size_t len)` 工厂**：
   - 强制 `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
   - 失败再 fallback internal
   - `mcp_server`、`sdcard_log_http`、`http_api_unified`、`SetPreviewGif` 全部走此工厂

5. **删除 `sdcard_log_http` 中 `gd_open_gif_data` 直解分支**（约 2213–2258 行），`loop=false` 时仍走 `LvglGif` 但 `SetLoopCount(1)`。

### 5.4 Phase 3：清理冗余（P1）

6. **废弃 `SetPreviewGif(file_path)`**，改为 `LoadFromSdCard(path) → ShowImageOnActiveView()`。

7. **移除 `AttitudeDisplay::gif_controller_`**，只保留 `GifPreviewPlayer` 一份。

8. **`LcdDisplay::gif_controller_`** 保留（表情专用），但与 `GifPreviewPlayer` 互斥：

```cpp
if (GifPreviewPlayer::IsActive()) GifPreviewPlayer::Hide();
```

### 5.5 Phase 4：内存优化（P1）

| 优化项 | 做法 | 节省 |
|--------|------|------|
| 取消静态首帧路径 | 删除路径 B canvas 拷贝 | ~W×H×4 |
| 统一 PSRAM 分配 | 所有 GIF raw bytes 走 SPIRAM | 保护内部 SRAM |
| 显示前 `lv_image_cache_drop` | MCP 路径全覆盖 | 减少 2MB 池压力 |
| 限制文件大小 | MCP 也加 512KB 上限（与 HTTP 一致） | 防 OOM |
| 隐藏时立即释放 | 统一在 `GifPreviewPlayer::Hide()` 中 `image_cache_.reset()` + `StopGif` | 及时释放 |

---

## 六、整合后的调用链（目标态）

```
用户/MCP/HTTP
    │
    ├─ MCP self.screen.display_gif
    ├─ HTTP /api/sdcard/display
    └─ HTTP http_api_display_resource
            │
            ▼
    LoadImageBuffer (PSRAM, ≤512KB)
            │
            ▼
    AttitudeDisplay::ShowImageOnActiveView(image, timeout)
            │
            ▼
    GifPreviewPlayer::Show(target_widget, image, timeout, loop)
            │
            ├─ IsGif? → LvglGif + FrameCallback + lv_timer
            └─ else   → lv_image_set_src (PNG/JPG decoder chain)
            │
            ▼
    timeout → Hide() → 释放 cache + gif_controller + 恢复 UI
```

---

## 七、实施优先级

| 优先级 | 任务 | 预期收益 |
|--------|------|----------|
| **P0** | 提取 `GifPreviewPlayer`，`ShowImageOnActiveView` 统一 GIF 动画 | MCP GIF 在罗盘可动；消除双控制器 |
| **P0** | MCP 下载改 PSRAM + 512KB 限制 | 保护内部 SRAM |
| **P0** | `FortuneWatchfaceView` 补 `SetFrameCallback`（或并入 Player） | 修复 JARVIS GIF 不刷新 |
| **P1** | 删除 `sdcard_log_http` 直解 gifdec 分支 | 减 ~160KB 峰值 + 减重复代码 |
| **P1** | 废弃 `SetPreviewGif` 独立路径 | 减一条维护分支 |
| **P2** | `SetPreviewImage` GIF 分支删除，全走 Player | 代码收敛 |
| **P2** | 与占卜/TTS 互斥（显示 GIF 时暂停跑马灯 timer） | 减 LVGL 竞争 |

---

## 八、结论

当前 GIF 显示**底层只有一套**（`gifdec` + `LvglGif`），但**上层有 4 条分叉路径**，导致：

- 同一 MCP 工具在 JARVIS / 罗盘下行为不同（动画 vs 静帧）
- `sdcard_log_http` 重复实现首帧解码，多占一份 canvas
- `AttitudeDisplay` 与 `FortuneWatchfaceView` 各持 `gif_controller_`，内存与生命周期无法统一
- 分配策略混用 `malloc` / `MALLOC_CAP_8BIT` / PSRAM，加剧内部 SRAM 压力

**最小整合方案**：新增 `GifPreviewPlayer` 单例，所有入口收敛到 `ShowImageOnActiveView`，删除 `sdcard_log_http` 直解分支和 `SetPreviewGif` 独立逻辑。预计可减少 **1 份 canvas 拷贝 + 1 个冗余控制器**，并统一 MCP/HTTP/SD 卡 GIF 的动画行为。

---

## 九、相关文件索引

| 功能 | 路径 |
|------|------|
| GIF 解码内核 | `main/display/lvgl_display/gif/gifdec.{c,h}` |
| GIF 动画控制器 | `main/display/lvgl_display/gif/lvgl_gif.{cc,h}` |
| 图像包装类 | `main/display/lvgl_display/lvgl_image.{cc,h}` |
| 罗盘预览 / 路由 | `main/display/attitude_display.{cc,h}` |
| JARVIS 图片叠加 | `main/display/fortune_watchface_view.{cc,h}` |
| MCP display_gif | `main/mcp_server.cc` |
| SD 卡 display API | `main/sdcard_log_http.cc` |
| HTTP 资源显示 | `main/http_api_unified.cc` |
| 百度 GIF 搜索计划 | `doc/plan_baidu_gif_search_display.md` |
| 内存分析 | `doc/ui_voice_interaction_memory_analysis_20260714.md` |
| Jarvis 交互计划 | `.trae/documents/jarvis_interaction_plan.md` |
