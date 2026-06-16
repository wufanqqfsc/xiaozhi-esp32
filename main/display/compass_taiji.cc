#include "compass_taiji.h"
#include "attitude_display.h"
#include <esp_log.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <esp_heap_caps.h>

static const char* TAG = "CompassTaiji";

// 静态成员初始化
lv_obj_t* CompassTaiji::taiji_container_ = nullptr;
lv_obj_t* CompassTaiji::white_circle_ = nullptr;
lv_obj_t* CompassTaiji::black_circle_ = nullptr;
lv_obj_t* CompassTaiji::white_dot_ = nullptr;
lv_obj_t* CompassTaiji::black_dot_ = nullptr;
lv_obj_t* CompassTaiji::outer_ring_ = nullptr;
lv_obj_t* CompassTaiji::outer_glow_ = nullptr;
lv_obj_t* CompassTaiji::canvas_ = nullptr;
uint32_t* CompassTaiji::canvas_buf_ = nullptr;
uint32_t* CompassTaiji::taiji_canvas_snapshot_ = nullptr;
uint32_t* CompassTaiji::study_ring_canvas_snapshot_ = nullptr;
size_t CompassTaiji::canvas_buf_bytes_ = 0;
bool CompassTaiji::canvas_snapshots_ready_ = false;
bool CompassTaiji::study_ring_mode_active_ = false;
int CompassTaiji::taiji_radius_ = 0;
int CompassTaiji::current_rotation_ = 0;

// 自动旋转：LVGL 定时器步进（避免 FreeRTOS 任务抢锁导致额外刷新）
static constexpr int kAutoRotationIntervalMs = 200;  // 步进间隔，减轻 transform 脏区刷屏

// 自动旋转控制
lv_timer_t* CompassTaiji::auto_rotation_timer_ = nullptr;
bool CompassTaiji::auto_rotation_running_ = false;
int CompassTaiji::auto_rotation_period_ms_ = 60000;
int CompassTaiji::auto_rotation_step_ = 0;
int CompassTaiji::auto_rotation_interval_ms_ = kAutoRotationIntervalMs;
volatile bool CompassTaiji::auto_rotation_paused_ = false;

/** 亚像素 AA 过渡带宽度（像素） */
static constexpr float kAaBandPx = 1.25f;

static inline float Clamp01(float v)
{
    return std::max(0.0f, std::min(1.0f, v));
}

/** 实心圆覆盖度：圆心 (cx,cy)，半径 r；边缘 ±kAaBandPx 平滑 */
static float CircleCoverage(float x, float y, float cx, float cy, float r)
{
    const float dx = x - cx;
    const float dy = y - cy;
    const float dist = std::sqrt(dx * dx + dy * dy);
    return Clamp01((r + kAaBandPx * 0.5f - dist) / kAaBandPx);
}

/** x>=0 半平面覆盖（S 曲线分界） */
static float RightHalfCoverage(float x)
{
    return Clamp01((x + kAaBandPx * 0.5f) / kAaBandPx);
}

static void BlendPixel(lv_obj_t* canvas, int px, int py, lv_color_t color, float alpha)
{
    if (alpha <= 0.003f) {
        return;
    }
    const lv_opa_t opa = static_cast<lv_opa_t>(std::min(alpha, 1.0f) * static_cast<float>(LV_OPA_COVER));
    lv_canvas_set_px(canvas, px, py, color, opa);
}

static void DrawRingAA(lv_obj_t* canvas, int cx, int cy, float radius, float width,
                       lv_color_t color, bool aa_inner_edge = true);

/**
 * 亚像素抗锯齿太极图（阴阳鱼 + 鱼眼底色 + 鎏金外圈）
 * 相对整数扫描线算法，圆弧与 S 形分界边缘更圆滑。
 */
static void ClearCanvasTransparent(lv_obj_t* canvas, int size)
{
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            lv_canvas_set_px(canvas, px, py, lv_color_black(), LV_OPA_TRANSP);
        }
    }
}

/** 学业功能区：深色圆盘 + 鎏金外圈，无阴阳鱼 */
static void DrawGoldRingOnlyAA(lv_obj_t* canvas, int center_x, int center_y, int r)
{
    const float fr = static_cast<float>(r);
    const float body_r = fr - static_cast<float>(TAIJI_GOLD_RING_WIDTH);
    const lv_color_t fill = lv_color_black();
    const lv_color_t gold = lv_color_hex(0xD4AF37);

    ClearCanvasTransparent(canvas, r * 2);

    const int bound = r + 6;
    for (int py = center_y - bound; py <= center_y + bound; ++py) {
        for (int px = center_x - bound; px <= center_x + bound; ++px) {
            const float x = static_cast<float>(px) + 0.5f - static_cast<float>(center_x);
            const float y = static_cast<float>(py) + 0.5f - static_cast<float>(center_y);
            const float dist = std::sqrt(x * x + y * y);
            if (dist <= body_r) {
                BlendPixel(canvas, px, py, fill, 1.0f);
            }
        }
    }

    DrawRingAA(canvas, center_x, center_y, fr - static_cast<float>(TAIJI_GOLD_RING_WIDTH) * 0.5f,
               static_cast<float>(TAIJI_GOLD_RING_WIDTH), gold, false);
}

static uint32_t* AllocCanvasSnapshotBuffer(size_t bytes)
{
    auto* buf = static_cast<uint32_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (buf == nullptr) {
        buf = static_cast<uint32_t*>(malloc(bytes));
    }
    return buf;
}

void CompassTaiji::BuildCanvasSnapshots(lv_obj_t* canvas, int r)
{
    if (canvas == nullptr || canvas_buf_ == nullptr || r <= 0) {
        return;
    }
    canvas_buf_bytes_ = static_cast<size_t>(r) * 2U * static_cast<size_t>(r) * 2U * sizeof(uint32_t);

    taiji_canvas_snapshot_ = AllocCanvasSnapshotBuffer(canvas_buf_bytes_);
    study_ring_canvas_snapshot_ = AllocCanvasSnapshotBuffer(canvas_buf_bytes_);
    if (taiji_canvas_snapshot_ == nullptr || study_ring_canvas_snapshot_ == nullptr) {
        ESP_LOGW(TAG, "Canvas snapshot alloc failed, study toggle will redraw");
        free(taiji_canvas_snapshot_);
        free(study_ring_canvas_snapshot_);
        taiji_canvas_snapshot_ = nullptr;
        study_ring_canvas_snapshot_ = nullptr;
        return;
    }

    memcpy(taiji_canvas_snapshot_, canvas_buf_, canvas_buf_bytes_);
    DrawGoldRingOnlyAA(canvas, r, r, r);
    memcpy(study_ring_canvas_snapshot_, canvas_buf_, canvas_buf_bytes_);
    memcpy(canvas_buf_, taiji_canvas_snapshot_, canvas_buf_bytes_);
    canvas_snapshots_ready_ = true;
    study_ring_mode_active_ = false;
    ESP_LOGI(TAG, "Canvas snapshots ready (%u bytes x2)", static_cast<unsigned>(canvas_buf_bytes_));
}

static void DrawTaijiDiagramAA(lv_obj_t* canvas, int center_x, int center_y, int r)
{
    const float fr = static_cast<float>(r);
    const float body_r = fr - static_cast<float>(TAIJI_GOLD_RING_WIDTH);
    const float half = fr * 0.5f;
    const lv_color_t white = lv_color_white();
    const lv_color_t black = lv_color_black();
    const lv_color_t gold = lv_color_hex(0xD4AF37);

    const int bound = r + 6;
    for (int py = center_y - bound; py <= center_y + bound; ++py) {
        for (int px = center_x - bound; px <= center_x + bound; ++px) {
            const float x = static_cast<float>(px) + 0.5f - static_cast<float>(center_x);
            const float y = static_cast<float>(py) + 0.5f - static_cast<float>(center_y);
            const float dist = std::sqrt(x * x + y * y);

            // 鱼体止于金圈内缘，外圈带专由鎏金环绘制，避免白鱼半透明羽化露出白毛刺
            if (dist > body_r) {
                continue;
            }
            const float fill_alpha = 1.0f;

            // 鱼眼位由 WiFi/BLE 子组件覆盖，不在 canvas 上绘制反色鱼眼点
            const float c_right = RightHalfCoverage(x);
            const float c_top_white = CircleCoverage(x, y, 0.0f, -half, half);
            const float c_bot_black = CircleCoverage(x, y, 0.0f, half, half);
            const float black_cov =
                1.0f - (1.0f - c_right * (1.0f - c_top_white)) * (1.0f - c_bot_black);

            const lv_opa_t mix = static_cast<lv_opa_t>(black_cov * static_cast<float>(LV_OPA_COVER));
            const lv_color_t body = lv_color_mix(black, white, mix);
            BlendPixel(canvas, px, py, body, fill_alpha);
        }
    }

    DrawRingAA(canvas, center_x, center_y, fr - static_cast<float>(TAIJI_GOLD_RING_WIDTH) * 0.5f,
               static_cast<float>(TAIJI_GOLD_RING_WIDTH), gold, false);
}

/** 抗锯齿圆环描边（用于鎏金外圈，边缘更圆滑） */
static void DrawRingAA(lv_obj_t* canvas, int cx, int cy, float radius, float width,
                       lv_color_t color, bool aa_inner_edge) {
    const float half = width * 0.5f;
    const float inner = radius - half;
    const float outer = radius + half;
    const int bound = (int)std::ceil(outer + 1.0f);

    for (int y = -bound; y <= bound; y++) {
        for (int x = -bound; x <= bound; x++) {
            const float dist = std::sqrt((float)(x * x + y * y));
            if (dist < inner - 1.0f || dist > outer + 1.0f) {
                continue;
            }

            float alpha = 1.0f;
            const float inner_edge = dist - inner;
            if (aa_inner_edge && inner_edge < kAaBandPx) {
                alpha = std::fmin(alpha, Clamp01(inner_edge / kAaBandPx));
            }
            const float outer_edge = outer - dist;
            if (outer_edge < kAaBandPx) {
                alpha = std::fmin(alpha, Clamp01(outer_edge / kAaBandPx));
            }
            if (alpha <= 0.0f) {
                continue;
            }

            const lv_opa_t opa = static_cast<lv_opa_t>(alpha * static_cast<float>(LV_OPA_COVER));
            lv_canvas_set_px(canvas, cx + x, cy + y, color, opa);
        }
    }
}

/** 鱼眼描边外缘 AA 宽度（像素）；向 bg 不透明混合，消除圈外黑白杂点 */
static constexpr float kFisheyeRingAaPx = 1.5f;

void CompassTaiji::PaintFisheyeDisc(lv_obj_t* canvas, int size, lv_color_t fill,
                                    lv_color_t ring, lv_color_t bg, int ring_width)
{
    if (canvas == nullptr || size <= 0 || ring_width <= 0) {
        return;
    }

    const int cx = size / 2;
    const int cy = size / 2;
    const float outer_r = static_cast<float>(size) * 0.5f;
    const float inner_r = outer_r - static_cast<float>(ring_width);

    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            const float x = static_cast<float>(px) + 0.5f - static_cast<float>(cx);
            const float y = static_cast<float>(py) + 0.5f - static_cast<float>(cy);
            const float dist = std::hypot(x, y);

            lv_color_t out = bg;
            if (dist <= inner_r) {
                out = fill;
            } else if (dist <= outer_r - kFisheyeRingAaPx) {
                out = ring;
            } else if (dist <= outer_r + kFisheyeRingAaPx * 0.5f) {
                const float ring_cov =
                    Clamp01((outer_r + kFisheyeRingAaPx * 0.5f - dist) / kFisheyeRingAaPx);
                out = lv_color_mix(ring, bg, static_cast<uint8_t>(ring_cov * 255.0f));
            }

            lv_canvas_set_px(canvas, px, py, out, LV_OPA_COVER);
        }
    }
}

/**
 * 在 (cx, cy) 为圆心创建太极图
 * @param radius 整体半径（外圆半径）
 */
void CompassTaiji::Create(lv_obj_t* parent, int cx, int cy, int radius) {
    ESP_LOGI(TAG, "Creating Taiji diagram at (%d, %d) radius=%d", cx, cy, radius);

    taiji_radius_ = radius;
    int canvas_size = radius * 2;

    // ========== 1. 容器（透明）==========
    taiji_container_ = lv_obj_create(parent);
    lv_obj_set_size(taiji_container_, canvas_size, canvas_size);
    lv_obj_set_pos(taiji_container_, cx - radius, cy - radius);
    lv_obj_set_style_radius(taiji_container_, radius, 0);
    lv_obj_set_style_bg_opa(taiji_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(taiji_container_, 0, 0);
    lv_obj_set_style_pad_all(taiji_container_, 0, 0);
    lv_obj_clear_flag(taiji_container_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_transform_pivot_x(taiji_container_, radius, 0);
    lv_obj_set_style_transform_pivot_y(taiji_container_, radius, 0);

    // ========== 2. 外圈鎏金高亮环 ==========
    // 关键修复: 移除 lv_obj 边框（会显示矩形外框）
    // 改用 canvas 直接绘制鎏金外圈, 避免矩形边框问题
    outer_ring_ = nullptr;
    outer_glow_ = nullptr;

    // ========== 3. 创建 canvas 画布 ==========
    // 关键: 使用 ARGB8888 格式, 每个像素 4 字节, 包含 alpha 通道
    // 这才能让未绘制的像素真正透明, 避免矩形边框残留
    uint32_t buf_size = canvas_size * canvas_size * sizeof(uint32_t);  // ARGB8888 = 4 bytes/pixel

    // 优先从 PSRAM 分配大块内存
    uint32_t* canvas_buf = (uint32_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!canvas_buf) {
        canvas_buf = (uint32_t*)malloc(buf_size);
    }
    if (!canvas_buf) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer (%u bytes)", buf_size);
        return;
    }
    // 初始化为透明黑色 (alpha=0 表示完全透明)
    memset(canvas_buf, 0, buf_size);

    // 创建 canvas 对象
    lv_obj_t* canvas = lv_canvas_create(taiji_container_);
    lv_canvas_set_buffer(canvas, canvas_buf, canvas_size, canvas_size, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, 0, 0);
    // 关键修复: 设置画布背景为完全透明, 防止矩形边框残留
    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_obj_set_style_outline_width(canvas, 0, 0);
    lv_obj_set_style_shadow_width(canvas, 0, 0);
    // 关键: 关闭 image recolor (防止默认色调覆盖透明像素)
    lv_obj_set_style_image_recolor_opa(canvas, LV_OPA_TRANSP, 0);
    // 旋转时启用图像抗锯齿，减轻 transform 缩放锯齿（静态 canvas，无每帧重绘开销）
    lv_image_set_antialias(canvas, true);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    canvas_ = canvas;
    canvas_buf_ = canvas_buf;

  // ========== 4. 绘制太极图（亚像素抗锯齿）==========
    DrawTaijiDiagramAA(canvas, radius, radius, radius);
    BuildCanvasSnapshots(canvas, radius);

    ESP_LOGI(TAG, "Taiji diagram created (AA %dx%d canvas)", canvas_size, canvas_size);
}

// ====================== 旋转控制 ======================
// 按键触发太极图顺时针旋转

/**
 * 顺时针旋转太极图（按键触发）
 * @param delta_angle 旋转角度（0.1°单位），正值=顺时针
 *                    例如 15° = 150
 */
void CompassTaiji::Rotate(int delta_angle) {
    SetRotation(current_rotation_ + delta_angle);
}

/**
 * 设置太极图旋转角度
 * @param angle 旋转角度（0.1°单位），0~3600 表示 0°~360°
 *              LVGL 中 3600 = 360°
 * 注意: 调用方需要持有 LVGL 锁!
 */
void CompassTaiji::SetRotation(int angle) {
    if (taiji_container_ == nullptr) {
        ESP_LOGW(TAG, "SetRotation: taiji_container_ is null, ignoring");
        return;
    }
    int normalized = angle % 3600;
    if (normalized < 0) normalized += 3600;
    if (normalized == current_rotation_) {
        return;
    }
    current_rotation_ = normalized;
    lv_obj_set_style_transform_rotation(taiji_container_, normalized, 0);
    // transform 样式变更已触发 LVGL 脏区标记，无需再 invalidate 整容器
}

/**
 * 获取当前旋转角度
 */
int CompassTaiji::GetRotation() {
    return current_rotation_;
}

/**
 * 重置旋转角度为 0
 */
void CompassTaiji::ResetRotation() {
    SetRotation(0);
}

// ====================== 自动旋转控制 ======================
// 60 秒 (60000ms) 转 360°，步进间隔 kAutoRotationIntervalMs

void CompassTaiji::OnAutoRotationTimer(lv_timer_t* timer)
{
    (void)timer;
    if (!auto_rotation_running_ || auto_rotation_paused_) {
        return;
    }
    TickAutoRotationStep();
}

/**
 * 启动自动旋转
 * @param period_ms 旋转周期 (毫秒) - 转 360° 所需时间
 *                  默认 60000ms = 60秒转一圈
 */
void CompassTaiji::StartAutoRotation(int period_ms) {
    if (auto_rotation_running_) {
        ESP_LOGW(TAG, "Auto rotation already running");
        return;
    }
    if (taiji_container_ == nullptr) {
        ESP_LOGE(TAG, "StartAutoRotation: taiji_container_ is null, ignoring");
        return;
    }

    auto_rotation_period_ms_ = period_ms;
    auto_rotation_interval_ms_ = kAutoRotationIntervalMs;
    RecalcAutoRotationStep();

    if (auto_rotation_step_ <= 0) {
        ESP_LOGE(TAG, "Invalid auto rotation step: %d (period=%dms)", auto_rotation_step_, period_ms);
        return;
    }

    auto_rotation_running_ = true;
    auto_rotation_paused_ = false;

    if (auto_rotation_timer_ != nullptr) {
        lv_timer_delete(auto_rotation_timer_);
        auto_rotation_timer_ = nullptr;
    }
    auto_rotation_timer_ = lv_timer_create(OnAutoRotationTimer, auto_rotation_interval_ms_, nullptr);

    ESP_LOGI(TAG, "Auto rotation started: period=%dms, step=%.1f°/step, interval=%dms",
             auto_rotation_period_ms_, auto_rotation_step_ / 10.0, auto_rotation_interval_ms_);
}

void CompassTaiji::RecalcAutoRotationStep()
{
    const int steps = auto_rotation_period_ms_ / auto_rotation_interval_ms_;
    auto_rotation_step_ = (steps > 0) ? (3600 / steps) : auto_rotation_step_;
}

void CompassTaiji::SetAutoRotationPeriod(int period_ms)
{
    if (period_ms < 500) {
        period_ms = 500;
    }
    auto_rotation_period_ms_ = period_ms;
    RecalcAutoRotationStep();
}

int CompassTaiji::GetAutoRotationPeriod()
{
    return auto_rotation_period_ms_;
}

void CompassTaiji::SetAutoRotationPaused(bool paused)
{
    auto_rotation_paused_ = paused;
}

void CompassTaiji::TickAutoRotationStep()
{
    if (taiji_container_ == nullptr || auto_rotation_step_ <= 0) {
        return;
    }
    Rotate(auto_rotation_step_);
}

/**
 * 停止自动旋转
 */
void CompassTaiji::StopAutoRotation() {
    if (!auto_rotation_running_) {
        return;
    }
    auto_rotation_running_ = false;
    if (auto_rotation_timer_ != nullptr) {
        lv_timer_delete(auto_rotation_timer_);
        auto_rotation_timer_ = nullptr;
    }
    ESP_LOGI(TAG, "Auto rotation stopped");
}

/**
 * 检查是否在自动旋转中
 */
bool CompassTaiji::IsAutoRotating() {
    return auto_rotation_running_;
}

lv_obj_t* CompassTaiji::GetContainer() {
    return taiji_container_;
}

lv_obj_t* CompassTaiji::GetCanvas() {
    return canvas_;
}

int CompassTaiji::GetRadius() {
    return taiji_radius_;
}

void CompassTaiji::SetStudyRingMode(bool ring_only)
{
    if (canvas_ == nullptr || taiji_radius_ <= 0 || canvas_buf_ == nullptr) {
        return;
    }
    if (ring_only == study_ring_mode_active_) {
        return;
    }

    if (canvas_snapshots_ready_) {
        uint32_t* src = ring_only ? study_ring_canvas_snapshot_ : taiji_canvas_snapshot_;
        if (src != nullptr) {
            memcpy(canvas_buf_, src, canvas_buf_bytes_);
            study_ring_mode_active_ = ring_only;
            lv_obj_invalidate(canvas_);
            return;
        }
    }

    const int r = taiji_radius_;
    if (ring_only) {
        DrawGoldRingOnlyAA(canvas_, r, r, r);
    } else {
        ClearCanvasTransparent(canvas_, r * 2);
        DrawTaijiDiagramAA(canvas_, r, r, r);
    }
    study_ring_mode_active_ = ring_only;
    lv_obj_invalidate(canvas_);
}
