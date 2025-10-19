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

# 自动检测 vcpkg toolchain
TOOLCHAIN_ARG=""
if [ -z "${CMAKE_TOOLCHAIN_FILE}" ]; then
    # 优先使用环境变量 VCPKG_ROOT，其次尝试 ~/vcpkg
    if [ -n "${VCPKG_ROOT}" ] && [ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]; then
        TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        echo -e "${GREEN}检测到 VCPKG_ROOT：${VCPKG_ROOT}，已自动使用 vcpkg toolchain${NC}"
    elif [ -f "$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake" ]; then
        TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"
        echo -e "${GREEN}检测到 $HOME/vcpkg，已自动使用 vcpkg toolchain${NC}"
    fi
fi

if cmake .. -DCMAKE_BUILD_TYPE=Release ${TOOLCHAIN_ARG}; then
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


# 运行测试（如果开启）
if grep -q "add_subdirectory(tests)" ../CMakeLists.txt; then
    echo -e "\n${YELLOW}运行测试...${NC}"
    if ctest --output-on-failure; then
        echo -e "${GREEN}✓ 所有测试通过${NC}"
    else
        echo -e "${RED}✗ 测试失败${NC}"
        exit 1
    fi
fi

echo -e "\n${GREEN}🎉 构建脚本执行完成！${NC}"
echo -e "${BLUE}提示：${NC}"
echo -e "  - 共享库位于: build/libmodern_coro.so*"