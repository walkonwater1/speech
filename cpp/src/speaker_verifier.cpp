/**
 * 声纹验证 — sherpa-onnx Speaker Verification
 *
 * Python 对应: src/speaker.py → SpeakerVerifier (CAM++, modelscope)
 * C++ 实现:   sherpa-onnx speaker verification C API
 *
 * 编译依赖: libsherpa-onnx.so + sherpa-onnx/c-api/c-api.h
 */

#include "speaker_verifier.h"

// 下载 sherpa-onnx 后取消注释:
// #define SHERPA_ONNX_AVAILABLE

#ifdef SHERPA_ONNX_AVAILABLE
  #include "sherpa-onnx/c-api/c-api.h"
#endif

#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

// ── 辅助: 文件复制 ───────────────────────────────────

static bool copy_file(const std::string& src, const std::string& dst)
{
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary);
    if (!out) return false;
    out << in.rdbuf();
    return true;
}

// ── 辅助: 文件夹是否为空 ─────────────────────────────

static bool is_dir_empty(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return true;
    if (!S_ISDIR(st.st_mode)) return true;

    // 检查是否有 .wav 文件
    // 简化：检查目录是否有任何文件
    return access((path + "/enroll_0.wav").c_str(), F_OK) != 0;
}

// ── SpeakerVerifier ──────────────────────────────────

SpeakerVerifier::SpeakerVerifier(const std::string& enroll_dir, float threshold)
    : enroll_dir_(enroll_dir)
    , threshold_(threshold)
{}

SpeakerVerifier::~SpeakerVerifier()
{
#ifdef SHERPA_ONNX_AVAILABLE
    if (model_) {
        SherpaOnnxSpeakerVerificationModelDestroy(model_);
    }
#endif
}

bool SpeakerVerifier::initialize()
{
    std::cout << "[SV] 加载声纹模型 ... " << std::flush;

    // 确保注册目录存在
    mkdir(enroll_dir_.c_str(), 0755);

#ifdef SHERPA_ONNX_AVAILABLE
    // sherpa-onnx speaker verification model
    // 需要下载模型文件，例如 3D-Speaker / CAM++
    std::string model_path = "cpp/third_party/sherpa-onnx/speaker-verification-model";

    SherpaOnnxSpeakerVerificationModelConfig* config =
        SherpaOnnxSpeakerVerificationModelConfigCreate();
    SherpaOnnxSpeakerVerificationModelConfigSetModel(config, model_path.c_str());

    model_ = SherpaOnnxCreateSpeakerVerificationModel(config);
    SherpaOnnxSpeakerVerificationModelConfigDelete(config);

    if (!model_) {
        std::cerr << "❌ 加载声纹模型失败" << std::endl;
        return false;
    }
#else
    std::cerr << "⚠️  sherpa-onnx 未安装（跳过）" << std::endl;
    initialized_ = true;
    return true;
#endif

    initialized_ = true;
    std::cout << "✅" << std::endl;
    return true;
}

bool SpeakerVerifier::has_enrolled() const
{
    return !is_dir_empty(enroll_dir_);
}

std::string SpeakerVerifier::status_text() const
{
    if (has_enrolled()) {
        return "声纹已注册 ✅ (" + enroll_dir_ + ")";
    } else {
        return "声纹未注册 ⚠️";
    }
}

std::string SpeakerVerifier::enroll_path() const
{
    return enroll_dir_ + "/enroll_0.wav";
}

bool SpeakerVerifier::enroll(const std::string& wav_path)
{
    // 检查时长 > 3 秒
    // (简化: 直接复制文件)
    mkdir(enroll_dir_.c_str(), 0755);

    if (!copy_file(wav_path, enroll_path())) {
        std::cerr << "   [SV] 保存注册语音失败" << std::endl;
        return false;
    }

    std::cout << "   [SV] ✅ 声纹注册完成 → " << enroll_path() << std::endl;
    return true;
}

bool SpeakerVerifier::verify(const std::string& test_wav)
{
    if (!has_enrolled()) {
        std::cerr << "   [SV] ⚠️ 声纹未注册，请先注册" << std::endl;
        return false;
    }

#ifdef SHERPA_ONNX_AVAILABLE
    if (!model_) return false;

    float similarity = SherpaOnnxSpeakerVerificationModelVerify(
        model_, enroll_path().c_str(), test_wav.c_str());

    bool passed = similarity >= threshold_;
    std::cout << "   [SV] " << (passed ? "✅ 通过" : "❌ 拒绝")
              << " (相似度: " << similarity << ", 阈值: " << threshold_ << ")" << std::endl;

    return passed;
#else
    (void)test_wav;  // 未使用
    std::cerr << "   [SV] ⚠️ sherpa-onnx 不可用，跳过验证" << std::endl;
    return true;  // 降级: 通过（不阻塞流程）
#endif
}
