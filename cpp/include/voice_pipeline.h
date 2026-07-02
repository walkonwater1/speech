#pragma once
/**
 * 语音交互管线编排器
 *
 * Python 对应: src/pipeline.py → VoicePipeline
 *
 * 这是你机器人主控类的参考实现，组装了全部模块：
 *
 *   VoicePipeline pipeline(cfg);
 *   pipeline.initialize();
 *   pipeline.process_text("你好");
 *   pipeline.process_voice();   // 录音 → ASR → KWS → SV → LLM → TTS
 */

#include <string>
#include <memory>

#include "config.h"
#include "asr_engine.h"
#include "llm_engine.h"
#include "tts_engine.h"
#include "wake_word.h"
#include "speaker_verifier.h"
#include "chat_memory.h"
#include "audio_io.h"

class VoicePipeline {
public:
    explicit VoicePipeline(const PipelineConfig& cfg = PipelineConfig());

    /// 加载所有模型（阻塞，首次可能下载）
    bool initialize();

    /// 文字输入 → LLM → TTS → 播放
    /// @return LLM 回复文本
    std::string process_text(const std::string& text);

    /// 录音 → ASR → 唤醒词 → 声纹 → LLM → TTS → 播放
    /// @return LLM 回复文本（失败返回空串）
    std::string process_voice();

    /// 声纹注册
    bool enroll_speaker();

    /// 清空对话记忆
    void clear_memory();

private:
    PipelineConfig cfg_;

    ASREngine         asr_;
    LLMEngine         llm_;
    TTSEngine         tts_;
    WakeWordDetector  kws_;
    SpeakerVerifier   speaker_;
    ChatMemory        memory_;
    AudioRecorder     recorder_;

    bool initialized_ = false;

    /// TTS 合成 + 播放
    void speak_and_play(const std::string& text);
};
