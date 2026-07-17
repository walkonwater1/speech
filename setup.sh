#!/usr/bin/env bash
#
# 一键安装 & 运行语音交互管线
#
# 用法:
#   ./setup.sh              # 安装依赖 + 下载模型 + 编译
#   ./setup.sh --run        # 安装依赖 + 下载模型 + 编译 + 运行
#   ./setup.sh --models     # 仅下载模型
#   ./setup.sh --build      # 仅编译
#   ./setup.sh --clean      # 清理编译产物
#
# 硬件: CPU only, 无需 GPU
# 系统: Ubuntu 20.04+

set -euo pipefail

# ── 配置 ──────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="${SCRIPT_DIR}/src"
THIRD_PARTY_DIR="${SRC_DIR}/third_party"
SHERPA_DIR="${THIRD_PARTY_DIR}/sherpa-onnx"
BUILD_DIR="${SRC_DIR}/build"

# sherpa-onnx 版本（与现有库匹配）
SHERPA_VERSION="1.13.2"
SHERPA_ARCH="linux-x64"
SHERPA_TAR="sherpa-onnx-${SHERPA_VERSION}-${SHERPA_ARCH}.tar.bz2"
SHERPA_URL="https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_VERSION}/${SHERPA_TAR}"

# 模型下载地址（HF Mirror 国内更快）
# 如果无法访问 huggingface，自动切换到镜像站
HF_BASE="https://huggingface.co"
HF_MIRROR="https://hf-mirror.com"

# ASR 模型: SenseVoice Small int8 (~228MB)
ASR_MODEL_REPO="csukuangfj/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17"
ASR_MODEL_FILES=(
    "model.int8.onnx"
    "tokens.txt"
)
ASR_MODEL_DIR="${SHERPA_DIR}/sense-voice-model"

# 声纹模型: CAM++ (~27MB)
SV_MODEL_REPO="csukuangfj/sherpa-onnx-speaker-verification"
SV_MODEL_FILES=(
    "3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx"
)
SV_MODEL_DIR="${SHERPA_DIR}/speaker-verification-model"

# ── 颜色 ──────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log()  { echo -e "${GREEN}[✓]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }
err()  { echo -e "${RED}[✗]${NC} $*"; }
info() { echo -e "${BLUE}[~]${NC} $*"; }

# ── 步骤1: 检查/安装系统依赖 ─────────────────────────────

install_deps() {
    log "检查系统依赖..."

    local missing=()
    local pkgs=()

    # 检查 cmake
    if ! command -v cmake &>/dev/null; then
        missing+=("cmake")
        pkgs+=("cmake")
    fi

    # 检查 g++
    if ! command -v g++ &>/dev/null; then
        missing+=("g++")
        pkgs+=("build-essential")
    fi

    # 检查 espeak-ng（直接查库文件，避免 ldconfig 非 root 无权限）
    if [ ! -f /usr/lib/x86_64-linux-gnu/libespeak-ng.so.1 ] \
       && [ ! -f /usr/lib/aarch64-linux-gnu/libespeak-ng.so.1 ]; then
        missing+=("espeak-ng")
        pkgs+=("espeak-ng" "espeak-ng-data")
    fi

    # 检查 libcurl（pkg-config 优先，再查多架构头文件路径）
    if ! pkg-config --exists libcurl 2>/dev/null \
       && [ ! -f /usr/include/curl/curl.h ] \
       && [ ! -f /usr/include/x86_64-linux-gnu/curl/curl.h ]; then
        missing+=("libcurl4")
        pkgs+=("libcurl4-openssl-dev")
    fi

    # 检查 nlohmann-json
    if [ ! -f /usr/include/nlohmann/json.hpp ] && [ ! -f /usr/include/nlohmann/json_fwd.hpp ]; then
        missing+=("nlohmann-json3-dev")
        pkgs+=("nlohmann-json3-dev")
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        warn "缺少依赖: ${missing[*]}"
        info "正在安装..."
        sudo apt-get update -qq
        sudo apt-get install -y "${pkgs[@]}"
        log "依赖安装完成"
    else
        log "系统依赖已就绪"
    fi
}

# ── 步骤2: 下载函数（优先 HF 镜像） ────────────────────────

download_file() {
    local url="$1"
    local output="$2"
    local desc="$3"

    if [ -f "$output" ]; then
        log "已存在: ${desc}"
        return 0
    fi

    info "下载: ${desc} ..."
    mkdir -p "$(dirname "$output")"

    # 尝试多个下载源
    if command -v wget &>/dev/null; then
        wget -q --show-progress -O "$output" "$url" 2>&1 || {
            warn "wget 失败，换镜像重试..."
            local mirror_url="${url/${HF_BASE}/${HF_MIRROR}}"
            wget -q --show-progress -O "$output" "$mirror_url" 2>&1
        }
    elif command -v curl &>/dev/null; then
        curl -L -# -o "$output" "$url" 2>&1 || {
            warn "curl 失败，换镜像重试..."
            local mirror_url="${url/${HF_BASE}/${HF_MIRROR}}"
            curl -L -# -o "$output" "$mirror_url" 2>&1
        }
    else
        err "需要 wget 或 curl，请先安装"
        exit 1
    fi

    if [ -f "$output" ]; then
        log "下载完成: ${desc} ($(du -h "$output" | cut -f1))"
    else
        err "下载失败: ${desc}"
        return 1
    fi
}

# ── 步骤3: 下载 sherpa-onnx 运行时库 ─────────────────────

install_sherpa_onnx() {
    log "检查 sherpa-onnx 运行时库..."

    local lib_file="${SHERPA_DIR}/lib/libsherpa-onnx-c-api.so"
    local include_file="${SHERPA_DIR}/include/sherpa-onnx/c-api/c-api.h"

    if [ -f "$lib_file" ] && [ -f "$include_file" ]; then
        log "sherpa-onnx 已就绪"
        return 0
    fi

    info "下载 sherpa-onnx v${SHERPA_VERSION} ..."

    local tmp_dir="/tmp/sherpa-onnx-$$"
    mkdir -p "$tmp_dir"

    local tar_path="${tmp_dir}/${SHERPA_TAR}"
    local url="${SHERPA_URL}"

    # 下载
    if command -v wget &>/dev/null; then
        wget -q --show-progress -O "$tar_path" "$url" || true
    else
        curl -L -# -o "$tar_path" "$url" || true
    fi

    if [ ! -f "$tar_path" ] || [ ! -s "$tar_path" ]; then
        err "下载 sherpa-onnx 失败"
        err "请手动下载: ${SHERPA_URL}"
        err "解压到: ${SHERPA_DIR}"
        rm -rf "$tmp_dir"
        return 1
    fi

    # 解压
    info "解压 sherpa-onnx ..."
    tar -xjf "$tar_path" -C "$tmp_dir"

    # 复制需要的文件
    local extracted_dir="${tmp_dir}/sherpa-onnx-${SHERPA_VERSION}-${SHERPA_ARCH}"
    mkdir -p "${SHERPA_DIR}/lib"
    mkdir -p "${SHERPA_DIR}/include"
    cp -r "${extracted_dir}/lib/"* "${SHERPA_DIR}/lib/"
    cp -r "${extracted_dir}/include/"* "${SHERPA_DIR}/include/"

    # 清理
    rm -rf "$tmp_dir"
    log "sherpa-onnx 安装完成"
}

# ── 步骤4: 下载模型 ───────────────────────────────────

install_models() {
    log "检查模型文件..."

    # ── ASR 模型: SenseVoice ──────────────────────────
    local asr_ok=true
    for f in "${ASR_MODEL_FILES[@]}"; do
        if [ ! -f "${ASR_MODEL_DIR}/$f" ]; then
            asr_ok=false
            break
        fi
    done

    if $asr_ok; then
        log "ASR 模型已就绪 (SenseVoice)"
    else
        info "下载 ASR 模型 (SenseVoice Small int8, ~228MB)..."
        mkdir -p "$ASR_MODEL_DIR"

        for f in "${ASR_MODEL_FILES[@]}"; do
            # 先尝试主站，失败自动切镜像
            local hf_url="${HF_BASE}/${ASR_MODEL_REPO}/resolve/main/$f"
            local mirror_url="${HF_MIRROR}/${ASR_MODEL_REPO}/resolve/main/$f"

            download_file "$hf_url" "${ASR_MODEL_DIR}/$f" "ASR: $f" || \
                download_file "$mirror_url" "${ASR_MODEL_DIR}/$f" "ASR: $f (mirror)"
        done
        log "ASR 模型下载完成"
    fi

    # ── 声纹模型: CAM++ ────────────────────────────────
    local sv_ok=true
    for f in "${SV_MODEL_FILES[@]}"; do
        if [ ! -f "${SV_MODEL_DIR}/$f" ]; then
            sv_ok=false
            break
        fi
    done

    if $sv_ok; then
        log "声纹模型已就绪 (CAM++)"
    else
        info "下载声纹模型 (CAM++, ~27MB)..."
        mkdir -p "$SV_MODEL_DIR"

        for f in "${SV_MODEL_FILES[@]}"; do
            local hf_url="${HF_BASE}/${SV_MODEL_REPO}/resolve/main/$f"
            local mirror_url="${HF_MIRROR}/${SV_MODEL_REPO}/resolve/main/$f"

            download_file "$hf_url" "${SV_MODEL_DIR}/$f" "SV: $f" || \
                download_file "$mirror_url" "${SV_MODEL_DIR}/$f" "SV: $f (mirror)"
        done
        log "声纹模型下载完成"
    fi
}

# ── 步骤5: 编译 ─────────────────────────────────────────

build() {
    log "编译 voice_pipeline..."

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | while IFS= read -r line; do
        echo "       $line"
    done

    local nproc
    nproc=$(nproc 2>/dev/null || echo 4)
    make -j"$nproc" 2>&1 | while IFS= read -r line; do
        echo "       $line"
    done

    cd "$SCRIPT_DIR"

    if [ -f "${BUILD_DIR}/voice_pipeline" ]; then
        log "编译成功 → ${BUILD_DIR}/voice_pipeline"
        log "二进制大小: $(du -h "${BUILD_DIR}/voice_pipeline" | cut -f1)"
    else
        err "编译失败，请检查错误信息"
        exit 1
    fi
}

# ── 步骤6: 运行 ─────────────────────────────────────────

run() {
    log "启动语音交互管线..."
    echo ""

    # 确保在项目根目录运行（模型路径使用相对路径）
    cd "$SCRIPT_DIR"

    # 设置 sherpa-onnx 库路径
    export LD_LIBRARY_PATH="${SHERPA_DIR}/lib:${LD_LIBRARY_PATH:-}"

    # 检查 Ollama 是否在运行
    if ! curl -s http://127.0.0.1:11434/api/tags &>/dev/null; then
        warn "Ollama 未运行，请先启动: ollama serve"
    fi

    # 尝试自动激活 conda chatAudio 环境（Piper TTS 需要）
    if [ -z "${CONDA_PREFIX:-}" ]; then
        for conda_base in "${HOME}/miniconda3" "${HOME}/anaconda3" "/opt/conda"; do
            if [ -f "${conda_base}/etc/profile.d/conda.sh" ]; then
                info "自动激活 conda 环境: chatAudio"
                source "${conda_base}/etc/profile.d/conda.sh"
                conda activate chatAudio 2>/dev/null || {
                    warn "conda 环境 chatAudio 不存在，Piper TTS 可能无法使用"
                    warn "请手动创建: conda create -n chatAudio python=3.10 && conda activate chatAudio && pip install piper-tts"
                }
                break
            fi
        done
    fi

    # 修复 conda 环境覆盖 ALSA 插件路径导致录音失败的问题
    export ALSA_PLUGIN_DIR=/usr/lib/x86_64-linux-gnu/alsa-lib
    exec "${BUILD_DIR}/voice_pipeline"
}

# ── 清理 ─────────────────────────────────────────────────

clean() {
    log "清理编译产物..."
    rm -rf "$BUILD_DIR"
    log "清理完成"
}

# ── 主流程 ──────────────────────────────────────────────

print_banner() {
    echo ""
    echo "  ╔══════════════════════════════════════════╗"
    echo "  ║    ASR-LLM-TTS 语音交互管线 一键安装     ║"
    echo "  ║    ASR → 唤醒词 → 声纹 → LLM → TTS       ║"
    echo "  ╚══════════════════════════════════════════╝"
    echo ""
}

print_help() {
    echo "用法: $0 [选项]"
    echo ""
    echo "  (无参数)   安装依赖 + 下载模型 + 编译"
    echo "  --run      安装依赖 + 下载模型 + 编译 + 运行"
    echo "  --models   仅下载模型"
    echo "  --build    仅编译"
    echo "  --clean    清理编译产物"
    echo "  --help     显示此帮助"
    echo ""
    echo "运行时依赖:"
    echo "  - Ollama 服务运行中 (ollama serve)"
    echo "  - qwen2.5:1.5b 模型已拉取 (ollama pull qwen2.5:1.5b)"
    echo "  - espeak-ng 已安装"
    echo "  - 麦克风/音箱可用 (ALSA)"
}

main() {
    print_banner

    case "${1:-}" in
        --help|-h)
            print_help
            exit 0
            ;;
        --clean)
            clean
            exit 0
            ;;
        --models)
            install_deps
            install_models
            log "模型下载完成！"
            log "下一步: $0 --build && $0 --run"
            exit 0
            ;;
        --build)
            build
            exit 0
            ;;
        --run)
            install_deps
            install_sherpa_onnx
            install_models
            build
            run
            ;;
        *)
            install_deps
            install_sherpa_onnx
            install_models
            build
            log "全部完成！"
            echo ""
            log "运行: $0 --run"
            log "或者: cd $(pwd) && ./src/build/voice_pipeline"
            ;;
    esac
}

main "$@"
