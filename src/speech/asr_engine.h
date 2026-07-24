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

/// ASR 模型类型
enum class AsrModelType {
    SENSE_VOICE,    // SenseVoice Small (原默认，多语言，易幻觉)
    ZIPFORMER_CTC   // Zipformer CTC (中文专用，低幻觉)
};

class ASREngine {
public:
    explicit ASREngine(const std::string& model_path,
                       AsrModelType model_type = AsrModelType::SENSE_VOICE);
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

    /// 静默模式：不打印 [ASR] 日志（流式 ASR 部分识别时使用）
    void set_quiet(bool q) { quiet_ = q; }
    bool quiet() const { return quiet_; }

private:
    std::string model_path_;
    AsrModelType model_type_ = AsrModelType::SENSE_VOICE;
    const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;  // C API opaque pointer
    bool initialized_ = false;
    bool quiet_ = false;
};
