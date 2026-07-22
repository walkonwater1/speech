#pragma once
/**
 * 管线配置结构体
 *
 * 支持两种配置方式：
 *   1. 代码直接赋值 — PipelineConfig cfg; cfg.llm_model = "...";
 *   2. JSON 配置文件 — cfg.load_from_file("config.json");
 *
 * JSON 配置文件格式见项目根目录 config.json。
 *
 * Python 对应: src/config.py → PipelineConfig
 */

#include <string>

struct PipelineConfig {
    // ── ASR ────────────────────────────────────────
    std::string asr_model_path = "src/third_party/sherpa-onnx/sense-voice-model";

    // ── LLM ────────────────────────────────────────
    std::string ollama_host   = "http://127.0.0.1:11434";
    std::string llm_model     = "qwen2.5:1.5b";
    std::string system_prompt = "你叫小千，是一个18岁的女大学生，性格活泼开朗。回答简洁有趣，不超过50字。";

    // ── TTS ────────────────────────────────────────
    int         tts_rate         = 200;                           // espeak 语速 (词/分钟)
    std::string tts_voice        = "cmn+f3";                      // espeak 音色
    std::string tts_backend      = "piper";                       // "espeak" 或 "piper"
    std::string piper_model_path = "~/pretrained_models/piper/zh_CN/zh_CN-xiao_ya-medium.onnx";

    // ── 唤醒词 ─────────────────────────────────────
    std::string wake_word = "zhan qi lai";   // 空字符串 = 关闭

    // ── 声纹验证 ───────────────────────────────────
    std::string sv_enroll_dir = "speaker_voice";
    float       sv_threshold  = 0.35f;       // 相似度阈值 (越低越严格)

    // ── 音频 ───────────────────────────────────────
    int sample_rate = 16000;

    // ── VAD（语音活动检测 / 打断灵敏度）─────────────
    std::string vad_backend          = "energy";   // "energy" 或 "adaptive"
    float vad_energy_threshold   = 0.003f;   // RMS 阈值，越大越不敏感（仅 energy 模式）
    int   vad_min_speech_frames  = 8;        // 最小语音帧数 (~160ms)
    int   vad_min_silence_frames = 30;       // 静音多少帧后判结束 (~600ms)
    int   vad_pre_speech_frames  = 15;       // 语音开始前保留帧数 (~300ms)
    float vad_adaptive_factor    = 3.0f;     // 阈值 = 噪声基线 × factor（仅 adaptive 模式）

    // ── 对话记忆 ───────────────────────────────────
    int max_rounds = 10;
    int max_tokens = 512;

    // ── 技能 ───────────────────────────────────────
    bool skill_weather    = true;
    bool skill_time       = true;
    bool skill_web_search = false;
    bool skill_rag        = false;
    std::string rag_docs_dir = "knowledge_base";

    // ── Function Calling (LLM 驱动工具选择) ──────────
    bool fc_enabled     = true;            // 启用 function calling
    std::string fc_model = "";             // 工具选择模型（空=复用 llm_model）

    // ── 文件加载 ───────────────────────────────────

    /// 从 JSON 文件加载配置（未出现在文件中的键保持默认值）
    /// @return true 加载成功，false 文件不存在或格式错误（回退到默认值）
    bool load_from_file(const std::string& path);

    /// 智能查找配置文件：当前目录 → 上级目录 → 环境变量
    /// @return 实际加载的文件路径，空 = 未找到（使用纯默认值）
    static std::string auto_load_path();
};
