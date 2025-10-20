#!/bin/bash

# 设置颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 解析命令行参数
MEMORY_LEAK_MODE=false
FAST_MEMORY_CHECK=false
BUILD_TYPE="Release"

while [[ $# -gt 0 ]]; do
    case $1 in
        --memory-leak)
            MEMORY_LEAK_MODE=true
            BUILD_TYPE="Debug"
            shift
            ;;
        --fast-memory-check)
            FAST_MEMORY_CHECK=true
            BUILD_TYPE="Debug"
            shift
            ;;
        --help)
            echo "用法: $0 [选项]"
            echo "选项:"
            echo "  --memory-leak       启用完整内存泄漏检测模式 (使用 AddressSanitizer 和 LeakSanitizer)"
            echo "  --fast-memory-check 启用快速内存检测模式 (仅使用 LeakSanitizer，速度更快)"
            echo "  --help              显示此帮助信息"
            exit 0
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            echo "使用 --help 查看可用选项"
            exit 1
            ;;
    esac
done

if [ "$MEMORY_LEAK_MODE" = true ]; then
    echo -e "${BLUE}=== Modern Coro 内存泄漏检测构建脚本 ===${NC}"
elif [ "$FAST_MEMORY_CHECK" = true ]; then
    echo -e "${BLUE}=== Modern Coro 快速内存检测构建脚本 ===${NC}"
else
    echo -e "${BLUE}=== Modern Coro 项目构建脚本 ===${NC}"
fi

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

# 根据模式设置不同的编译选项
if [ "$MEMORY_LEAK_MODE" = true ]; then
    echo -e "${YELLOW}启用内存泄漏检测模式...${NC}"
    # 优化 ASan 配置以加快速度：
    # - ASAN_OPTIONS: 减少检测开销，加快退出速度
    # - LSAN_OPTIONS: 减少泄漏检测的详细程度
    export ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1:fast_unwind_on_malloc=0:malloc_context_size=5:detect_odr_violation=0:alloc_dealloc_mismatch=0"
    export LSAN_OPTIONS="suppressions=../asan.supp:fast_unwind_on_malloc=0:malloc_context_size=5"
    
    if cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
                -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=leak -g -O1 -fno-omit-frame-pointer -fno-sanitize-recover=all" \
                -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address -fsanitize=leak" \
                ${TOOLCHAIN_ARG}; then
        echo -e "${GREEN}✓ CMake 配置成功${NC}"
    else
        echo -e "${RED}✗ CMake 配置失败${NC}"
        exit 1
    fi
elif [ "$FAST_MEMORY_CHECK" = true ]; then
    echo -e "${YELLOW}启用快速内存检测模式 (仅泄漏检测)...${NC}"
    # 仅使用 LeakSanitizer，速度更快
    export LSAN_OPTIONS="suppressions=../lsan.supp:fast_unwind_on_malloc=0:malloc_context_size=5"
    
    if cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
                -DCMAKE_CXX_FLAGS="-fsanitize=leak -g -O1 -fno-omit-frame-pointer" \
                -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=leak" \
                ${TOOLCHAIN_ARG}; then
        echo -e "${GREEN}✓ CMake 配置成功${NC}"
    else
        echo -e "${RED}✗ CMake 配置失败${NC}"
        exit 1
    fi
else
    if cmake .. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ${TOOLCHAIN_ARG}; then
        echo -e "${GREEN}✓ CMake 配置成功${NC}"
    else
        echo -e "${RED}✗ CMake 配置失败${NC}"
        exit 1
    fi
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
    if [ "$MEMORY_LEAK_MODE" = true ]; then
        # 内存泄漏检测模式：直接运行测试可执行文件，避免 CTest 超时问题
        echo -e "${BLUE}内存泄漏检测模式：直接运行测试...${NC}"
        if timeout 600 ./tests/unit_tests --gtest_output=xml:unit_tests.xml && timeout 800 ./tests/integration_tests --gtest_output=xml:integration_tests.xml; then
            echo -e "${GREEN}✓ 所有测试通过${NC}"
        else
            echo -e "${RED}✗ 测试失败${NC}"
            exit 1
        fi
    elif [ "$FAST_MEMORY_CHECK" = true ]; then
        # 快速内存检测模式：仅泄漏检测，速度更快
        echo -e "${BLUE}快速内存检测模式：直接运行测试...${NC}"
        if timeout 120 ./tests/unit_tests --gtest_output=xml:unit_tests.xml && timeout 180 ./tests/integration_tests --gtest_output=xml:integration_tests.xml; then
            echo -e "${GREEN}✓ 所有测试通过${NC}"
        else
            echo -e "${RED}✗ 测试失败${NC}"
            exit 1
        fi
    else
        # 普通模式：使用 CTest
        if ctest --output-on-failure; then
            echo -e "${GREEN}✓ 所有测试通过${NC}"
        else
            echo -e "${RED}✗ 测试失败${NC}"
            exit 1
        fi
    fi
fi

echo -e "\n${GREEN}🎉 构建脚本执行完成！${NC}"
echo -e "${BLUE}提示：${NC}"
if [ "$MEMORY_LEAK_MODE" = true ]; then
    echo -e "  - 内存检测日志保存在: build/asan.log, build/lsan.log"
    echo -e "  - 使用 lsan.supp 文件抑制已知泄漏"
    echo -e "  - 注意：完整内存检测模式较慢，但检测最全面"
elif [ "$FAST_MEMORY_CHECK" = true ]; then
    echo -e "  - 快速内存检测模式：仅检测内存泄漏，速度更快"
    echo -e "  - 使用 lsan.supp 文件抑制已知泄漏"
else
    echo -e "  - 共享库位于: build/libmodern_coro.so*"
    echo -e "  - 如需内存泄漏检测，请使用: ./build.sh --memory-leak"
    echo -e "  - 如需快速内存检测，请使用: ./build.sh --fast-memory-check"
fi