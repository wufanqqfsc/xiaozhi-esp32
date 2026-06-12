#include "attitude_display.h"
#include "lvgl_theme.h"
#include "application.h"
#include "board.h"
#include "compass_taiji.h"
#include <esp_log.h>
#include <cstdio>
#include <inttypes.h>
#include <ctime>
#include <cmath>
#include <font_awesome.h>

#define TAG "AttitudeDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);

// 构造函数：调用 SpiLcdDisplay 基类构造，它会自动初始化 LVGL
AttitudeDisplay::AttitudeDisplay(esp_lcd_panel_io_handle_t panel_io,
                                 esp_lcd_panel_handle_t panel,
                                 int width, int height,
                                 int offset_x, int offset_y,
                                 bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy)
{
    ESP_LOGI(TAG, "AttitudeDisplay constructed, %dx%d", width, height);
}

void AttitudeDisplay::SetupUI()
{
    // 1. 防止重复调用
    if (IsSetupUICalled()) {
        ESP_LOGW(TAG, "SetupUI() already called, skipping");
        return;
    }

    // 2. 调用基类标记 SetupUI 被调用
    Display::SetupUI();

    // 3. 获取 LVGL 锁（线程安全）
    DisplayLockGuard lock(this);

    // 4. 获取主题
    auto lvgl_theme = static_cast<LvglTheme*>(GetTheme());
    if (lvgl_theme == nullptr) {
        ESP_LOGE(TAG, "Theme is null!");
        return;
    }

    // 5. 获取当前主题色值
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();

    // 6. 设置屏幕基础样式
    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, lvgl_theme->text_font()->font(), 0);
    lv_obj_set_style_text_color(screen, theme_colors.text_main, 0);
    lv_obj_set_style_bg_color(screen, theme_colors.bg_outer, 0);

    // 7. 创建圆形屏幕遮罩（360×360圆形屏幕适配）
    // 原理：绘制一个背景色大圆覆盖整个屏幕，遮住四角，让整个显示呈现圆形
    lv_obj_t* round_mask = lv_obj_create(screen);
    lv_obj_set_size(round_mask, 360, 360);
    lv_obj_set_pos(round_mask, 0, 0);
    lv_obj_set_style_radius(round_mask, 180, 0);
    lv_obj_set_style_bg_color(round_mask, theme_colors.bg_outer, 0);
    lv_obj_set_style_border_width(round_mask, 0, 0);
    lv_obj_set_style_clip_corner(round_mask, true, 0);
    lv_obj_move_background(round_mask);

    // 8. 创建主容器（360×360，覆盖整个屏幕）
    attitude_container_ = lv_obj_create(screen);
    lv_obj_set_size(attitude_container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(attitude_container_, 0, 0);
    lv_obj_set_style_border_width(attitude_container_, 0, 0);
    lv_obj_set_style_pad_all(attitude_container_, 0, 0);
    lv_obj_set_style_bg_color(attitude_container_, theme_colors.bg_outer, 0);
    lv_obj_set_style_clip_corner(attitude_container_, false, 0);

    // ============ 4层同心圆布局 ============
    // 按设计文档规范，从内向外、从小到大创建

    // 阶段0: 玄黑径向渐变背景（底层，不属于4层）
    CreateBackground();

    // 层级0: 中心太极图（迭代13，target.png 中心元素）
    CreateLayer0Taiji();

    // 层级一: 核心信息区 (0~54px 半径范围)
    CreateLayer1CoreInfo();

    // 层级二: 动态指示区 (54~90px 半径范围)
    CreateLayer2DynamicIndicator();

    // 层级三: 状态进度区 (90~144px 半径范围)
    CreateLayer3StatusProgress();

    // 层级四: 边界留白区 (144~178px 半径范围)
    CreateLayer4Boundary();

    // 方位圆点（设计文档第5节：4个绝对方位的实心圆点）
    CreateCompassPoints();

    ESP_LOGI(TAG, "SetupUI completed with theme: %s (4-layer concentric layout)",
             AttitudeTheme::GetInstance().GetThemeName());
}

// 层级0: 中心太极图（迭代13，target.png 核心视觉）
// 创建一个直径 160px 的太极图（半径 80px）
void AttitudeDisplay::CreateLayer0Taiji()
{
    const int CENTER_X = 180;
    const int CENTER_Y = 180;
    const int TAIJI_RADIUS = 80;  // 太极图半径

    ESP_LOGI(TAG, "Creating Layer0 Taiji diagram (radius=%d)", TAIJI_RADIUS);

    CompassTaiji::Create(attitude_container_, CENTER_X, CENTER_Y, TAIJI_RADIUS);
}

// 浅灰渐变背景（主题色值驱动）
void AttitudeDisplay::CreateBackground()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();

    // 创建背景容器（全屏）
    background_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(background_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(background_, 0, 0);
    lv_obj_set_style_border_width(background_, 0, 0);
    lv_obj_set_style_pad_all(background_, 0, 0);
    lv_obj_set_style_bg_color(background_, theme_colors.bg_outer, 0);
    lv_obj_set_style_bg_opa(background_, LV_OPA_100, 0);

    // 创建中心微亮层（模拟径向渐变）
    bg_layer_center_ = lv_obj_create(background_);
    lv_obj_set_size(bg_layer_center_, 300, 300);  // 直径 300
    lv_obj_set_style_radius(bg_layer_center_, 150, 0);  // 圆形
    lv_obj_set_style_border_width(bg_layer_center_, 0, 0);
    lv_obj_set_style_bg_color(bg_layer_center_, theme_colors.bg_inner, 0);
    lv_obj_set_style_bg_opa(bg_layer_center_, LV_OPA_100, 0);
    lv_obj_center(bg_layer_center_);

    bg_inner_glow_ = nullptr;

    // 将背景层移到最底层
    lv_obj_move_background(background_);

    ESP_LOGI(TAG, "Background created with theme colors");
}

// 重写 UpdateStatusBar (已废弃: 4层布局无顶部信息栏)
// 保留空实现以保持 ABI 兼容
void AttitudeDisplay::UpdateStatusBar(bool update_all)
{
    // 4层同心圆布局不再有顶部信息栏，时间/网络/电量等状态通过其他渠道展示
    // 后续可在层级三的 progress_arc_ 旁边添加状态指示
}

// 设置姿态数据 (后续迭代会在这里更新 UI)
void AttitudeDisplay::SetAttitudeData(float pitch, float roll, float yaw)
{
    current_pitch_ = pitch;
    current_roll_ = roll;
    current_yaw_ = yaw;
    // 后续迭代会在层级二（动态指示区）实现姿态指示
}

// 设置解读文字 (已废弃: 迭代14清理)
// 保留空实现以保持 API 兼容
void AttitudeDisplay::SetInterpretation(const std::string& text)
{
    // 迭代14清理: layer3_state_label_ 已被移除
    // 4层布局不再有解读区域
    // 该 API 保留以保持向后兼容, 但不再生效
    (void)text;  // 防止未使用警告
}

void AttitudeDisplay::SetTheme(Theme* theme)
{
    Display::SetTheme(theme);
    ApplyThemeToAttitudeUI();
}

void AttitudeDisplay::ApplyThemeToAttitudeUI()
{
    ApplyCurrentTheme();
}

// 主题切换接口（公开API）
void AttitudeDisplay::SwitchTheme(AttitudeThemeType theme)
{
    AttitudeTheme::GetInstance().SetTheme(theme);
    ApplyCurrentTheme();
}

// 应用当前主题到4层同心圆布局的所有元素
void AttitudeDisplay::ApplyCurrentTheme()
{
    DisplayLockGuard lock(this);
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();

    // 1. 背景层
    if (background_ != nullptr) {
        lv_obj_set_style_bg_color(background_, theme_colors.bg_outer, 0);
    }
    if (bg_layer_center_ != nullptr) {
        lv_obj_set_style_bg_color(bg_layer_center_, theme_colors.bg_inner, 0);
    }

    // 2. 层级一: 核心信息区 (迭代14清理, 文本已移除, 容器仍存在)

    // 3. 层级二: 动态指示区
    if (layer2_inner_ring_ != nullptr) {
        lv_obj_set_style_arc_color(layer2_inner_ring_, theme_colors.border_line, 0);
    }
    // layer2_indicator_line_ 已废弃（迭代14清理）

    // 4. 层级三: 状态进度区
    if (layer3_bg_arc_ != nullptr) {
        lv_obj_set_style_arc_color(layer3_bg_arc_, theme_colors.card_bg, 0);
    }
    if (layer3_progress_arc_ != nullptr) {
        lv_color_t state_color = AttitudeTheme::GetInstance().GetStateColor(current_state_level_);
        lv_obj_set_style_arc_color(layer3_progress_arc_, state_color, LV_PART_INDICATOR);
    }
    // layer3_state_label_ 已废弃（迭代14清理）

    // 5. 层级四: 边界留白区
    if (layer4_outer_ring_ != nullptr) {
        lv_obj_set_style_border_color(layer4_outer_ring_, theme_colors.border_line, 0);
    }

    // 6. 方位圆点
    auto apply_point_color = [&](lv_obj_t* obj) {
        if (obj != nullptr) {
            lv_obj_set_style_bg_color(obj, theme_colors.point_default, 0);
        }
    };
    apply_point_color(dir_n_label_);
    apply_point_color(dir_e_label_);
    apply_point_color(dir_s_label_);
    apply_point_color(dir_w_label_);

    ESP_LOGI(TAG, "Theme applied to 4-layer layout: %s", AttitudeTheme::GetInstance().GetThemeName());
}

// ====================== 4层同心圆布局实现 ======================

// 层级一: 核心信息区 (0~54px 半径范围)
void AttitudeDisplay::CreateLayer1CoreInfo()
{
    // 迭代14清理: 移除所有核心信息区文本
    // 原 "姿态平衡仪" / "Balance OK" / "0.0°" 文字已废弃
    // 中心 0~54px 区域由太极图（迭代13）取代

    // 保留 layer1_container_ 引用, 但不创建任何文本
    // 容器可以保留用于后续添加 64 卦符号的占位
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    layer1_container_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(layer1_container_, 108, 108);
    lv_obj_set_pos(layer1_container_, CENTER_X - 54, CENTER_Y - 54);
    lv_obj_set_style_radius(layer1_container_, 54, 0);
    lv_obj_set_style_bg_opa(layer1_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(layer1_container_, 0, 0);
    lv_obj_set_style_pad_all(layer1_container_, 0, 0);
    lv_obj_clear_flag(layer1_container_, LV_OBJ_FLAG_CLICKABLE);


    ESP_LOGI(TAG, "Layer1 CoreInfo created (0~54px)");
}

// 层级二: 动态指示区 (54~90px 半径范围)
void AttitudeDisplay::CreateLayer2DynamicIndicator()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    // 内圈装饰细线 (lv_arc 模拟圆环) - 直径 160, 半径 80px
    layer2_inner_ring_ = lv_arc_create(attitude_container_);
    lv_obj_set_size(layer2_inner_ring_, 160, 160);
    lv_obj_set_pos(layer2_inner_ring_, CENTER_X - 80, CENTER_Y - 80);
    lv_arc_set_range(layer2_inner_ring_, 0, 360);
    lv_arc_set_value(layer2_inner_ring_, 360);
    lv_arc_set_bg_angles(layer2_inner_ring_, 0, 360);
    lv_arc_set_angles(layer2_inner_ring_, 0, 360);
    // 关键修复: 同时设置 part 0 (背景) 和 INDICATOR 的颜色, 防止默认蓝色
    lv_obj_set_style_arc_width(layer2_inner_ring_, 1, 0);
    lv_obj_set_style_arc_color(layer2_inner_ring_, theme_colors.border_line, 0);
    lv_obj_set_style_arc_width(layer2_inner_ring_, 1, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(layer2_inner_ring_, theme_colors.border_line, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(layer2_inner_ring_, LV_OPA_50, 0);
    // 隐藏 knob 控件（不是用 remove_style）
    lv_obj_set_style_opa(layer2_inner_ring_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(layer2_inner_ring_, LV_OBJ_FLAG_CLICKABLE);

    // 迭代14清理: 移除中心角度指示线 (lv_line)
    // 原参考线 (从圆心向上, 半径54~90px) 已废弃
    // 该区域未来由 64 卦符号层填充

    ESP_LOGI(TAG, "Layer2 DynamicIndicator created (54~90px, indicator line removed)");
}

// 层级三: 状态进度区 (90~144px 半径范围)
void AttitudeDisplay::CreateLayer3StatusProgress()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    // 迭代14清理: 移除 "BALANCE" 状态文字
    // 原 lv_label "BALANCE" 已废弃
    // 该区域未来由 12 地支层填充

    // 背景环 (lv_arc, 直径 260, 半径 130px, 颜色=card_bg)
    // 使用 LVGL 主题模式可防止默认主题覆盖我们的样式
    layer3_bg_arc_ = lv_arc_create(attitude_container_);
    lv_obj_set_size(layer3_bg_arc_, 260, 260);
    lv_obj_set_pos(layer3_bg_arc_, CENTER_X - 130, CENTER_Y - 130);
    lv_arc_set_range(layer3_bg_arc_, 0, 360);
    lv_arc_set_value(layer3_bg_arc_, 360);
    lv_arc_set_bg_angles(layer3_bg_arc_, 0, 360);
    lv_arc_set_angles(layer3_bg_arc_, 0, 360);
    // 关键修复: arc 背景部分 + 指示器部分都要设置颜色
    lv_obj_set_style_arc_width(layer3_bg_arc_, 4, 0);
    lv_obj_set_style_arc_color(layer3_bg_arc_, theme_colors.card_bg, 0);  // 背景
    lv_obj_set_style_arc_color(layer3_bg_arc_, theme_colors.card_bg, LV_PART_INDICATOR);  // 指示器也用背景色（因为全填充）
    lv_obj_set_style_opa(layer3_bg_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(layer3_bg_arc_, LV_OBJ_FLAG_CLICKABLE);

    // 进度环 (lv_arc, 直径 280, 半径 140px, 颜色=state_normal)
    // 关键: lv_arc 默认 indicator 颜色是蓝色 #2392EB, 必须在创建后立即用样式覆盖!
    layer3_progress_arc_ = lv_arc_create(attitude_container_);
    lv_obj_set_size(layer3_progress_arc_, 280, 280);
    lv_obj_set_pos(layer3_progress_arc_, CENTER_X - 140, CENTER_Y - 140);
    // 关键修复: 同时设置所有相关 part 的颜色, 防止默认蓝色污染
    lv_obj_set_style_arc_color(layer3_progress_arc_, lv_color_hex(0x000000), 0);  // 背景(0) 透明
    lv_obj_set_style_arc_color(layer3_progress_arc_, theme_colors.state_normal, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(layer3_progress_arc_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(layer3_progress_arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(layer3_progress_arc_, 0, 0);  // 背景部分透明
    // 然后再设置范围/角度
    lv_arc_set_range(layer3_progress_arc_, 0, 360);
    lv_arc_set_value(layer3_progress_arc_, 0);  // 初始进度 0
    lv_arc_set_bg_angles(layer3_progress_arc_, 0, 360);
    lv_arc_set_angles(layer3_progress_arc_, 0, 0);  // 初始从 0° 开始
    // 隐藏 knob
    lv_obj_set_style_opa(layer3_progress_arc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(layer3_progress_arc_, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "Layer3 StatusProgress created (90~144px)");
}

// 层级四: 边界留白区 (144~178px 半径范围)
void AttitudeDisplay::CreateLayer4Boundary()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;

    // 1px 鎏金外圆环 (直径 356, 半径 178px)
    layer4_outer_ring_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(layer4_outer_ring_, 356, 356);
    lv_obj_set_pos(layer4_outer_ring_, CENTER_X - 178, CENTER_Y - 178);
    lv_obj_set_style_radius(layer4_outer_ring_, 178, 0);
    lv_obj_set_style_bg_opa(layer4_outer_ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(layer4_outer_ring_, 1, 0);
    lv_obj_set_style_border_color(layer4_outer_ring_, theme_colors.border_line, 0);
    lv_obj_set_style_border_opa(layer4_outer_ring_, LV_OPA_100, 0);

    ESP_LOGI(TAG, "Layer4 Boundary created (144~178px)");
}

// 4个方位实心圆点 (设计文档第5节)
void AttitudeDisplay::CreateCompassPoints()
{
    const auto& theme_colors = AttitudeTheme::GetInstance().GetColors();
    const int CENTER_X = 180;
    const int CENTER_Y = 180;
    const int POINT_SIZE = 6;
    const int POINT_OFFSET = 18;  // 距屏幕边缘 18px (设计文档要求)

    // 北 (上)
    dir_n_label_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(dir_n_label_, POINT_SIZE, POINT_SIZE);
    lv_obj_set_pos(dir_n_label_, CENTER_X - POINT_SIZE/2, POINT_OFFSET);
    lv_obj_set_style_radius(dir_n_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dir_n_label_, theme_colors.point_default, 0);
    lv_obj_set_style_border_width(dir_n_label_, 0, 0);

    // 南 (下)
    dir_s_label_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(dir_s_label_, POINT_SIZE, POINT_SIZE);
    lv_obj_set_pos(dir_s_label_, CENTER_X - POINT_SIZE/2, 360 - POINT_OFFSET - POINT_SIZE);
    lv_obj_set_style_radius(dir_s_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dir_s_label_, theme_colors.point_default, 0);
    lv_obj_set_style_border_width(dir_s_label_, 0, 0);

    // 西 (左)
    dir_w_label_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(dir_w_label_, POINT_SIZE, POINT_SIZE);
    lv_obj_set_pos(dir_w_label_, POINT_OFFSET, CENTER_Y - POINT_SIZE/2);
    lv_obj_set_style_radius(dir_w_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dir_w_label_, theme_colors.point_default, 0);
    lv_obj_set_style_border_width(dir_w_label_, 0, 0);

    // 东 (右)
    dir_e_label_ = lv_obj_create(attitude_container_);
    lv_obj_set_size(dir_e_label_, POINT_SIZE, POINT_SIZE);
    lv_obj_set_pos(dir_e_label_, 360 - POINT_OFFSET - POINT_SIZE, CENTER_Y - POINT_SIZE/2);
    lv_obj_set_style_radius(dir_e_label_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dir_e_label_, theme_colors.point_default, 0);
    lv_obj_set_style_border_width(dir_e_label_, 0, 0);

    ESP_LOGI(TAG, "Compass points (N/E/S/W) created with 6x6 dots");
}

// 更新状态颜色 (公开API, 供外部调用改变状态等级)
void AttitudeDisplay::UpdateStateColor(int level)
{
    if (level < 0) level = 0;
    if (level > 4) level = 4;
    current_state_level_ = level;

    DisplayLockGuard lock(this);

    // 更新进度环颜色
    if (layer3_progress_arc_ != nullptr) {
        lv_color_t state_color = AttitudeTheme::GetInstance().GetStateColor(level);
        lv_obj_set_style_arc_color(layer3_progress_arc_, state_color, LV_PART_INDICATOR);
    }
}