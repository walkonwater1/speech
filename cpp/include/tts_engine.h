#pragma once
/**
 * 语音合成引擎 (TTS)
 *
 * Python 对应: src/tts.py → TTSEngine
 * 依赖:      libespeak-ng (系统库)
 *
 * 备选:  sherpa-onnx Kokoro TTS (音质更好，需额外模型)
 */

#include <string>

class TTSEngine {
public:
    explicit TTSEngine(int rate = 200);
    ~TTSEngine();

    TTSEngine(const TTSEngine&) = delete;
    TTSEngine& operator=(const TTSEngine&) = delete;

    /// 初始化 espeak
    bool initialize();

    /// 文字 → WAV 文件
    /// @param text        待合成文本 (UTF-8)
    /// @param output_path 输出 WAV 路径
    bool synthesize(const std::string& text, const std::string& output_path);

private:
    int rate_;
    bool initialized_ = false;
};
