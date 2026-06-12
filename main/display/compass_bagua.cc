#include "compass_bagua.h"
#include <esp_log.h>
#include <cmath>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_lvgl_port.h>

static const char* TAG = "CompassBagua";

// 静态成员初始化
lv_obj_t* CompassBagua::bagua_container_ = nullptr;
lv_obj_t* CompassBagua::bagua_labels_[64] = {nullptr};
lv_color_t CompassBagua::main_gold_ = lv_color_hex(0xD4AF37);
lv_color_t CompassBagua::variant_silver_ = lv_color_hex(0xC0C0C0);

// 64 卦爻表 (按先天八卦顺序: 乾兑离震巽坎艮坤)
// 64 卦由 6 个爻组成, bit 0 (LSB) = 底爻, bit 5 (MSB) = 顶爻
// bit = 1 表示阳爻 (实线), 0 表示阴爻 (虚线/两段)
//
// 先天八卦 8 个基本卦:
//  乾 (☰) = 0b111111 = 63 (6 个阳爻)
//  兑 (☱) = 0b111110 = 62 (5 阳 1 阴, 顶爻为阴)
//  离 (☲) = 0b101010 = 42 (3 阳 3 阴, 交替)
//  震 (☳) = 0b001001 =  9 (2 阳 4 阴, 底二阳, 上四阴)
//  巽 (☴) = 0b110110 = 54 (4 阳 2 阴)
//  坎 (☵) = 0b010010 = 18 (2 阳 4 阴, 中间二阳)
//  艮 (☶) = 0b100100 = 36 (2 阳 4 阴, 顶底阴, 中间阳)
//  坤 (☷) = 0b000000 =  0 (6 个阴爻)
//
// 64 卦排列 = 8 卦 × 8 卦 自下而上组合
// 索引公式: index = upper_trigram * 8 + lower_trigram
//   upper/lower: 0=坤 1=艮 2=坎 3=巽 4=震 5=离 6=兑 7=乾
//
// 简化: 用 6 字节数组定义 64 卦
const uint8_t CompassBagua::hexagrams_[64] = {
    // 0-7: 坤上 + (坤艮坎巽震离兑乾)下
    0b000000, 0b000001, 0b000010, 0b000011, 0b000100, 0b000101, 0b000110, 0b000111,
    // 8-15: 艮上 + (坤艮坎巽震离兑乾)下
    0b001000, 0b001001, 0b001010, 0b001011, 0b001100, 0b001101, 0b001110, 0b001111,
    // 16-23: 坎上 + (坤艮坎巽震离兑乾)下
    0b010000, 0b010001, 0b010010, 0b010011, 0b010100, 0b010101, 0b010110, 0b010111,
    // 24-31: 巽上 + (坤艮坎巽震离兑乾)下
    0b011000, 0b011001, 0b011010, 0b011011, 0b011100, 0b011101, 0b011110, 0b011111,
    // 32-39: 震上 + (坤艮坎巽震离兑乾)下
    0b100000, 0b100001, 0b100010, 0b100011, 0b100100, 0b100101, 0b100110, 0b100111,
    // 40-47: 离上 + (坤艮坎巽震离兑乾)下
    0b101000, 0b101001, 0b101010, 0b101011, 0b101100, 0b101101, 0b101110, 0b101111,
    // 48-55: 兑上 + (坤艮坎巽震离兑乾)下
    0b110000, 0b110001, 0b110010, 0b110011, 0b110100, 0b110101, 0b110110, 0b110111,
    // 56-63: 乾上 + (坤艮坎巽震离兑乾)下
    0b111000, 0b111001, 0b111010, 0b111011, 0b111100, 0b111101, 0b111110, 0b111111
};

// 8 个主卦索引 (8 个八卦在 64 卦中位置)
// 八卦先天顺序: 乾兑离震巽坎艮坤
// 八卦都是 "上卦 = 下卦" 的纯卦:
//   乾(上乾下乾) = 56+7 = 63
//   兑(上兑下兑) = 48+6 = 54
//   离(上离下离) = 40+5 = 45
//   震(上震下震) = 32+4 = 36
//   巽(上巽下巽) = 24+3 = 27
//   坎(上坎下坎) = 16+2 = 18
//   艮(上艮下艮) = 8+1 = 9
//   坤(上坤下坤) = 0+0 = 0
const int CompassBagua::main_gua_indices_[8] = {
    0,   // 坤 ☷
    9,   // 艮 ☶
    18,  // 坎 ☵
    27,  // 巽 ☴
    36,  // 震 ☳
    45,  // 离 ☲
    54,  // 兑 ☱
    63   // 乾 ☰
};

// 绘制一个爻 (阳爻=实线, 阴爻=虚线两段)
// 迭代18: 缩小尺寸以适应 r=160 外圈 - 12x12 卦象
static void DrawYao(lv_obj_t* canvas, int cx, int y, bool is_yang, lv_color_t color, int width, int gap) {
    if (is_yang) {
        // 阳爻: 实线
        for (int dx = -width; dx <= width; dx++) {
            lv_canvas_set_px(canvas, cx + dx, y, color, LV_OPA_COVER);
        }
    } else {
        // 阴爻: 虚线 (两段, 中间断开)
        for (int dx = -width - gap; dx <= -gap; dx++) {
            lv_canvas_set_px(canvas, cx + dx, y, color, LV_OPA_COVER);
        }
        for (int dx = gap; dx <= width + gap; dx++) {
            lv_canvas_set_px(canvas, cx + dx, y, color, LV_OPA_COVER);
        }
    }
}

// 绘制一个卦象 (6 个爻) 在 canvas 上的指定中心位置
// hexagram: 6 个 bit, bit 0 = 底爻, bit 5 = 顶爻
// 迭代18: 自适应卦象尺寸 (12x12 或 18x18)
static void DrawHexagram(lv_obj_t* canvas, int cx, int cy, uint8_t hexagram, lv_color_t color, int size) {
    // 紧凑绘制: 每爻 1px, 爻间 0.5px 间隙, 6 爻总高 ~ 9px
    const int yao_height = 1;   // 每个爻占 1 像素 (紧凑)
    const int yao_gap = 1;      // 爻之间间隔 1 像素
    const int total_height = (yao_height + yao_gap) * 6 - yao_gap;  // 6 爻总高度

    // 爻宽: 主卦 3px, 变卦 2px
    int yao_width = (size >= 20) ? 3 : 2;
    int yao_gap_inner = 1;

    // 从底爻 (bit 0) 到顶爻 (bit 5) 绘制
    int start_y = cy + total_height / 2;
    for (int i = 0; i < 6; i++) {
        bool is_yang = (hexagram >> i) & 1;
        int yao_y = start_y - i * (yao_height + yao_gap);
        DrawYao(canvas, cx, yao_y, is_yang, color, yao_width, yao_gap_inner);
    }
}

// 创建一个卦象的 canvas 并绘制
// size: 卦象的尺寸 (宽度=高度)
static lv_obj_t* CreateHexagramCanvas(lv_obj_t* parent, int cx, int cy, uint8_t hexagram, lv_color_t color, int size) {
    // 分配 canvas buffer (ARGB8888 透明)
    size_t buf_size = size * size * 4;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        buf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate hexagram canvas buffer (%u bytes)", buf_size);
        return nullptr;
    }
    memset(buf, 0, buf_size);

    // 创建 canvas
    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, buf, size, size, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_pos(canvas, cx - size / 2, cy - size / 2);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_obj_set_style_outline_width(canvas, 0, 0);
    lv_obj_set_style_shadow_width(canvas, 0, 0);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);

    // 绘制卦象在 canvas 中心
    int center = size / 2;
    DrawHexagram(canvas, center, center, hexagram, color, size);

    return canvas;
}

void CompassBagua::Create(lv_obj_t* parent, int cx, int cy, int radius) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "Create: parent is null");
        return;
    }

    ESP_LOGI(TAG, "Creating 64 bagua at (%d, %d) radius=%d", cx, cy, radius);

    // 创建容器 (透明, 不接收事件)
    bagua_container_ = lv_obj_create(parent);
    lv_obj_set_size(bagua_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(bagua_container_, 0, 0);
    lv_obj_set_style_bg_opa(bagua_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bagua_container_, 0, 0);
    lv_obj_clear_flag(bagua_container_, LV_OBJ_FLAG_CLICKABLE);

    // 沿圆周均匀分布 64 卦 (每 5.625° = 360/64)
    // 起始角度: 0° 在北方 (顶部), 顺时针排列
    const float angle_step_rad = 2.0f * M_PI / 64.0f;

    // 迭代18: 卦象尺寸需 ≥ 24px 高 (6 爻 x (3px 爻 + 1px 间隙) = 24px)
    // r=130 圆周上 5.625° 间隔 = 弧长 12.8px
    // 主卦 24x24 (填充弧长但可能略重叠), 变卦 18x18
    for (int i = 0; i < 64; i++) {
        // 角度: 从 -90° 开始 (北方), 顺时针
        float angle = -M_PI / 2.0f + i * angle_step_rad;

        // 计算位置
        int px = cx + (int)(radius * cosf(angle));
        int py = cy + (int)(radius * sinf(angle));

        // 判断是否主卦 (8 个)
        bool is_main = false;
        for (int j = 0; j < 8; j++) {
            if (main_gua_indices_[j] == i) {
                is_main = true;
                break;
            }
        }

        lv_color_t color = is_main ? main_gold_ : variant_silver_;

        // 创建卦象 canvas
        // 迭代18: 卦象紧凑尺寸 (适配 r=160 外圈)
        // r=160 圆周上 5.625° 间隔 = 弧长 15.7px
        // 主卦 12x12, 变卦 8x8, 紧凑绘制 (每爻 1px, 6 爻总高 9-11px)
        int size = is_main ? 12 : 8;
        bagua_labels_[i] = CreateHexagramCanvas(bagua_container_, px, py, hexagrams_[i], color, size);
    }

    ESP_LOGI(TAG, "64 bagua created successfully at radius=%d (size: main=12, variant=8)", radius);
}

void CompassBagua::UpdateTheme() {
    // 主题色已硬编码, 暂不实现
}

void CompassBagua::Delete() {
    if (bagua_container_ != nullptr) {
        lv_obj_delete(bagua_container_);
        bagua_container_ = nullptr;
    }
    for (int i = 0; i < 64; i++) {
        bagua_labels_[i] = nullptr;
    }
}
