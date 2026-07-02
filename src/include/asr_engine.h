#pragma once
/**
 * 语音识别引擎 (ASR)
 *
 * Python 对应: src/asr.py → ASREngine
 * 依赖:      sherpa-onnx C API (v1.13+)
 */

#include <string>
#include <memory>

// sherpa-onnx C API 前向声明
struct SherpaOnnxOfflineRecognizer;

class ASREngine {
public:
    explicit ASREngine(const std::string& model_path);
    ~ASREngine();

    // 禁止拷贝
    ASREngine(const ASREngine&) = delete;
    ASREngine& operator=(const ASREngine&) = delete;

    /// 加载模型（阻塞 1-5 秒）
    bool initialize();

    /// 语音 → 文字
    /// @param wav_path  16kHz 单声道 WAV 文件路径
    /// @return 识别文本，失败返回空字符串
    std::string transcribe(const std::string& wav_path);

private:
    std::string model_path_;
    const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;  // C API opaque pointer
    bool initialized_ = false;
};
