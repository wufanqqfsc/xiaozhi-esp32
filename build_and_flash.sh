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
CYAN='\033[0;36m'
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

print_progress() {
    echo -e "${CYAN}[PROGRESS]${NC} $1"
}

# 配置 - ESP32-S3-Touch-LCD-1.85B 版本（不带TCA9554 IO扩展器）
BOARD_TYPE="waveshare/esp32-s3-touch-lcd-1.85b"
BOARD_NAME="Waveshare ESP32-S3-Touch-LCD-1.85B"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

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
        print_info "IDF_PATH: $IDF_PATH"
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
                print_info "IDF_PATH: $IDF_PATH"
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
        echo ""
        print_info "可用串口列表:"
        ls /dev/cu.* 2>/dev/null | head -10 || echo "  无可用串口"
        exit 1
    fi

    print_success "检测到设备: $PORT"

    # 测试设备连接
    print_info "测试设备连接..."
    if python3 -c "import serial; ser = serial.Serial('$PORT', 115200, timeout=1); ser.close(); print('OK')" 2>/dev/null; then
        print_success "设备连接正常"
    else
        print_warning "设备可能需要重新插拔"
    fi
}

# 获取本机局域网 IP（macOS/Linux 通用，优先默认路由出口网卡）
get_local_ip() {
    local ip=""
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # 先取默认路由的出口网卡（en0/en1 等），再读 IP；避免误用非活动网卡
        local iface
        iface=$(route -n get default 2>/dev/null | awk '/interface:/{print $2}' | head -1)
        if [ -n "$iface" ]; then
            ip=$(ipconfig getifaddr "$iface" 2>/dev/null)
        fi
        if [ -z "$ip" ]; then
            ip=$(ipconfig getifaddr en0 2>/dev/null)
        fi
        if [ -z "$ip" ]; then
            ip=$(ipconfig getifaddr en1 2>/dev/null)
        fi
    elif [[ "$OSTYPE" == "linux"* ]]; then
        # 优先从默认路由拿出口 IP
        ip=$(ip route get 1.1.1.1 2>/dev/null | awk '/src/{print $7; exit}')
        if [ -z "$ip" ]; then
            ip=$(hostname -I 2>/dev/null | awk '{print $1}')
        fi
    fi
    echo "$ip"
}

# 把 sdkconfig 中所有后端 URL 的 host 同步为本机最新 IP
# 用法：update_backend_host <NEW_IP>
# 备份 sdkconfig 到 sdkconfig.bak.hostip
update_backend_host() {
    local NEW_IP="$1"
    local SDKCONFIG="$PROJECT_DIR/sdkconfig"
    [ -z "$NEW_IP" ] && { print_warning "update_backend_host: NEW_IP 为空"; return 1; }

    if [ ! -f "$SDKCONFIG" ]; then
        print_warning "sdkconfig 不存在: $SDKCONFIG，跳过 host 更新"
        return 1
    fi

    # 1) 当前 sdkconfig 中已配置的 host（取 CONFIG_OTA_URL 的 host 部分）
    local CURRENT_OTA_HOST
    CURRENT_OTA_HOST=$(grep -E '^CONFIG_OTA_URL=' "$SDKCONFIG" \
        | head -1 \
        | sed -E 's|^CONFIG_OTA_URL="[^/]*//([^:/"]+).*|\1|')
    if [ -z "$CURRENT_OTA_HOST" ]; then
        CURRENT_OTA_HOST="(empty)"
    fi

    if [ "$CURRENT_OTA_HOST" = "$NEW_IP" ]; then
        print_success "后端 host 已是最新: $NEW_IP，无需更新"
        return 0
    fi

    # 2) 备份
    cp "$SDKCONFIG" "$SDKCONFIG.bak.hostip"

    # 3) 替换 CONFIG_OTA_URL / CONFIG_LOCAL_WEBSOCKET_URL 中 //host: 中的 host
    #    匹配模式: //<old_host>:<port>  ->  //<new_ip>:<port>
    sed -i.tmp -E "s|^(CONFIG_OTA_URL=\"[a-zA-Z]+://)[^:\"]+(:[^\"]+\")|\1${NEW_IP}\2|" "$SDKCONFIG"
    sed -i.tmp -E "s|^(CONFIG_LOCAL_WEBSOCKET_URL=\"[a-zA-Z]+://)[^:\"]+(:[^\"]+\")|\1${NEW_IP}\2|" "$SDKCONFIG"
    rm -f "$SDKCONFIG.tmp"

    # 4) 校验
    local AFTER_OTA_HOST
    AFTER_OTA_HOST=$(grep -E '^CONFIG_OTA_URL=' "$SDKCONFIG" \
        | head -1 \
        | sed -E 's|^CONFIG_OTA_URL="[^/]*//([^:/"]+).*|\1|')
    if [ "$AFTER_OTA_HOST" != "$NEW_IP" ]; then
        print_error "后端 host 替换失败! 期望: $NEW_IP, 实际: $AFTER_OTA_HOST"
        # 回滚
        mv "$SDKCONFIG.bak.hostip" "$SDKCONFIG"
        return 1
    fi

    print_success "后端 host 已更新: $CURRENT_OTA_HOST -> $NEW_IP"
    grep -E "^CONFIG_OTA_URL|^CONFIG_LOCAL_WEBSOCKET_URL" "$SDKCONFIG"
}

# 编译固件
build_firmware() {
    echo ""
    print_progress "开始编译固件..."
    print_info "目标开发板: $BOARD_NAME"
    print_info "开发板类型: $BOARD_TYPE"
    print_info "项目目录: $PROJECT_DIR"
    echo ""

    cd "$PROJECT_DIR"

    # 清理旧配置
    print_progress "清理旧配置..."
    echo "----------------------------------------"
    if [ -d "build" ]; then
        python3 "$IDF_PATH/tools/idf.py" fullclean 2>&1 | sed 's/\r$//' || true
    else
        echo "(build 目录不存在，跳过清理)"
    fi
    echo "----------------------------------------"
    print_success "清理完成"
    echo ""

    # 通过 sed 修改 sdkconfig 来切换 board 类型
    # 重要：idf.py -D 参数是 CMake 变量，不会影响 Kconfig，
    # 真正控制 board 编译目标的是 sdkconfig 里的 CONFIG_BOARD_TYPE_*
    print_progress "切换 sdkconfig 到目标 board..."
    echo "----------------------------------------"
    local BOARD_KCONFIG_NAME
    # bread-compact-wifi -> BREAD_COMPACT_WIFI
    # waveshare/esp32-s3-touch-lcd-1.85b -> WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B
    BOARD_KCONFIG_NAME=$(echo "$BOARD_TYPE" | tr 'a-z-' 'A-Z_' | tr '/' '_')
    # 把所有 BOARD_TYPE 设为 not set（避免互斥冲突），再启用目标
    sed -i.bak -E "s/^(CONFIG_BOARD_TYPE_[A-Z0-9_]+)=y$/# \1 is not set/" sdkconfig
    sed -i.bak -E "s/^# (CONFIG_BOARD_TYPE_${BOARD_KCONFIG_NAME}) is not set$/\1=y/" sdkconfig
    grep -E "^CONFIG_BOARD_TYPE" sdkconfig | head -3
    echo "----------------------------------------"
    print_success "Board 配置已切换"
    echo ""

    # 同步 sdkconfig 中的后端 host 为本机最新 IP
    # 避免后端机器换了 IP 后烧录出来的固件连不上 OTA/WebSocket
    print_progress "同步 sdkconfig 后端 host 为本机最新 IP..."
    echo "----------------------------------------"
    BACKEND_IP=$(get_local_ip)
    if [ -n "$BACKEND_IP" ]; then
        print_info "本机 IP: $BACKEND_IP"
        if ! update_backend_host "$BACKEND_IP"; then
            print_warning "后端 host 同步失败，继续编译（使用 sdkconfig 现有 host）"
        fi
    else
        print_warning "无法获取本机 IP，跳过后端 host 同步"
    fi
    echo "----------------------------------------"
    print_success "后端 host 同步完成"
    echo ""

    # 设置开发板类型 - 使用 CMake 参数强制选择正确的开发板
    print_progress "开始编译固件..."
    echo "----------------------------------------"

    # 编译固件 (去除回车符避免格式混乱)
    python3 "$IDF_PATH/tools/idf.py" -DBOARD_TYPE_WAVESHARE_ESP32_S3_TOUCH_LCD_1_85B=y build 2>&1 | sed 's/\r$//'
    BUILD_RESULT=${PIPESTATUS[0]}

    echo "----------------------------------------"
    print_success "编译完成"
    echo ""

    # 合并固件
    print_progress "合并固件..."
    echo "----------------------------------------"
    python3 "$IDF_PATH/tools/idf.py" merge-bin 2>&1 | sed 's/\r$//'
    echo "----------------------------------------"
    print_success "固件合并完成"
    echo ""

    # 检查固件是否生成
    if [ -f "build/merged-binary.bin" ]; then
        FIRMWARE_SIZE=$(ls -lh build/merged-binary.bin | awk '{print $5}')
        BINARY_SIZE=$(ls -lh build/xiaozhi.bin | awk '{print $5}')
        print_success "固件编译成功!"
        print_info "合并固件大小: $FIRMWARE_SIZE"
        print_info "应用固件大小: $BINARY_SIZE"
        print_info "固件路径: $PROJECT_DIR/build/merged-binary.bin"
    else
        print_error "固件生成失败!"
        exit 1
    fi

    # 显示编译产物信息
    echo ""
    print_info "编译产物列表:"
    ls -la build/*.bin 2>/dev/null || echo "  无bin文件"
    ls -la build/bootloader/*.bin 2>/dev/null || echo "  无bootloader文件"
    ls -la build/partition_table/*.bin 2>/dev/null || echo "  无分区表文件"
}

# 烧录固件
flash_firmware() {
    echo ""
    print_progress "开始烧录固件..."
    print_info "目标设备: $PORT"
    print_info "开发板: $BOARD_NAME"
    echo ""
    
    cd "$PROJECT_DIR"
    
    # 检查固件是否存在
    if [ ! -f "build/merged-binary.bin" ]; then
        print_error "固件不存在，请先编译!"
        print_info "运行: ./build_and_flash.sh build"
        exit 1
    fi
    
    # 获取固件大小
    FIRMWARE_SIZE=$(ls -lh build/merged-binary.bin | awk '{print $5}')
    print_info "固件大小: $FIRMWARE_SIZE"
    print_progress "正在烧录..."
    echo "----------------------------------------"
    
    # 烧录固件 (去除回车符避免格式混乱)
    python3 "$IDF_PATH/tools/idf.py" -p "$PORT" flash 2>&1 | sed 's/\r$//'
    BUILD_RESULT=${PIPESTATUS[0]}
    
    echo "----------------------------------------"
    echo ""
    print_success "固件烧录完成!"
    print_info "设备正在重启..."
    echo ""
}

# 监视串口输出
monitor_device() {
    echo ""
    print_progress "启动串口监视..."
    print_info "设备端口: $PORT"
    print_info "波特率: 115200"
    print_info "按 Ctrl+] 退出监视"
    echo "----------------------------------------"
    
    cd "$PROJECT_DIR"
    python3 "$IDF_PATH/tools/idf.py" -p "$PORT" monitor
}

# 显示帮助信息
show_help() {
    echo ""
    echo "Waveshare ESP32-S3-Touch-LCD-1.85B 编译和烧录脚本"
    echo "=================================================="
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
    clear
    echo "=================================================="
    echo "  Waveshare ESP32-S3-Touch-LCD-1.85B"
    echo "      编译和烧录工具 v1.0"
    echo "=================================================="
    echo ""
    echo "当前时间: $(date "+%Y-%m-%d %H:%M:%S")"
    echo "项目目录: $PROJECT_DIR"
    echo ""
    
    # 解析参数
    ACTION="${1:-all}"
    
    # 显示帮助
    if [ "$ACTION" == "help" ] || [ "$ACTION" == "-h" ] || [ "$ACTION" == "--help" ]; then
        show_help
        exit 0
    fi
    
    # 检查环境
    print_progress "步骤1/4: 检查ESP-IDF环境"
    check_idf_env
    echo ""
    
    # 检测设备（烧录和监视时需要）
    if [ "$ACTION" == "flash" ] || [ "$ACTION" == "monitor" ] || [ "$ACTION" == "all" ]; then
        print_progress "步骤2/4: 检测ESP32设备"
        detect_device "$ACTION" "$2"
        echo ""
    fi
    
    # 执行操作
    case "$ACTION" in
        "build")
            print_progress "步骤2/2: 编译固件"
            build_firmware
            ;;
        "flash")
            print_progress "步骤3/4: 烧录固件"
            flash_firmware
            ;;
        "monitor")
            print_progress "步骤3/3: 启动串口监视"
            monitor_device
            ;;
        "all")
            print_progress "步骤3/4: 编译固件"
            build_firmware
            echo ""
            print_progress "步骤4/4: 烧录固件"
            flash_firmware
            echo ""
            print_success "编译和烧录完成!"
            echo ""
            print_info "启动串口监视 (按 Ctrl+] 退出)..."
            monitor_device
            ;;
        *)
            print_error "未知操作: $ACTION"
            show_help
            exit 1
            ;;
    esac
    
    echo ""
    print_success "操作完成!"
}

# 运行主函数
main "$1" "$2"
