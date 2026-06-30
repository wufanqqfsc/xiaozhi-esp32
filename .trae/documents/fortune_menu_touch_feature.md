# 功能计划：运势菜单图标触摸点击功能

## 任务概述

为 12 个运势功能环图标添加触摸点击功能，当用户点击对应图标时，通过 `DebugInfo` 视图显示该功能的一级分类内容（title 显示"图标+主功能名"，details 显示一级功能分类）。

## 当前状态分析

### 已实现功能
1. **Boot 按钮选中功能** ✅ 已实现
   - `HandleBootKey()` → `SelectFortuneMenuItemUnlocked(0)` 或 `CycleFortuneMenuSelectionUnlocked()`
   - 调用 `ShowFortuneFeatureCategoryUnlocked(index)` 显示 DebugInfo 卡

2. **DebugInfo 视图展示** ✅ 已实现
   - `kFortuneMenuFeatureCategories[]` 数组包含 12 个功能的一级分类内容
   - `ShowFortuneFeatureCategoryUnlocked()` 已实现，使用 `PresentDebugInfoCardUnlocked()` 显示卡片

3. **触摸层** ⚠️ 已禁用
   - `fortune_menu_ring_touch_` 触摸层已创建但触摸事件已禁用（`LV_OBJ_FLAG_CLICKABLE` 已清除）

### 待实现功能
- **图标点击功能** ❌ 未实现
  - 需要恢复触摸层的点击事件支持
  - 需要实现触摸坐标到图标索引的映射

## 实现方案

### 1. 恢复触摸层点击事件

在 `CreateFortuneMenuRingTouch()` 中重新启用触摸层的点击标志：

```cpp
// 恢复触摸点击支持
lv_obj_add_flag(fortune_menu_ring_touch_, LV_OBJ_FLAG_CLICKABLE);
```

### 2. 添加触摸事件处理函数

在 `attitude_display.cc` 中添加静态触摸事件回调函数 `OnFortuneMenuRingTouched()`：

```cpp
static void OnFortuneMenuRingTouched(lv_event_t* e)
{
    auto* self = static_cast<AttitudeDisplay*>(lv_event_get_user_data(e));
    if (self == nullptr) return;

    lv_indev_t* indev = lv_indev_get_active();
    if (indev == nullptr) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    // 计算触摸点相对于屏幕中心的角度
    const int dx = pt.x - ATTITUDE_CENTER_X;
    const int dy = pt.y - ATTITUDE_CENTER_Y;

    // 检查是否在允许的触摸区域内（TAIJI_RADIUS ~ LAYER4_BOUNDARY_RADIUS）
    const int r = static_cast<int>(sqrt(dx * dx + dy * dy));
    if (r < FORTUNE_MENU_TOUCH_INNER_R || r > FORTUNE_MENU_TOUCH_OUTER_R) {
        return; // 点击不在功能环区域内
    }

    // 计算角度并映射到图标索引
    double angle = atan2(dy, dx) * 180.0 / M_PI;
    angle -= FORTUNE_MENU_START_ANGLE_DEG; // 归一化到起始角度
    if (angle < 0) angle += 360.0;

    const double step = 360.0 / FORTUNE_MENU_COUNT;
    int index = static_cast<int>(angle / step) % FORTUNE_MENU_COUNT;

    // 调用现有的选中方法
    self->SelectFortuneMenuItemUnlocked(index);
}
```

### 3. 注册触摸事件

在 `CreateFortuneMenuRingTouch()` 中注册事件回调：

```cpp
lv_obj_add_event_cb(fortune_menu_ring_touch_, OnFortuneMenuRingTouched, LV_EVENT_CLICKED, this);
```

## 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `main/display/attitude_display.cc` | 1. 恢复触摸层点击标志<br>2. 添加 `OnFortuneMenuRingTouched()` 函数<br>3. 注册触摸事件回调 |
| `main/display/attitude_display.h` | 无需修改（`SelectFortuneMenuItemUnlocked` 已是 private 方法） |

## 验证步骤

1. **编译验证**
   ```bash
   cd xiaozhi-esp32
   ./build_and_flash.sh
   ```

2. **功能验证**
   - Boot 短按：循环选中图标，DebugInfo 显示一级分类 ✅
   - 触摸点击图标：选中该图标，DebugInfo 显示一级分类（新增）

3. **测试场景**
   - 点击太极圆内部的区域 → 无响应
   - 点击外圈空白区域 → 无响应
   - 点击功能环图标区域 → 选中该图标并显示 DebugInfo
   - Boot 循环选中 → 每次选中显示对应 DebugInfo
   - 点击 Power 键 → 取消选中态，隐藏 DebugInfo

## 预期效果

当用户触摸点击功能环上的图标时：
- 该图标变为选中态（放大 + 白色高亮）
- DebugInfo 卡显示在该位置
- title: "图标 + 主功能名"（如 "🧮 财运"）
- details: 一级功能分类列表（如 "1. 股票预测\n2. 理财延伸\n3. 传统财运"）
