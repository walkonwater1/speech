/**
 * 语音交互管线编排器
 *
 * Python 对应: src/pipeline.py → VoicePipeline
 *
 * 数据流:
 *   文字:   prompt ──→ LLM ──→ TTS ──→ 播放 → 更新记忆
 *   语音:   麦克风 ──→ ASR ──→ KWS ──→ SV ──→ LLM ──→ TTS ──→ 播放
 */

#include "voice_pipeline.h"

#include <iostream>
#include <cstdio>

VoicePipeline::VoicePipeline(const PipelineConfig& cfg)
    : cfg_(cfg)
    , asr_(cfg.asr_model_path)
    , llm_(cfg.ollama_host, cfg.llm_model, cfg.system_prompt)
    , tts_(cfg.tts_rate)
    , kws_(cfg.wake_word)
    , speaker_(cfg.sv_enroll_dir, cfg.sv_threshold)
    , memory_(cfg.max_rounds, cfg.max_tokens)
    , recorder_(cfg.sample_rate)
{}

bool VoicePipeline::initialize()
{
    std::cout << "============ 初始化管线 ============" << std::endl;

    if (!asr_.initialize()) {
        std::cerr << "❌ ASR 初始化失败" << std::endl;
        // 不 return false —— 允许降级运行（无 ASR）
    }

    if (!speaker_.initialize()) {
        std::cerr << "❌ 声纹模型初始化失败" << std::endl;
        // 同上
    }

    if (!tts_.initialize()) {
        std::cerr << "❌ TTS 初始化失败" << std::endl;
        return false;
    }

    // 显示状态
    std::cout << "[4/4] Ollama: " << cfg_.llm_model
              << " (" << cfg_.ollama_host << ")" << std::endl;

    std::cout << "   特性: ";
    if (kws_.enabled()) std::cout << "唤醒词=\"" << cfg_.wake_word << "\" ";
    std::cout << "声纹(阈值=" << cfg_.sv_threshold << ") ";
    std::cout << "记忆(最近" << cfg_.max_rounds << "轮)";
    std::cout << std::endl;

    std::cout << "   " << speaker_.status_text() << std::endl;
    std::cout << std::endl;

    initialized_ = true;
    return true;
}

// ── 文字输入 ─────────────────────────────────────────

std::string VoicePipeline::process_text(const std::string& text)
{
    if (!initialized_) return "";

    // 1) LLM 推理（带对话记忆）
    std::string context = memory_.get_context();
    std::string reply = llm_.chat(text, context);

    if (reply.empty()) return "";

    // 2) 更新记忆
    memory_.add(text, reply);

    // 3) TTS + 播放
    speak_and_play(reply);

    return reply;
}

// ── 语音输入（完整链路）──────────────────────────────

std::string VoicePipeline::process_voice()
{
    if (!initialized_) return "";

    const std::string wav_file = "temp_recording.wav";

    // 1) 录音
    if (!recorder_.record(wav_file)) {
        return "";
    }

    // 2) ASR
    std::string prompt = asr_.transcribe(wav_file);
    if (prompt.empty()) {
        std::cout << "   ⚠️ 未识别到语音" << std::endl;
        std::remove(wav_file.c_str());
        return "";
    }

    // 3) 唤醒词检查
    if (!kws_.detect(prompt)) {
        speak_and_play("请说出正确的唤醒词");
        std::remove(wav_file.c_str());
        return "";
    }

    // 4) 声纹验证
    if (!speaker_.has_enrolled()) {
        speak_and_play("请先注册声纹，输入 enroll 开始注册");
        std::remove(wav_file.c_str());
        return "";
    }

    if (!speaker_.verify(wav_file)) {
        speak_and_play("声纹验证失败，我无法为您服务");
        std::remove(wav_file.c_str());
        return "";
    }

    std::remove(wav_file.c_str());

    // 5) LLM
    std::string context = memory_.get_context();
    std::string reply = llm_.chat(prompt, context);
    if (reply.empty()) return "";

    // 6) 更新记忆
    memory_.add(prompt, reply);

    // 7) TTS + 播放
    speak_and_play(reply);

    return reply;
}

// ── 声纹注册 ─────────────────────────────────────────

bool VoicePipeline::enroll_speaker()
{
    const std::string wav_file = "temp_enroll.wav";

    if (!recorder_.record(wav_file)) {
        return false;
    }

    if (!speaker_.enroll(wav_file)) {
        std::remove(wav_file.c_str());
        return false;
    }

    speak_and_play("声纹注册完成！现在只有你可以命令我啦！");
    return true;
}

// ── 工具 ─────────────────────────────────────────────

void VoicePipeline::clear_memory()
{
    memory_.clear();
    std::cout << "   🧹 对话记忆已清空" << std::endl;
}

void VoicePipeline::speak_and_play(const std::string& text)
{
    const std::string tts_file = "temp_reply.wav";

    if (tts_.synthesize(text, tts_file)) {
        std::cout << "   🔊 播放中..." << std::endl;
        AudioPlayer::play(tts_file);
        std::remove(tts_file.c_str());
    }
}
