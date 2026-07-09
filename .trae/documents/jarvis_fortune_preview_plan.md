# 每日运势选中显示 Jarvis.png 实施计划

## 1. 需求分析

**用户需求**：选中"每日运势"（今日运势，index 0）时，在屏幕中央显示 `doc/Jarvis.png` 图片。

**当前行为**：
- 选中任意运势菜单项时，调用 `ShowFortuneFeatureCategoryUnlocked(index)` 显示 DebugInfo 卡片，展示一级分类文本
- 已有 `SetPreviewImage()` 机制和 `image_overlay_card_` 圆形浮层，支持 PNG/JPG/GIF 显示
- 已有 `preview_image_`（lv_image widget）用于显示静态图

**目标**：
- 选中"今日运势"（index 0）时，显示 Jarvis.png 图片，不显示 DebugInfo 卡片
- 选中其他运势项时，保持原有 DebugInfo 卡片行为
- 取消选中（电源键短按）时，隐藏图片，恢复主界面

---

## 2. 仓库研究结论

### 2.1 图片资源加载方式

项目有两种图片加载方案：

**方案 A：assets 分区加载**（推荐，与现有体系一致）
- 资源存储在 Flash 的 `assets` 分区
- 通过 `Assets::GetAssetData(name, ptr, size)` 获取数据
- 已有完整的索引机制（index.json）
- 优点：不占用固件空间，可独立替换
- 缺点：需要额外打包步骤

**方案 B：直接嵌入 C 数组**
- 将 PNG 转换为 `uint8_t jarvis_png[]` 数组
- `#include` 到代码中
- 优点：简单直接，编译即包含
- 缺点：增大固件体积（Jarvis.png 360×360 RGBA 预计 50~150KB）

**选择方案 B**：因为只有一张图片，且作为核心 UI 元素，嵌入固件更可靠，避免 assets 分区缺失导致不显示。

### 2.2 现有显示机制

- `SetPreviewImage(std::unique_ptr<LvglImage> image)` — 显示图片浮层，传入 nullptr 隐藏
- `LvglAllocatedImage(data, size)` — 从内存 buffer 构造 PNG/JPG 图片描述符
- `image_overlay_card_` — 圆形浮层容器，尺寸 = `LAYER4_OUTER_SIZE`（约 356×356）
- `preview_image_` — lv_image widget，用于静态图显示
- 图片缩放：`lv_image_set_scale(preview_image_, 128 * DEBUG_INFO_CARD_W / img_dsc->header.w)`

### 2.3 关键代码位置

| 文件 | 函数/变量 | 作用 |
|------|----------|------|
| `main/display/attitude_display.cc` | `SelectFortuneMenuItemUnlocked()` | 选中运势项入口 |
| `main/display/attitude_display.cc` | `ShowFortuneFeatureCategoryUnlocked()` | 显示 DebugInfo 分类卡片 |
| `main/display/attitude_display.cc` | `DeselectFortuneMenuItemUnlocked()` | 取消选中 |
| `main/display/attitude_display.cc` | `SetPreviewImage()` | 图片浮层显示/隐藏 |
| `main/display/lvgl_display/lvgl_image.h` | `LvglAllocatedImage` | PNG 内存图片构造 |
| `main/display/attitude_display.h` | `preview_image_`, `image_overlay_card_` | 图片相关成员 |

---

## 3. 修改文件与模块

### 3.1 新增文件
- `main/assets/common/jarvis.png` — 图片资源（从 doc/Jarvis.png 复制）
- `main/display/jarvis_image.h` — 嵌入 PNG 数据的 C 数组头文件（由脚本生成或手动转换）

### 3.2 修改文件
- `main/display/attitude_display.cc` — 修改选中逻辑，index 0 时显示图片
- `main/display/attitude_display.h` — 如有需要新增成员变量

### 3.3 CMakeLists.txt
- `main/CMakeLists.txt` — 如需编译新源文件则加入

---

## 4. 实施步骤

### 步骤 1：准备图片资源
- 从 `doc/Jarvis.png` 复制到 `main/assets/common/jarvis.png`
- 用 Python/xxd 工具将 PNG 转换为 C 数组头文件 `jarvis_image.h`
  - 格式：`static const uint8_t jarvis_png_data[] = { ... };`
  - 外加 `static const size_t jarvis_png_size = ...;`

### 步骤 2：修改选中逻辑
在 `SelectFortuneMenuItemUnlocked(int index)` 中：
- 当 `index == 0`（今日运势）时：
  - 不调用 `ShowFortuneFeatureCategoryUnlocked(index)`
  - 构造 `LvglAllocatedImage`（用 jarvis_png_data）
  - 调用 `SetPreviewImage()` 显示图片
- 当 `index != 0` 时：
  - 先调用 `SetPreviewImage(nullptr)` 隐藏图片（防止上一次还显示着）
  - 保持原有 `ShowFortuneFeatureCategoryUnlocked(index)` 行为

### 步骤 3：修改取消选中逻辑
在 `DeselectFortuneMenuItemUnlocked()` 中：
- 增加 `SetPreviewImage(nullptr)` 调用，确保取消选中时图片也隐藏

### 步骤 4：修改循环选中逻辑
在 `CycleFortuneMenuSelectionUnlocked()` 中：
- 逻辑同步骤 2，根据新 index 判断是显示图片还是 DebugInfo 卡片

### 步骤 5：编译验证
- 执行 `./build_and_flash.sh` 编译烧录
- 验证选中"今日运势"时显示 Jarvis.png
- 验证选中其他运势项时正常显示 DebugInfo 卡片
- 验证电源键短按取消选中时图片隐藏
- 验证循环切换时图片/卡片正确切换

---

## 5. 潜在依赖与注意事项

### 5.1 图片尺寸
- Jarvis.png 为 360×360
- `image_overlay_card_` 尺寸为 `LAYER4_OUTER_SIZE`（约 356×356，因屏幕边缘有金色环）
- 现有 `SetPreviewImage` 会自动缩放（`lv_image_set_scale`）
- 需要确认缩放后效果是否正常（360 缩到 ~356 应该没问题）

### 5.2 内存占用
- PNG 原图约 50~150KB（360×360 RGBA）
- 嵌入固件后占用 Flash，运行时也会在 RAM 中存在
- 8MB PSRAM + 8MB Flash 应该没问题

### 5.3 显示层级
- `image_overlay_card_` 显示时会隐藏 `attitude_container_`（整个主界面）
- 这是 `SetPreviewImage` 的默认行为
- 对于"今日运势"选中态，是否需要保留周围的运势菜单图标？
  - **方案**：保留当前行为（全屏显示图片），因为是选中态的强调展示
  - 如果用户希望保留菜单环，需要新增一个轻量级图片显示方式（不隐藏主界面）

### 5.4 与 DebugInfo 卡片的互斥
- 当前 DebugInfo 卡片（function_area_card_）和图片浮层（image_overlay_card_）是两套独立机制
- 需要确保二者不同时显示
- 在 `ShowFortuneFeatureCategoryUnlocked` 调用前先 `SetPreviewImage(nullptr)`，反之亦然

---

## 6. 风险处理

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| PNG 解码失败 | 图片不显示 | 检查 LVGL lodepng decoder 是否启用；添加错误日志 |
| 内存不足 | 编译/运行时崩溃 | 若固件过大，改用 assets 分区方案 |
| 图片缩放后模糊 | 视觉效果差 | 调整缩放算法，或预先把图片缩放到目标尺寸 |
| 与现有功能冲突 | 其他运势项显示异常 | 充分测试所有 12 个运势项的选中/取消行为 |

---

## 7. 验证清单

- [ ] 选中"今日运势"（index 0）显示 Jarvis.png 图片
- [ ] 选中其他 11 个运势项显示 DebugInfo 卡片（原有行为不变）
- [ ] 电源键短按取消选中时，图片/卡片均隐藏
- [ ] Boot 键循环切换时，图片/卡片正确切换
- [ ] 触摸选中"今日运势"时显示图片
- [ ] 编译无错误，固件可正常烧录
- [ ] 真机功能测试通过
