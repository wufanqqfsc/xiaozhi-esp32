#!/usr/bin/env python3
"""
Jarvis 语音交互端到端自动化测试脚本

功能：
  1. 检查设备状态（WiFi/HTTP API）
  2. 使用 macOS say 命令播放唤醒词 "贾维斯"
  3. 监控设备日志和后端日志，验证：
     - 角色回复是否正确（贾维斯/Jarvis）
     - JARVIS 视图切换是否有白屏
  4. 输出测试报告
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
from datetime import datetime

# ======== 配置 ========
DEVICE_IP = "192.168.3.22"
DEVICE_HTTP_PORT = 8080
BACKEND_IP = "192.168.3.32"
BACKEND_REST_PORT = 8091
BACKEND_WS_PORT = 8092
SERVER_JAVA_DIR = os.path.expanduser(
    "~/Desktop/cv/github/OpenMAIC/xiaozhi-esp32-server-java"
)
DEVICE_ID = "a0:f2:62:e4:3a:40"
ROLE_NAME_EXPECTED = ["贾维斯", "Jarvis", "jarvis"]
ROLE_NAME_WRONG = ["小何"]

# 唤醒词和测试对话
WAKE_WORD = "贾维斯"
TEST_QUESTION = "你好，你是谁？"

# 超时设置
WAIT_WAKEUP_SEC = 8
WAIT_REPLY_SEC = 15
WAIT_STT_SEC = 10


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


def log_info(msg):
    print(f"{Colors.CYAN}[INFO]{Colors.RESET} {msg}")


def log_ok(msg):
    print(f"{Colors.GREEN}[PASS]{Colors.RESET} {msg}")


def log_fail(msg):
    print(f"{Colors.RED}[FAIL]{Colors.RESET} {msg}")


def log_warn(msg):
    print(f"{Colors.YELLOW}[WARN]{Colors.RESET} {msg}")


def log_step(step, msg):
    print(f"\n{Colors.BOLD}=== 步骤 {step}: {msg} ==={Colors.RESET}\n")


def http_get(url, timeout=5):
    try:
        req = urllib.request.Request(url)
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read().decode("utf-8", errors="replace")
    except Exception as e:
        return None


def device_api(path):
    return http_get(f"http://{DEVICE_IP}:{DEVICE_HTTP_PORT}{path}")


def check_device_status():
    log_step(1, "检查设备状态")
    status = device_api("/api/device/status")
    if not status:
        log_fail("无法连接设备 HTTP API")
        return False
    try:
        data = json.loads(status)
    except Exception:
        log_fail(f"设备状态解析失败: {status[:200]}")
        return False

    wifi_ok = data.get("wifi_connected", False)
    http_ok = data.get("http_running", False)
    free_heap = data.get("memory", {}).get("free_heap", 0)
    uptime = data.get("uptime_seconds", 0)

    if wifi_ok:
        log_ok(f"WiFi 已连接")
    else:
        log_fail("WiFi 未连接")
        return False

    if http_ok:
        log_ok(f"HTTP 服务运行中 (端口 {DEVICE_HTTP_PORT})")
    else:
        log_fail("HTTP 服务未运行")
        return False

    log_info(f"可用内存: {free_heap} 字节")
    log_info(f"运行时间: {uptime} 秒")
    return True


def get_latest_log_filename():
    logs_list = device_api("/api/sdcard/logs")
    if not logs_list:
        return None
    try:
        logs = json.loads(logs_list)
        if not logs:
            return None
        logs.sort(key=lambda x: x.get("mtime", 0), reverse=True)
        return logs[0]["name"]
    except Exception:
        return None


def get_device_log_lines(since_pattern=None, timeout=3):
    """获取设备最新日志内容"""
    log_name = get_latest_log_filename()
    if not log_name:
        return ""
    content = device_api(f"/api/sdcard/logs/{log_name}")
    if not content:
        return ""
    lines = content.split("\n")
    return lines


def speak(text, rate=180):
    """使用 macOS say 命令播放语音"""
    log_info(f"播放语音: '{text}'")
    try:
        subprocess.run(
            ["say", "-r", str(rate), text],
            check=True,
            timeout=10,
        )
        return True
    except subprocess.TimeoutExpired:
        log_warn("语音播放超时")
        return False
    except Exception as e:
        log_fail(f"语音播放失败: {e}")
        return False


def trigger_screenshot(label=""):
    """触发设备截图（异步）"""
    try:
        req = urllib.request.Request(
            f"http://{DEVICE_IP}:{DEVICE_HTTP_PORT}/api/sdcard/shots",
            method="POST",
        )
        urllib.request.urlopen(req, timeout=3)
        log_info(f"截图已触发 {label}")
        return True
    except Exception as e:
        log_warn(f"截图触发失败: {e}")
        return False


def check_wakeup_in_log(lines):
    """检查日志中是否有唤醒记录"""
    for line in lines:
        if "WAKE_WORD_DETECTED" in line or "wake word" in line.lower() or "wakeup" in line.lower():
            return True, line.strip()
    return False, ""


def check_jarvis_view_in_log(lines):
    """检查 JARVIS 视图切换日志，返回 (show_ok, hide_ok, white_screen_error"""
    show_ok = False
    hide_ok = False
    white_screen = False
    show_line = ""
    hide_line = ""
    for line in lines:
        if "ShowJarvisWatchface" in line:
            show_ok = True
            show_line = line.strip()
        if "HideJarvisWatchface" in line:
            hide_ok = True
            hide_line = line.strip()
        if "white" in line.lower() and "screen" in line.lower():
            white_screen = True
    return show_ok, hide_ok, white_screen, show_line, hide_line


def check_backend_dialogue_log(lines_back):
    """检查后端对话日志中的 LLM 回复文本"""
    stt_text = ""
    llm_reply_text = ""
    has_tts_start = False
    websocket_connected = False

    for line in lines_back:
        if DEVICE_ID.lower() in line.lower():
            websocket_connected = True
        if '"type":"stt"' in line or '"type": "stt"' in line:
            m = re.search(r'"text"\s*:\s*"([^"]+)"', line)
            if m:
                stt_text = m.group(1)
        if '"state":"sentence_start"' in line or '"state": "sentence_start"' in line:
            m = re.search(r'"text"\s*:\s*"([^"]+)"', line)
            if m:
                llm_reply_text += m.group(1) + " "
        if '"state":"start"' in line or '"state": "start"' in line:
            has_tts_start = True
    return websocket_connected, stt_text.strip(), llm_reply_text.strip(), has_tts_start


def read_backend_log_tail(lines=100):
    """读取后端对话日志末尾"""
    log_path = os.path.join(SERVER_JAVA_DIR, "logs", "xiaozhi-dialogue.log")
    if not os.path.exists(log_path):
        return []
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            all_lines = f.readlines()
        return all_lines[-lines:]
    except Exception as e:
        log_warn(f"读取后端日志失败: {e}")
        return []


def check_role_correct(reply_text):
    """检查角色名是否正确"""
    reply_lower = reply_text.lower()
    for wrong in ROLE_NAME_WRONG:
        if wrong in reply_text:
            return False, f"回复中包含错误角色名: {wrong}"
    for expected in ROLE_NAME_EXPECTED:
        if expected.lower() in reply_lower:
            return True, f"回复中包含正确角色名: {expected}"
    return None, "回复中未检测到角色自称"


def query_db_last_message(limit=5):
    """查询数据库中最新的对话消息"""
    try:
        result = subprocess.run(
            [
                "docker", "exec", "xiaozhi-mysql",
                "mysql", "-uxiaozhi", "-p123456", "xiaozhi",
                "-e",
                f"SELECT messageId, sender, LEFT(message, 150) as msg FROM sys_message "
                f"WHERE deviceId='{DEVICE_ID}' AND state='1' "
                f"ORDER BY messageId DESC LIMIT {limit};",
            ],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.stdout.strip()
    except Exception as e:
        log_warn(f"数据库查询失败: {e}")
        return ""


def get_latest_assistant_reply():
    """从数据库获取最新的 assistant 消息"""
    try:
        result = subprocess.run(
            [
                "docker", "exec", "xiaozhi-mysql",
                "mysql", "-N", "-B", "-uxiaozhi", "-p123456", "xiaozhi",
                "-e",
                f"SELECT message FROM sys_message "
                f"WHERE deviceId='{DEVICE_ID}' AND state='1' AND sender='assistant' "
                f"ORDER BY messageId DESC LIMIT 1;",
            ],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return result.stdout.strip()
    except Exception as e:
        log_warn(f"获取最新回复失败: {e}")
        return ""


def main():
    global DEVICE_IP, BACKEND_IP, WAKE_WORD, TEST_QUESTION
    parser = argparse.ArgumentParser(description="Jarvis 端到端自动化测试")
    parser.add_argument("--device-ip", default=DEVICE_IP, help="设备 IP")
    parser.add_argument("--backend-ip", default=BACKEND_IP, help="后端 IP")
    parser.add_argument("--wake-word", default=WAKE_WORD, help="唤醒词")
    parser.add_argument("--question", default=TEST_QUESTION, help="测试问题")
    parser.add_argument("--no-speak", action="store_true", help="不播放语音，仅检查日志")
    parser.add_argument("--rate", type=int, default=170, help="语音速率")
    args = parser.parse_args()

    DEVICE_IP = args.device_ip
    BACKEND_IP = args.backend_ip
    WAKE_WORD = args.wake_word
    TEST_QUESTION = args.question

    print(f"\n{Colors.BOLD}{'='*60}")
    print(f"  Jarvis 语音交互端到端自动化测试")
    print(f"{'='*60}{Colors.RESET}")
    print(f"  设备 IP:    {DEVICE_IP}")
    print(f"  后端 IP:    {BACKEND_IP}")
    print(f"  唤醒词:     {WAKE_WORD}")
    print(f"  测试问题:   {TEST_QUESTION}")
    print(f"  设备 ID:   {DEVICE_ID}")
    print()

    results = {}

    # 步骤 1: 设备状态检查
    if not check_device_status():
        log_fail("设备状态检查失败，终止测试")
        sys.exit(1)

    results["device_status"] = True

    # 步骤 2: 记录初始日志位置
    log_step(2, "记录初始状态")
    log_name_before = get_latest_log_filename()
    backend_log_before = len(read_backend_log_tail(999999)) if os.path.exists(
        os.path.join(SERVER_JAVA_DIR, "logs", "xiaozhi-dialogue.log")
    ) else 0
    log_info(f"当前设备日志: {log_name_before}")
    log_info(f"后端日志行数: {backend_log_before}")
    trigger_screenshot("(唤醒前)")
    log_ok("初始状态已记录")

    if args.no_speak:
        log_warn("--no-speak 模式，跳过语音播放")
    else:
        # 步骤 3: 播放唤醒词
        log_step(3, "播放唤醒词")
        log_info("请确保设备麦克风靠近扬声器...")
        time.sleep(1)
        speak_ok = speak(WAKE_WORD, rate=args.rate)
        if not speak_ok:
            log_fail("唤醒词播放失败")
            results["wakeup_play"] = False
        else:
            log_ok("唤醒词已播放")
            results["wakeup_play"] = True

        # 等待唤醒响应
        log_info(f"等待 {WAIT_WAKEUP_SEC} 秒，等待设备唤醒...")
        time.sleep(WAIT_WAKEUP_SEC)

        # 步骤 4: 检查唤醒是否成功
        log_step(4, "检查唤醒结果")
        device_lines = get_device_log_lines()
        wakeup_ok, wakeup_line = check_wakeup_in_log(device_lines)
        if wakeup_ok:
            log_ok(f"设备已唤醒: {wakeup_line}")
            results["wakeup_detected"] = True
        else:
            log_fail("设备日志中未检测到唤醒事件")
            results["wakeup_detected"] = False

        # 检查 JARVIS 视图显示
        show_ok, hide_ok, ws_err, show_line, hide_line = check_jarvis_view_in_log(device_lines)
        if show_ok:
            log_ok(f"JARVIS 视图已显示: {show_line}")
            results["jarvis_view_show"] = True
        else:
            log_warn("未检测到 JARVIS 视图显示日志")
            results["jarvis_view_show"] = False

        trigger_screenshot("(唤醒后)")

        # 步骤 5: 播放测试问题
        log_step(5, "播放测试问题")
        speak_ok = speak(TEST_QUESTION, rate=args.rate)
        if not speak_ok:
            log_fail("测试问题播放失败")
            results["question_play"] = False
        else:
            log_ok("测试问题已播放")
            results["question_play"] = True

        # 等待回复
        log_info(f"等待 {WAIT_REPLY_SEC} 秒，等待 AI 回复...")
        time.sleep(WAIT_REPLY_SEC)

    # 步骤 6: 验证后端日志
    log_step(6, "检查后端对话日志")
    backend_lines = read_backend_log_tail(200)
    ws_ok, stt_text, llm_reply, tts_start = check_backend_dialogue_log(backend_lines)

    if ws_ok:
        log_ok("设备 WebSocket 已连接后端")
        results["websocket_connected"] = True
    else:
        log_warn("后端日志中未检测到设备 WebSocket 连接")
        results["websocket_connected"] = False

    if stt_text:
        log_ok(f"STT 识别结果: {stt_text}")
        results["stt_ok"] = True
        results["stt_text"] = stt_text
    else:
        log_warn("未检测到 STT 识别文本")
        results["stt_ok"] = False

    if llm_reply:
        log_ok(f"LLM 回复文本: {llm_reply[:100]}...")
        results["llm_reply_ok"] = True
        results["llm_reply"] = llm_reply
    else:
        log_warn("未检测到 LLM 回复文本")
        results["llm_reply_ok"] = False

    # 步骤 7: 角色验证
    log_step(7, "验证角色一致性")
    role_ok = None
    role_msg = ""
    if llm_reply:
        role_ok, role_msg = check_role_correct(llm_reply)
        if role_ok is True:
            log_ok(f"角色正确: {role_msg}")
            results["role_correct"] = True
        elif role_ok is False:
            log_fail(f"角色错误: {role_msg}")
            results["role_correct"] = False
        else:
            log_warn(role_msg)
            results["role_correct"] = None
    else:
        log_warn("无 LLM 回复文本，跳过角色验证")
        results["role_correct"] = None

    # 步骤 8: 数据库对话历史验证
    log_step(8, "数据库对话历史验证")
    db_msgs = query_db_last_message()
    if db_msgs:
        log_info("最近 5 条对话:")
        for line in db_msgs.split("\n"):
            if line.strip():
                print(f"    {line}")
        results["db_check"] = True
    else:
        log_warn("未查询到对话记录")
        results["db_check"] = False

    # 步骤 8.5: 基于数据库的角色验证
    log_step(9, "角色一致性验证（数据库）")
    db_reply = get_latest_assistant_reply()
    if db_reply:
        log_info(f"最新助手回复（前150字）: {db_reply[:150]}")
        role_ok_db, role_msg_db = check_role_correct(db_reply)
        if role_ok_db is True:
            log_ok(f"角色正确: {role_msg_db}")
            results["role_correct_db"] = True
        elif role_ok_db is False:
            log_fail(f"角色错误: {role_msg_db}")
            results["role_correct_db"] = False
        else:
            log_warn(role_msg_db)
            results["role_correct_db"] = None
    else:
        log_warn("数据库中无助手回复")
        results["role_correct_db"] = None

    # 步骤 10: JARVIS 视图白屏检查
    log_step(10, "JARVIS 视图白屏检查")
    device_lines = get_device_log_lines()
    show_ok, hide_ok, ws_err, show_line, hide_line = check_jarvis_view_in_log(device_lines)

    if show_ok and hide_ok:
        log_ok("JARVIS 视图显示和隐藏均有日志记录")
        results["view_transition"] = True
    elif show_ok:
        log_warn("仅检测到视图显示，未检测到视图隐藏（可能仍在交互中）")
        results["view_transition"] = "partial"
    else:
        log_warn("未检测到 JARVIS 视图切换日志")
        results["view_transition"] = False

    if ws_err:
        log_fail("检测到白屏相关错误")
        results["white_screen"] = True
    else:
        log_ok("未检测到白屏相关错误日志")
        results["white_screen"] = False

    trigger_screenshot("(测试结束)")

    # 总结报告
    print(f"\n{Colors.BOLD}{'='*60}")
    print(f"  测试报告")
    print(f"{'='*60}{Colors.RESET}\n")

    total = 0
    passed = 0
    failed = 0
    warnings = 0

    test_items = [
        ("设备状态", "device_status"),
        ("唤醒词播放", "wakeup_play"),
        ("唤醒检测", "wakeup_detected"),
        ("JARVIS 视图显示", "jarvis_view_show"),
        ("测试问题播放", "question_play"),
        ("WebSocket 连接", "websocket_connected"),
        ("STT 识别", "stt_ok"),
        ("LLM 回复", "llm_reply_ok"),
        ("角色正确（日志）", "role_correct"),
        ("角色正确（数据库）", "role_correct_db"),
        ("数据库记录", "db_check"),
        ("视图切换", "view_transition"),
        ("白屏检测", "white_screen"),
    ]

    for label, key in test_items:
        if key not in results:
            continue
        val = results[key]
        total += 1
        if val is True:
            passed += 1
            print(f"  {Colors.GREEN}[PASS]{Colors.RESET} {label}")
        elif val is False:
            if key == "white_screen":
                passed += 1
                print(f"  {Colors.GREEN}[PASS]{Colors.RESET} {label} (无白屏)")
            else:
                failed += 1
                print(f"  {Colors.RED}[FAIL]{Colors.RESET} {label}")
        else:
            warnings += 1
            print(f"  {Colors.YELLOW}[WARN]{Colors.RESET} {label}")

    print(f"\n{Colors.BOLD}总计: {total} 项, 通过: {passed}, 失败: {failed}, 警告: {warnings}{Colors.RESET}\n")

    if failed == 0:
        print(f"{Colors.GREEN}测试通过！{Colors.RESET}")
        return 0
    else:
        print(f"{Colors.RED}测试失败！{Colors.RESET}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
