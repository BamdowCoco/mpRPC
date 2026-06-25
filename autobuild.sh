#!/bin/bash
# autobuild.sh - 自动化构建脚本

# 颜色代码
# 检查是否在终端中运行（不是管道或重定向）
if [ -t 1 ]; then
    BLUE='\033[0;94m'
    # BLUE='\033[0;34m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    RED='\033[0;31m'

    RESET='\033[0m'
else
    # 不支持颜色时，使用空字符串
    BLUE=''
    GREEN=''
    YELLOW=''
    RED=''
    RESET=''
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

set -e # 遇到错误立刻退出

echo -e "${YELLOW}========= 开始构建 =========${RESET}"

# 创建 build 目录
echo -e "${BLUE}创建 build 目录...${RESET}"
mkdir -p "$BUILD_DIR"

# 进入 build 目录
cd build

# 清理
echo -e "${BLUE}清理旧文件...${RESET}"
rm -rf *

# 构建项目
echo -e "${BLUE}执行cmake...${RESET}"
cmake ..

echo -e "${BLUE}执行make...${RESET}"
make

echo -e "${GREEN}========= 构建完成 =========${RESET}"