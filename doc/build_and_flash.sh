#!/bin/bash
# build_and_flash.sh - Waveshare ESP32-S3-Touch-LCD-1.85B 编译和烧录脚本
# 
# 专门用于 Waveshare ESP32-S3-Touch-LCD-1.85B 开发板
#
# 使用方法:
#   ./build_and_flash.sh [action] [port]
# 
# 参数:
#   action - 操作类型: build, flash, monitor, all (默认: all)
#   port   - 串口设备 (默认: 自动检测)
#
# 示例:
#   ./build_and_flash.sh              # 编译并烧录
#   ./build_and_flash.sh build        # 仅编译
#   ./build_and_flash.sh flash        # 仅烧录
#   ./build_and_flash.sh monitor      # 仅监视串口
#   ./build_and_flash.sh all          # 编译、烧录并监视

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印函数
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 配置 - ESP32-S3-Touch-LCD-1.85B 版本（不带TCA9554 IO扩展器）
BOARD_TYPE="waveshare/esp32-s3-touch-lcd-1.85b"
BOARD_NAME="Waveshare ESP32-S3-Touch-LCD-1.85B"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"; while [ ! -f "$PROJECT_DIR/CMakeLists.txt" ] && [ "$PROJECT_DIR" != "/" ]; do PROJECT_DIR="$(dirname "$PROJECT_DIR")"; done

# ESP-IDF路径配置（已安装的版本）
IDF_PATHS=(
    "$HOME/.espressif/v5.5.4/esp-idf"
    "$HOME/esp/esp-idf"
)

# 检查ESP-IDF环境
check_idf_env() {
    print_info "检查ESP-IDF环境..."
    
    if [ -n "$IDF_PATH" ] && command -v idf.py &> /dev/null; then
        IDF_VERSION=$(idf.py --version 2>/dev/null | head -1)
        print_success "ESP-IDF环境已就绪: $IDF_VERSION"
        return 0
    fi
    
    print_info "尝试自动加载ESP-IDF..."
    
    # 尝试多个ESP-IDF路径
    for idf_path in "${IDF_PATHS[@]}"; do
        if [ -d "$idf_path" ] && [ -f "$idf_path/export.sh" ]; then
            print_info "加载ESP-IDF: $idf_path"
            source "$idf_path/export.sh" 2>/dev/null || true
            
            if command -v idf.py &> /dev/null; then
                IDF_VERSION=$(idf.py --version 2>/dev/null | head -1)
                print_success "ESP-IDF环境已就绪: $IDF_VERSION"
                return 0
            fi
        fi
    done
    
    print_error "无法加载ESP-IDF环境!"
    echo ""
    echo "请先安装ESP-IDF:"
    echo ""
    echo "  # 方法1: 使用ESP-IDF安装器"
    echo "  https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/get-started/index.html"
    echo ""
    echo "  # 方法2: 手动安装"
    echo "  mkdir -p ~/esp"
    echo "  cd ~/esp"
    echo "  git clone --recursive https://github.com/espressif/esp-idf.git --branch v5.4"
    echo "  cd esp-idf"
    echo "  ./install.sh esp32s3"
    echo ""
    echo "  # 设置环境变量 (每次使用前执行)"
    echo "  source ~/esp/esp-idf/export.sh"
    echo ""
    exit 1
}

# 检测ESP32设备
detect_device() {
    print_info "检测ESP32设备..."
    
    # 如果用户指定了端口，直接使用
    if [ -n "$2" ] && [ "$1" != "build" ]; then
        PORT="$2"
        print_info "使用指定端口: $PORT"
        return
    fi
    
    # 自动检测
    PORT=""
    
    # macOS
    if [[ "$OSTYPE" == "darwin"* ]]; then
        PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
        if [ -z "$PORT" ]; then
            PORT=$(ls /dev/cu.usbserial* 2>/dev/null | head -1)
        fi
    # Linux
    elif [[ "$OSTYPE" == "linux"* ]]; then
        PORT=$(ls /dev/ttyUSB* 2>/dev/null | head -1)
        if [ -z "$PORT" ]; then
            PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)
        fi
    fi
    
    if [ -z "$PORT" ]; then
        print_error "未检测到ESP32设备!"
        print_info "请检查:"
        echo "  1. USB线是否支持数据传输"
        echo "  2. 开发板是否正确连接并通电"
        echo "  3. 是否需要安装USB驱动"
        exit 1
    fi
    
    print_success "检测到设备: $PORT"
}

# 进度条显示函数
show_progress() {
    local current=$1
    local total=$2
    local width=50
    local percentage=$((current * 100 / total))
    local filled=$((width * current / total))
    local empty=$((width - filled))
    
    printf "\r[%s%s] %3d%%" \
        "$(printf '#%.0s' $(seq 1 $filled 2>/dev/null) 2>/dev/null || echo "")" \
        "$(printf '.%.0s' $(seq 1 $empty 2>/dev/null) 2>/dev/null || echo "")" \
        $percentage
}

# 编译固件
build_firmware() {
    print_info "开始编译固件..."
    print_info "目标开发板: $BOARD_NAME"
    print_info "开发板类型: $BOARD_TYPE"
    
    cd "$PROJECT_DIR"
    
    # 清理旧配置
    print_info "清理旧配置..."
    python3 "$IDF_PATH/tools/idf.py" fullclean 2>&1 | grep -v "WARNING" || true
    
    # 设置开发板类型 - 使用 CMake 参数强制选择正确的开发板
    print_info "执行编译 (显示详细输出)..."
    echo ""
    
    # 编译并显示详细输出
    python3 "$IDF_PATH/tools/idf.py" -DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y build 2>&1 | while IFS= read -r line; do
        echo "  $line"
        
        # 检测编译进度
        if echo "$line" | grep -q "Building"; then
            printf "  ${YELLOW}[编译中]${NC} %s\n" "$line"
        elif echo "$line" | grep -q "Linking"; then
            printf "  ${BLUE}[链接中]${NC} %s\n" "$line"
        elif echo "$line" | grep -q "Built target"; then
            printf "  ${GREEN}[完成]${NC} %s\n" "$line"
        fi
    done
    
    # 合并固件
    print_info "合并固件..."
    python3 "$IDF_PATH/tools/idf.py" merge-bin 2>&1 | grep -v "WARNING" || true
    
    # 检查固件是否生成
    if [ -f "build/merged-binary.bin" ]; then
        FIRMWARE_SIZE=$(ls -lh build/merged-binary.bin | awk '{print $5}')
        print_success "固件编译成功! 大小: $FIRMWARE_SIZE"
        print_success "固件路径: $PROJECT_DIR/build/merged-binary.bin"
    else
        print_error "固件生成失败!"
        exit 1
    fi
}

# 烧录固件
flash_firmware() {
    print_info "开始烧录固件..."
    print_info "目标设备: $PORT"
    print_info "开发板: $BOARD_NAME"
    
    cd "$PROJECT_DIR"
    
    # 检查固件是否存在
    if [ ! -f "build/merged-binary.bin" ]; then
        print_error "固件不存在，请先编译!"
        print_info "运行: ./build_and_flash.sh build"
        exit 1
    fi
    
    # 获取固件大小用于显示进度
    FIRMWARE_SIZE=$(ls -lh build/merged-binary.bin | awk '{print $5}')
    print_info "固件大小: $FIRMWARE_SIZE"
    print_info "正在烧录..."
    echo ""
    
    # 烧录固件并显示进度
    python3 "$IDF_PATH/tools/idf.py" -p "$PORT" flash 2>&1 | while IFS= read -r line; do
        echo "  $line"
        
        # 检测烧录进度
        if echo "$line" | grep -qE "Writing at 0x[0-9a-f]+"; then
            # 提取烧录进度百分比
            percentage=$(echo "$line" | grep -oE '\([0-9]+ %\)' | tr -d '()' || echo "")
            if [ -n "$percentage" ]; then
                printf "  ${YELLOW}[烧录中]${NC} %s\n" "$line"
            else
                printf "  ${BLUE}[写入]${NC} %s\n" "$line"
            fi
        elif echo "$line" | grep -q "Hash of data verified"; then
            printf "  ${GREEN}[验证]${NC} %s\n" "$line"
        elif echo "$line" | grep -q "Leaving"; then
            printf "  ${GREEN}[完成]${NC} %s\n" "$line"
        elif echo "$line" | grep -q "Hard resetting"; then
            printf "  ${GREEN}[复位]${NC} %s\n" "$line"
        fi
    done
    
    echo ""
    print_success "固件烧录完成!"
    print_info "设备正在重启..."
}

# 监视串口输出
monitor_device() {
    print_info "启动串口监视..."
    print_info "按 Ctrl+] 退出监视"
    
    cd "$PROJECT_DIR"
    idf.py -p "$PORT" monitor
}

# 显示帮助信息
show_help() {
    echo ""
    echo "Waveshare ESP32-S3-Touch-LCD-1.85B 编译和烧录脚本"
    echo ""
    echo "使用方法:"
    echo "  $0 [action] [port]"
    echo ""
    echo "参数:"
    echo "  action  - 操作类型 (默认: all)"
    echo "            build   - 仅编译固件"
    echo "            flash   - 仅烧录固件"
    echo "            monitor - 仅监视串口"
    echo "            all     - 编译、烧录并监视"
    echo "  port    - 串口设备 (默认: 自动检测)"
    echo ""
    echo "示例:"
    echo "  $0                    # 编译并烧录"
    echo "  $0 build              # 仅编译"
    echo "  $0 flash              # 仅烧录 (需先编译)"
    echo "  $0 monitor            # 仅监视串口"
    echo "  $0 all                # 编译、烧录并监视"
    echo "  $0 flash /dev/cu.usbmodem1101  # 指定端口烧录"
    echo ""
    echo "前置要求:"
    echo "  ESP-IDF v5.4+ 开发环境"
    echo "  安装方法: source ~/esp/esp-idf/export.sh"
    echo ""
}

# 主函数
main() {
    echo ""
    echo "=========================================="
    echo "  Waveshare ESP32-S3-Touch-LCD-1.85B"
    echo "  编译和烧录工具"
    echo "=========================================="
    echo ""
    
    # 解析参数
    ACTION="${1:-all}"
    
    # 显示帮助
    if [ "$ACTION" == "help" ] || [ "$ACTION" == "-h" ] || [ "$ACTION" == "--help" ]; then
        show_help
        exit 0
    fi
    
    # 检查环境
    check_idf_env
    
    # 检测设备（烧录和监视时需要）
    if [ "$ACTION" == "flash" ] || [ "$ACTION" == "monitor" ] || [ "$ACTION" == "all" ]; then
        detect_device "$ACTION" "$2"
    fi
    
    # 执行操作
    case "$ACTION" in
        "build")
            build_firmware
            ;;
        "flash")
            flash_firmware
            ;;
        "monitor")
            monitor_device
            ;;
        "all")
            build_firmware
            flash_firmware
            print_success "编译和烧录完成!"
            print_info "启动串口监视 (按 Ctrl+] 退出)..."
            monitor_device
            ;;
        *)
            print_error "未知操作: $ACTION"
            show_help
            exit 1
            ;;
    esac
}

# 运行主函数
main "$1" "$2"