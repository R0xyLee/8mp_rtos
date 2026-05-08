#!/bin/bash
set -e

# ========== 使用说明 ==========
usage() {
    echo "Usage: $0 {clean|release|ddr_debug|ddr_release|flash_debug|flash_release}"
    echo "  clean          : 清理所有构建（调用每个 armgcc 目录下的 clean.sh）"
    echo "  release        : 构建 release 模式 (build_release.sh)"
    echo "  ddr_debug      : 构建 ddr_debug 模式 (build_ddr_debug.sh)"
    echo "  ddr_release    : 构建 ddr_release 模式 (build_ddr_release.sh)"
    echo "  flash_debug    : 构建 flash_debug 模式 (build_flash_debug.sh)"
    echo "  flash_release  : 构建 flash_release 模式 (build_flash_release.sh)"
    exit 1
}

# ========== 参数检查 ==========
if [ $# -ne 1 ]; then
    usage
fi

MODE="$1"
VALID_MODES="clean release ddr_debug ddr_release flash_debug flash_release"
if ! echo "$VALID_MODES" | grep -qw "$MODE"; then
    echo "Error: Invalid mode '$MODE'"
    usage
fi

RTOS_HOME_DIR=$(pwd)
export ARMGCC_DIR=$RTOS_HOME_DIR/tools/gcc-arm-none-eabi-10-2020-q4-major

# ========== 工具链路径设置（仅构建时需要） ==========
if [ "$MODE" != "clean" ]; then
    TOOLCHAIN_REL_PATH="tools/gcc-arm-none-eabi-10-2020-q4-major/bin"
    TOOLCHAIN_ABS_PATH="$RTOS_HOME_DIR/$TOOLCHAIN_REL_PATH"

    if [ ! -d "$TOOLCHAIN_ABS_PATH" ]; then
        echo "Error: Toolchain directory not found: $TOOLCHAIN_ABS_PATH"
        exit 1
    fi
    if [ ! -x "$TOOLCHAIN_ABS_PATH/arm-none-eabi-gcc" ]; then
        echo "Error: arm-none-eabi-gcc not found in $TOOLCHAIN_ABS_PATH"
        exit 1
    fi
    export PATH="$TOOLCHAIN_ABS_PATH:$PATH"
    echo "Toolchain added to PATH: $TOOLCHAIN_ABS_PATH"

    # 创建针对当前模式的输出目录
    OUTPUT_DIR="$RTOS_HOME_DIR/OK8MP_RTOS_BIN/$MODE"
    mkdir -p "$OUTPUT_DIR"
    echo "Output directory: $OUTPUT_DIR"
fi

# ========== 定义构建时需要的脚本名称和产物目录 ==========
if [ "$MODE" != "clean" ]; then
    # 构建脚本名称映射
    BUILD_SCRIPT="build_${MODE}.sh"
    # 产物目录名称（通常与模式同名，如 release/、ddr_debug/）
    ARTIFACT_DIR="$MODE"
fi

# ========== 遍历所有 armgcc 目录并执行 ==========
find boards/ -type d -name "armgcc" -print0 | while IFS= read -r -d '' armgcc_dir; do
    echo "Processing: $armgcc_dir"
    cd "$armgcc_dir"

    if [ "$MODE" = "clean" ]; then
        # 清理模式：检查 clean.sh 是否存在
        if [ -f "./clean.sh" ] && [ -x "./clean.sh" ]; then
            if ! ./clean.sh; then
                echo "Error: clean.sh failed in $armgcc_dir"
                exit 1
            fi
            echo "Cleaned: $armgcc_dir"
        else
            echo "Skipping: clean.sh not found or not executable in $armgcc_dir"
        fi
    else
        # 构建模式：检查对应的构建脚本是否存在
        if [ -f "./$BUILD_SCRIPT" ] && [ -x "./$BUILD_SCRIPT" ]; then
            # 执行构建脚本
            if ! ./"$BUILD_SCRIPT"; then
                echo "Error: $BUILD_SCRIPT failed in $armgcc_dir"
                exit 1
            fi
            # 复制产物：假设产物位于 $ARTIFACT_DIR 目录下
            if [ -d "$ARTIFACT_DIR" ]; then
                # 复制所有文件到输出目录，保留文件名，如果重名则覆盖
                cp -f "$ARTIFACT_DIR"/* "$OUTPUT_DIR/" 2>/dev/null || echo "No files in $ARTIFACT_DIR/ or copy failed"
                echo "Artifacts copied from $armgcc_dir/$ARTIFACT_DIR"
            else
                echo "Warning: $ARTIFACT_DIR directory not found in $armgcc_dir, no artifacts copied"
            fi
        else
            echo "Skipping: $BUILD_SCRIPT not found or not executable in $armgcc_dir"
        fi
    fi

    cd "$RTOS_HOME_DIR"
done

if [ "$MODE" = "clean" ]; then
    echo "All clean completed successfully."
else
    echo "All builds completed successfully for mode '$MODE'. Output is in $OUTPUT_DIR"
fi

