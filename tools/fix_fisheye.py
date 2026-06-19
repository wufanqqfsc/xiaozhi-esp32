#!/usr/bin/env python3
"""
鱼眼图标优化重构脚本：
1. 删除旧的 AllocFisheyeCanvasBuffer + CreateFisheyeCanvas（canvas buffer 绘制）
2. 重写 CreateWifiFisheye / CreateBleFisheye（使用 LVGL 样式系统，20x20）
3. 删除 RedrawWifiFisheyeCanvas / RedrawBleFisheyeCanvas
4. 重写 ApplyWifiFisheyeStyle / ApplyBleFisheyeStyle
5. 重写 UpdateWifiFisheyeBorderColor / UpdateBleFisheyeBorderColor
6. 修改 OnFortuneFisheyePulseTimer（不再引用 canvas）
7. 修改迷宫相关代码中对 wifi_fisheye_canvas_ / ble_fisheye_canvas_ 的引用
"""
import sys

FILE_PATH = "/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/main/display/attitude_display.cc"

def main():
    with open(FILE_PATH, 'r') as f:
        content = f.read()

    # === 1. 删除 AllocFisheyeCanvasBuffer 和 CreateFisheyeCanvas ===
    # 这两个函数紧跟在 GetFisheyeIconFont 后面
    old_block = """static uint32_t* AllocFisheyeCanvasBuffer(int size)
{
    const uint32_t buf_size = static_cast<uint32_t>(size) * static_cast<uint32_t>(size) *
                              sizeof(uint32_t);
    auto* buf = static_cast<uint32_t*>(heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM));
    if (buf == nullptr) {
        buf = static_cast<uint32_t*>(malloc(buf_size));
    }
    if (buf != nullptr) {
        memset(buf, 0, buf_size);
    }
    return buf;
}

static lv_obj_t* CreateFisheyeCanvas(lv_obj_t* parent, uint32_t*& out_buf)
{
    out_buf = AllocFisheyeCanvasBuffer(FISHEYE_ICON_SIZE);
    if (out_buf == nullptr) {
        ESP_LOGE(TAG, "Fisheye canvas alloc failed (%d px)", FISHEYE_ICON_SIZE);
        return nullptr;
    }

    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, out_buf, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE,
                         LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSPARENT, 0);
    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_image_set_antialias(canvas, false);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    return canvas;
}

"""

    if old_block in content:
        content = content.replace(old_block, "")
        print("[OK] 已删除 AllocFisheyeCanvasBuffer 和 CreateFisheyeCanvas")
    else:
        print("[INFO] AllocFisheyeCanvasBuffer 已不存在，跳过")

    # === 2. 重写 FisheyeBorderColorAnimCb（让它实际改变边框色） ===
    old_fb = """static void FisheyeBorderColorAnimCb(void* obj, int32_t value)
{
    (void)obj;
    (void)value;
}

"""
    new_fb = """static void FisheyeBorderColorAnimCb(void* obj, int32_t value)
{
    lv_obj_set_style_border_color(static_cast<lv_obj_t*>(obj),
                                   lv_color_hex(static_cast<uint32_t>(value)), 0);
}

"""
    if old_fb in content:
        content = content.replace(old_fb, new_fb)
        print("[OK] 已重写 FisheyeBorderColorAnimCb")
    else:
        print("[INFO] FisheyeBorderColorAnimCb 可能已修改，跳过")

    # === 3. 删除 RedrawWifiFisheyeCanvas 和 RedrawBleFisheyeCanvas ===
    old_redraw = """void AttitudeDisplay::RedrawWifiFisheyeCanvas()
{
    if (wifi_fisheye_canvas_ == nullptr) {
        return;
    }
    CompassTaiji::PaintFisheyeDisc(wifi_fisheye_canvas_, FISHEYE_ICON_SIZE,
                                   lv_color_black(), kFisheyeWifiRingBorder,
                                   lv_color_white(), FISHEYE_BORDER_WIDTH);
}

void AttitudeDisplay::RedrawBleFisheyeCanvas()
{
    if (ble_fisheye_canvas_ == nullptr) {
        return;
    }
    CompassTaiji::PaintFisheyeDisc(ble_fisheye_canvas_, FISHEYE_ICON_SIZE,
                                   lv_color_white(), kFisheyeBleRingBorder,
                                   lv_color_black(), FISHEYE_BORDER_WIDTH);
}

"""

    if old_redraw in content:
        content = content.replace(old_redraw, "")
        print("[OK] 已删除 RedrawWifiFisheyeCanvas / RedrawBleFisheyeCanvas")
    else:
        # 可能是用新的常量名
        alt_redraw = """void AttitudeDisplay::RedrawWifiFisheyeCanvas()
{
    if (wifi_fisheye_canvas_ == nullptr) {
        return;
    }
    CompassTaiji::PaintFisheyeDisc(wifi_fisheye_canvas_, FISHEYE_ICON_SIZE,
                                   lv_color_black(), kFisheyeWifiRingBorder,
                                   lv_color_white(), FISHEYE_BORDER_WIDTH);
}

void AttitudeDisplay::RedrawBleFisheyeCanvas()
{
    if (ble_fisheye_canvas_ == nullptr) {
        return;
    }
    CompassTaiji::PaintFisheyeDisc(ble_fisheye_canvas_, FISHEYE_ICON_SIZE,
                                   lv_color_white(), kFisheyeBleRingBorder,
                                   lv_color_black(), FISHEYE_BORDER_WIDTH);
}

"""
        if alt_redraw in content:
            content = content.replace(alt_redraw, "")
            print("[OK] 已删除 RedrawWifiFisheyeCanvas / RedrawBleFisheyeCanvas (alt)")
        else:
            print("[INFO] 可能已被删除，跳过")

    # === 4. 重写 CreateWifiFisheye ===
    old_create_wifi = """void AttitudeDisplay::CreateWifiFisheye()
{
    lv_obj_t* parent = CompassTaiji::GetContainer();
    if (parent == nullptr) {
        ESP_LOGE(TAG, "CreateWifiFisheye: taiji container is null");
        return;
    }

    wifi_fisheye_ = lv_obj_create(parent);
    lv_obj_set_size(wifi_fisheye_, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE);
    lv_obj_set_pos(wifi_fisheye_, FISHEYE_WIFI_LOCAL_X, FISHEYE_WIFI_LOCAL_Y);
    lv_obj_set_style_radius(wifi_fisheye_, FISHEYE_ICON_SIZE / 2, 0);
    lv_obj_set_style_clip_corner(wifi_fisheye_, true, 0);
    lv_obj_set_style_bg_opa(wifi_fisheye_, LV_OPA_TRANSPARENT, 0);
    lv_obj_set_style_border_width(wifi_fisheye_, 0, 0);
    lv_obj_set_style_pad_all(wifi_fisheye_, 0, 0);
    lv_obj_clear_flag(wifi_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    uint32_t* canvas_buf = nullptr;
    wifi_fisheye_canvas_ = CreateFisheyeCanvas(wifi_fisheye_, canvas_buf);
    (void)canvas_buf;
    RedrawWifiFisheyeCanvas();

    wifi_fisheye_icon_ = lv_label_create(wifi_fisheye_);
    lv_obj_set_style_text_font(wifi_fisheye_icon_, GetIconFont(this), 0);
    lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
    lv_obj_center(wifi_fisheye_icon_);
    lv_obj_move_foreground(wifi_fisheye_icon_);

    ESP_LOGI(TAG, "WiFi fisheye on taiji at local (%d,%d)",
             FISHEYE_WIFI_LOCAL_X, FISHEYE_WIFI_LOCAL_Y);
}

"""

    new_create_wifi = """void AttitudeDisplay::CreateWifiFisheye()
{
    lv_obj_t* parent = CompassTaiji::GetContainer();
    if (parent == nullptr) {
        ESP_LOGE(TAG, "CreateWifiFisheye: taiji container is null");
        return;
    }

    // 使用 LVGL 样式系统：直接在容器上画圆形背景+边框，内部放图标 label
    wifi_fisheye_ = lv_obj_create(parent);
    lv_obj_set_size(wifi_fisheye_, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE);
    lv_obj_set_pos(wifi_fisheye_, FISHEYE_WIFI_LOCAL_X, FISHEYE_WIFI_LOCAL_Y);
    lv_obj_set_style_radius(wifi_fisheye_, FISHEYE_ICON_SIZE / 2, 0);
    lv_obj_set_style_clip_corner(wifi_fisheye_, true, 0);
    lv_obj_set_style_bg_opa(wifi_fisheye_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(wifi_fisheye_, kFisheyeWifiBg, 0);
    lv_obj_set_style_border_width(wifi_fisheye_, FISHEYE_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeWifiBorder, 0);
    lv_obj_set_style_pad_all(wifi_fisheye_, 0, 0);
    lv_obj_clear_flag(wifi_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    wifi_fisheye_icon_ = lv_label_create(wifi_fisheye_);
    lv_obj_set_style_text_font(wifi_fisheye_icon_, GetFisheyeIconFont(), 0);
    lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
    lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
    lv_obj_center(wifi_fisheye_icon_);
    lv_obj_move_foreground(wifi_fisheye_icon_);

    ESP_LOGI(TAG, "WiFi fisheye on taiji at local (%d,%d) size=%d",
             FISHEYE_WIFI_LOCAL_X, FISHEYE_WIFI_LOCAL_Y, FISHEYE_ICON_SIZE);
}

"""

    if old_create_wifi in content:
        content = content.replace(old_create_wifi, new_create_wifi)
        print("[OK] 已重写 CreateWifiFisheye")
    else:
        print("[WARN] CreateWifiFisheye 匹配失败，可能已被修改")

    # === 5. 重写 CreateBleFisheye ===
    old_create_ble = """void AttitudeDisplay::CreateBleFisheye()
{
    lv_obj_t* parent = CompassTaiji::GetContainer();
    if (parent == nullptr) {
        ESP_LOGE(TAG, "CreateBleFisheye: taiji container is null");
        return;
    }

    ble_fisheye_ = lv_obj_create(parent);
    lv_obj_set_size(ble_fisheye_, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE);
    lv_obj_set_pos(ble_fisheye_, FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y);
    lv_obj_set_style_radius(ble_fisheye_, FISHEYE_ICON_SIZE / 2, 0);
    lv_obj_set_style_clip_corner(ble_fisheye_, true, 0);
    lv_obj_set_style_bg_opa(ble_fisheye_, LV_OPA_TRANSPARENT, 0);
    lv_obj_set_style_border_width(ble_fisheye_, 0, 0);
    lv_obj_set_style_pad_all(ble_fisheye_, 0, 0);
    lv_obj_clear_flag(ble_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    uint32_t* canvas_buf = nullptr;
    ble_fisheye_canvas_ = CreateFisheyeCanvas(ble_fisheye_, canvas_buf);
    (void)canvas_buf;
    RedrawBleFisheyeCanvas();

    ble_fisheye_icon_ = lv_label_create(ble_fisheye_);
    lv_obj_set_style_text_font(ble_fisheye_icon_, GetIconFont(this), 0);
    lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
    lv_obj_center(ble_fisheye_icon_);
    lv_obj_move_foreground(ble_fisheye_icon_);

    ESP_LOGI(TAG, "BLE fisheye on taiji at local (%d,%d)",
             FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y);
}

"""

    new_create_ble = """void AttitudeDisplay::CreateBleFisheye()
{
    lv_obj_t* parent = CompassTaiji::GetContainer();
    if (parent == nullptr) {
        ESP_LOGE(TAG, "CreateBleFisheye: taiji container is null");
        return;
    }

    // 使用 LVGL 样式系统：白底黑描边蓝色图标
    ble_fisheye_ = lv_obj_create(parent);
    lv_obj_set_size(ble_fisheye_, FISHEYE_ICON_SIZE, FISHEYE_ICON_SIZE);
    lv_obj_set_pos(ble_fisheye_, FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y);
    lv_obj_set_style_radius(ble_fisheye_, FISHEYE_ICON_SIZE / 2, 0);
    lv_obj_set_style_clip_corner(ble_fisheye_, true, 0);
    lv_obj_set_style_bg_opa(ble_fisheye_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ble_fisheye_, kFisheyeBleBg, 0);
    lv_obj_set_style_border_width(ble_fisheye_, FISHEYE_BORDER_WIDTH, 0);
    lv_obj_set_style_border_color(ble_fisheye_, kFisheyeBleBorder, 0);
    lv_obj_set_style_pad_all(ble_fisheye_, 0, 0);
    lv_obj_clear_flag(ble_fisheye_, LV_OBJ_FLAG_CLICKABLE);

    ble_fisheye_icon_ = lv_label_create(ble_fisheye_);
    lv_obj_set_style_text_font(ble_fisheye_icon_, GetFisheyeIconFont(), 0);
    lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
    lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
    lv_obj_center(ble_fisheye_icon_);
    lv_obj_move_foreground(ble_fisheye_icon_);

    ESP_LOGI(TAG, "BLE fisheye on taiji at local (%d,%d) size=%d",
             FISHEYE_BLE_LOCAL_X, FISHEYE_BLE_LOCAL_Y, FISHEYE_ICON_SIZE);
}

"""

    if old_create_ble in content:
        content = content.replace(old_create_ble, new_create_ble)
        print("[OK] 已重写 CreateBleFisheye")
    else:
        print("[WARN] CreateBleFisheye 匹配失败，可能已被修改")

    # === 6. 重写 ApplyWifiFisheyeStyle ===
    old_apply_wifi = """void AttitudeDisplay::ApplyWifiFisheyeStyle(WifiStatus status)
{
    if (wifi_fisheye_ == nullptr || wifi_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(wifi_fisheye_);
    RedrawWifiFisheyeCanvas();

    switch (status) {
    case WifiStatus::DISCONNECTED:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI_SLASH);
        break;
    case WifiStatus::CONNECTING:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
        StartFisheyePulse(wifi_fisheye_);
        break;
    case WifiStatus::CONNECTED:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
        break;
    default:
        break;
    }
}

"""

    new_apply_wifi = """void AttitudeDisplay::ApplyWifiFisheyeStyle(WifiStatus status)
{
    if (wifi_fisheye_ == nullptr || wifi_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(wifi_fisheye_);

    // 恢复默认边框色（白色）
    lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeWifiBorder, 0);

    switch (status) {
    case WifiStatus::DISCONNECTED:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI_SLASH);
        break;
    case WifiStatus::CONNECTING:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
        StartFisheyePulse(wifi_fisheye_);
        break;
    case WifiStatus::CONNECTED:
        lv_obj_set_style_text_color(wifi_fisheye_icon_, kFisheyeGold, 0);
        lv_label_set_text(wifi_fisheye_icon_, FONT_AWESOME_WIFI);
        break;
    default:
        break;
    }
}

void AttitudeDisplay::UpdateWifiFisheyeBorderColor(WifiStatus status)
{
    if (wifi_fisheye_ == nullptr) {
        return;
    }
    // 预留：可根据状态动态改变边框颜色
    lv_obj_set_style_border_color(wifi_fisheye_, kFisheyeWifiBorder, 0);
}

"""

    if old_apply_wifi in content:
        content = content.replace(old_apply_wifi, new_apply_wifi)
        print("[OK] 已重写 ApplyWifiFisheyeStyle + 新增 UpdateWifiFisheyeBorderColor")
    else:
        print("[WARN] ApplyWifiFisheyeStyle 匹配失败")

    # === 7. 重写 ApplyBleFisheyeStyle ===
    old_apply_ble = """void AttitudeDisplay::ApplyBleFisheyeStyle(BleStatus status)
{
    if (ble_fisheye_ == nullptr || ble_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(ble_fisheye_);
    RedrawBleFisheyeCanvas();

    switch (status) {
    case BleStatus::DISABLED:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    case BleStatus::ADVERTISING:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    case BleStatus::CONNECTED:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeBleBlue, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    default:
        break;
    }
}

"""

    new_apply_ble = """void AttitudeDisplay::ApplyBleFisheyeStyle(BleStatus status)
{
    if (ble_fisheye_ == nullptr || ble_fisheye_icon_ == nullptr) {
        return;
    }

    StopFisheyePulse(ble_fisheye_);

    // 恢复默认边框色（黑色）
    lv_obj_set_style_border_color(ble_fisheye_, kFisheyeBleBorder, 0);

    switch (status) {
    case BleStatus::DISABLED:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    case BleStatus::ADVERTISING:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeGrayIcon, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    case BleStatus::CONNECTED:
        lv_obj_set_style_text_color(ble_fisheye_icon_, kFisheyeBleBlue, 0);
        lv_label_set_text(ble_fisheye_icon_, FONT_AWESOME_BLUETOOTH);
        break;
    default:
        break;
    }
}

void AttitudeDisplay::UpdateBleFisheyeBorderColor(BleStatus status)
{
    if (ble_fisheye_ == nullptr) {
        return;
    }
    lv_obj_set_style_border_color(ble_fisheye_, kFisheyeBleBorder, 0);
}

"""

    if old_apply_ble in content:
        content = content.replace(old_apply_ble, new_apply_ble)
        print("[OK] 已重写 ApplyBleFisheyeStyle + 新增 UpdateBleFisheyeBorderColor")
    else:
        print("[WARN] ApplyBleFisheyeStyle 匹配失败")

    # === 8. 重写 OnFortuneFisheyePulseTimer（不再引用 canvas） ===
    old_pulse = """    self->fortune_fisheye_pulse_gold_ = !self->fortune_fisheye_pulse_gold_;
    const lv_color_t icon = self->fortune_fisheye_pulse_gold_ ? kFisheyeGold : kFisheyeGrayIcon;

    if (self->wifi_fisheye_canvas_ != nullptr) {
        self->RedrawWifiFisheyeCanvas();
        lv_obj_set_style_text_color(self->wifi_fisheye_icon_, icon, 0);
    }
    if (self->ble_fisheye_canvas_ != nullptr) {
        self->RedrawBleFisheyeCanvas();
        const lv_color_t ble_icon = self->fortune_fisheye_pulse_gold_
            ? kFisheyeBleBlue : kFisheyeGrayIcon;
        lv_obj_set_style_text_color(self->ble_fisheye_icon_, ble_icon, 0);
    }

    self->fortune_fisheye_pulse_count_++;
    if (self->fortune_fisheye_pulse_count_ >= 5) {"""

    new_pulse = """    self->fortune_fisheye_pulse_gold_ = !self->fortune_fisheye_pulse_gold_;
    const lv_color_t icon = self->fortune_fisheye_pulse_gold_ ? kFisheyeGold : kFisheyeGrayIcon;

    if (self->wifi_fisheye_ != nullptr && self->wifi_fisheye_icon_ != nullptr) {
        lv_obj_set_style_text_color(self->wifi_fisheye_icon_, icon, 0);
    }
    if (self->ble_fisheye_ != nullptr && self->ble_fisheye_icon_ != nullptr) {
        const lv_color_t ble_icon = self->fortune_fisheye_pulse_gold_
            ? kFisheyeBleBlue : kFisheyeGrayIcon;
        lv_obj_set_style_text_color(self->ble_fisheye_icon_, ble_icon, 0);
    }

    self->fortune_fisheye_pulse_count_++;
    if (self->fortune_fisheye_pulse_count_ >= 5) {"""

    if old_pulse in content:
        content = content.replace(old_pulse, new_pulse)
        print("[OK] 已重写 OnFortuneFisheyePulseTimer（移除 canvas 引用）")
    else:
        print("[WARN] OnFortuneFisheyePulseTimer 匹配失败，可能已被修改")

    # === 9. 移除对旧的常量 kFisheyeWifiRingBorder / kFisheyeBleRingBorder 的残留引用 ===
    # 这些常量可能在其它地方还被引用，先保留（它们在 namespace{ } 中定义），
    # 只是不再被 CreateFisheye... 和 Redraw... 使用。
    # 如后续发现可再清理。

    # 保存文件
    with open(FILE_PATH, 'w') as f:
        f.write(content)

    print("\n[DONE] 鱼眼图标重构完成！")
    print("  - 鱼眼尺寸：28 -> 20 px")
    print("  - 字体：GetIconFont() -> GetFisheyeIconFont() (font_awesome_16_4)")
    print("  - 绘制：ARGB8888 canvas buffer -> LVGL 样式系统")
    print("  - 删除：AllocFisheyeCanvasBuffer/CreateFisheyeCanvas/Redraw...")
    print("  - 新增：GetFisheyeIconFont()、UpdateWifiFisheyeBorderColor()、UpdateBleFisheyeBorderColor()")
    print("  - 新增：FisheyeBorderColorAnimCb() 实际更新边框色")


if __name__ == "__main__":
    main()
