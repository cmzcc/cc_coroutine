#!/bin/bash

# 设置颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== Modern Coro 项目构建脚本 ===${NC}"

# 确保在正确的目录
cd "$(dirname "$0")"

# 清理旧的构建目录（解决 CMake 缓存问题）
if [ -d "build" ]; then
    echo -e "${YELLOW}清理旧的构建目录...${NC}"
    rm -rf build
fi

# 创建新的构建目录
echo -e "${YELLOW}创建构建目录...${NC}"
mkdir -p build
cd build

# 配置项目 - 现在 CMakeLists.txt 在根目录
echo -e "${YELLOW}配置 CMake 项目...${NC}"
if cmake .. -DCMAKE_BUILD_TYPE=Release; then
    echo -e "${GREEN}✓ CMake 配置成功${NC}"
else
    echo -e "${RED}✗ CMake 配置失败${NC}"
    exit 1
fi

# 编译
echo -e "${YELLOW}开始编译 (使用 $(nproc) 个并行任务)...${NC}"
if make -j$(nproc); then
    echo -e "${GREEN}✓ 编译成功！${NC}"
else
    echo -e "${RED}✗ 编译失败${NC}"
    exit 1
fi

# 检查生成的文件
echo -e "\n${BLUE}=== 构建结果 ===${NC}"
if [ -f "modern_coro_example" ]; then
    echo -e "  ${GREEN}✓${NC} 可执行文件: $(pwd)/modern_coro_example"
else
    echo -e "  ${RED}✗${NC} 可执行文件未找到"
fi

# 查找共享库文件
SHARED_LIB=$(find . -name "libmodern_coro.so*" -type f | head -1)
if [ -n "$SHARED_LIB" ]; then
    echo -e "  ${GREEN}✓${NC} 共享库: $(pwd)/$SHARED_LIB"
    
    # 创建符号链接（如果需要）
    if [[ "$SHARED_LIB" == *".so."* ]]; then
        BASE_NAME=$(echo "$SHARED_LIB" | sed 's/\.so\..*/\.so/')
        if [ ! -L "$BASE_NAME" ]; then
            ln -sf "$(basename "$SHARED_LIB")" "$BASE_NAME"
            echo -e "  ${GREEN}✓${NC} 符号链接: $(pwd)/$BASE_NAME"
        fi
    fi
else
    echo -e "  ${RED}✗${NC} 共享库未找到"
fi

# 显示库信息
if [ -n "$SHARED_LIB" ]; then
    echo -e "\n${BLUE}=== 库信息 ===${NC}"
    echo -e "  大小: $(du -h "$SHARED_LIB" | cut -f1)"
    if command -v ldd >/dev/null 2>&1; then
        echo -e "  依赖: $(ldd "$SHARED_LIB" | grep -c '=>') 个动态库"
    fi
fi


echo -e "\n${GREEN}🎉 构建脚本执行完成！${NC}"
echo -e "${BLUE}提示：${NC}"
echo -e "  - 可执行文件位于: build/modern_coro_example"
echo -e "  - 共享库位于: build/libmodern_coro.so*"
echo -e "  - 运行示例: cd build && ./modern_coro_example"