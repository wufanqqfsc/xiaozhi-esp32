#!/bin/bash
# Wrapper: 完整 source ESP-IDF 环境并执行命令
set -e

export PATH="/Users/sfan/bin:/opt/homebrew/bin:/usr/local/bin:$PATH"
export IDF_PATH="/Users/sfan/.espressif/v5.5.4/esp-idf"
export ESP_PYTHON="/Users/sfan/.espressif/python_env/idf5.5_py3.14_env/bin/python3"

eval "$("$ESP_PYTHON" "$IDF_PATH/tools/activate.py" --export --shell zsh)"

# 关键的 cmake 修复：set CMAKE_PROGRAM_PATH
export CMAKE_PROGRAM_PATH="/usr/bin:/usr/local/bin:/opt/homebrew/bin:$CMAKE_PROGRAM_PATH"

# 同时直接设置 CMAKE_MAKE_PROGRAM（HIGH priority）
export CMAKE_MAKE_PROGRAM=/usr/bin/make

# 验证关键工具
echo "=== 验证"
which "$CMAKE_MAKE_PROGRAM"
which idf.py

echo ""
echo "=== 执行: $@"
exec "$@"
