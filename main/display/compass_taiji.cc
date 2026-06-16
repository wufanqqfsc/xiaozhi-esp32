#include "compass_taiji.h"
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

/**
 * 填充圆 (中点圆算法 + 水平扫描线)
 * 在 (cx, cy) 为中心，半径 r 的圆内用 color 填充
 */
static inline void FillCircle(lv_obj_t* canvas, int cx, int cy, int r,
                              lv_color_t color) {
    int r_sq = r * r;
    for (int y = -r; y <= r; y++) {
        int dy_sq = y * y;
        int x_max = (int)std::sqrt((float)(r_sq - dy_sq));
        for (int x = -x_max; x <= x_max; x++) {
            lv_canvas_set_px(canvas, cx + x, cy + y, color, LV_OPA_COVER);
        }
    }
}

/** 抗锯齿圆环描边（用于鎏金外圈，边缘更圆滑） */
static void DrawRingAA(lv_obj_t* canvas, int cx, int cy, float radius, float width,
                       lv_color_t color) {
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
            if (inner_edge < 1.0f) {
                alpha = std::fmin(alpha, std::fmax(0.0f, inner_edge));
            }
            const float outer_edge = outer - dist;
            if (outer_edge < 1.0f) {
                alpha = std::fmin(alpha, std::fmax(0.0f, outer_edge));
            }
            if (alpha <= 0.0f) {
                continue;
            }

            const lv_opa_t opa = static_cast<lv_opa_t>(alpha * static_cast<float>(LV_OPA_COVER));
            lv_canvas_set_px(canvas, cx + x, cy + y, color, opa);
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
    // 旋转时关闭抗锯齿，降低 ARGB canvas 每帧绘制开销
    lv_image_set_antialias(canvas, false);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    canvas_ = canvas;

    // ========== 4. 绘制太极图 ==========
    lv_color_t white = lv_color_white();
    lv_color_t black = lv_color_black();

    int r = radius;
    int center_x = r;  // 画布中心
    int center_y = r;

    int half_r = r / 2;
    int dot_r = r / 6;       // 与 FISHEYE_ICON_SIZE/2 一致（radius=108 时 dot_r=18）

    // 步骤 1~4: 阴阳鱼本体（鱼眼圆点由 WiFi/BLE 图标控件替代，不在 canvas 上绘制）

    // 步骤 1: 填充白色大圆 (阳鱼)
    FillCircle(canvas, center_x, center_y, r, white);

    // 步骤 2: 填充黑色右半圆 (阴鱼) - x > center_x 部分
    for (int y = -r; y <= r; y++) {
        int dy_sq = y * y;
        int x_max = (int)std::sqrt((float)(r * r - dy_sq));
        for (int x = 0; x <= x_max; x++) {  // x >= 0
            lv_canvas_set_px(canvas, center_x + x, center_y + y, black, LV_OPA_COVER);
        }
    }

    // 步骤 3: 填充上半小圆(白色) - 阴中有阳点
    // 在阴鱼（黑色右半）的上半添加白色小圆
    for (int y = -half_r; y <= half_r; y++) {
        int dy_sq = y * y;
        int x_max = (int)std::sqrt((float)(half_r * half_r - dy_sq));
        for (int x = -x_max; x <= x_max; x++) {
            int px = center_x + x;
            int py = center_y - half_r + y;
            // 需要在大圆内
            int big_dx = x;
            int big_dy = y - half_r;
            if (big_dx * big_dx + big_dy * big_dy <= r * r) {
                lv_canvas_set_px(canvas, px, py, white, LV_OPA_COVER);
            }
        }
    }

    // 步骤 4: 填充下半小圆(黑色) - 阳中有阴点
    // 在阳鱼（白色左半）的下半添加黑色小圆
    for (int y = -half_r; y <= half_r; y++) {
        int dy_sq = y * y;
        int x_max = (int)std::sqrt((float)(half_r * half_r - dy_sq));
        for (int x = -x_max; x <= x_max; x++) {
            int px = center_x + x;
            int py = center_y + half_r + y;
            // 需要在大圆内
            int big_dx = x;
            int big_dy = y + half_r;
            if (big_dx * big_dx + big_dy * big_dy <= r * r) {
                lv_canvas_set_px(canvas, px, py, black, LV_OPA_COVER);
            }
        }
    }

    // 步骤 5~6: 鱼眼底色 baked 进 canvas（与太极一体旋转，避免独立白底控件触发分块白闪）
    const int top_eye_y = center_y - half_r;
    const int bot_eye_y = center_y + half_r;
    FillCircle(canvas, center_x, top_eye_y, dot_r, lv_color_hex(0x505050));
    FillCircle(canvas, center_x, bot_eye_y, dot_r, white);

    // 步骤 7: 鎏金外圈（4px 抗锯齿）
    constexpr float kTaijiGoldRingWidth = 4.0f;
    lv_color_t gold = lv_color_hex(0xD4AF37);
    DrawRingAA(canvas, center_x, center_y, (float)r - 1.0f, kTaijiGoldRingWidth, gold);

    ESP_LOGI(TAG, "Taiji diagram created successfully (%dx%d canvas)", canvas_size, canvas_size);
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

int CompassTaiji::GetRadius() {
    return taiji_radius_;
}
