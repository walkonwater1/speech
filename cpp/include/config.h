#pragma once
/**
 * 管线配置结构体
 *
 * Python 对应: src/config.py → PipelineConfig
 */

#include <string>

struct PipelineConfig {
    // ── ASR ────────────────────────────────────────
    /// sherpa-onnx SenseVoice 模型路径
    std::string asr_model_path = "cpp/third_party/sherpa-onnx/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2025-06-18";

    // ── LLM ────────────────────────────────────────
    std::string ollama_host   = "http://127.0.0.1:11434";
    std::string llm_model     = "qwen2.5:1.5b";
    std::string system_prompt = "你叫小千，是一个18岁的女大学生，性格活泼开朗。回答简洁有趣，不超过50字。";

    // ── TTS ────────────────────────────────────────
    int tts_rate = 200;   // espeak 语速 (词/分钟)

    // ── 唤醒词 ─────────────────────────────────────
    /// 拼音字符串，空 = 关闭
    std::string wake_word = "zhan qi lai";

    // ── 声纹验证 ───────────────────────────────────
    /// 注册音频目录
    std::string sv_enroll_dir = "speaker_voice";
    /// 相似度阈值 (越低越严格)
    float sv_threshold = 0.35f;

    // ── 音频 ───────────────────────────────────────
    int sample_rate = 16000;

    // ── 对话记忆 ───────────────────────────────────
    int max_rounds = 10;
    int max_tokens = 512;
};
