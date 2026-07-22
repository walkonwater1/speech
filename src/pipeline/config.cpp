/**
 * 管线配置 — JSON 文件加载
 *
 * 使用 nlohmann/json（项目已有依赖）解析配置文件。
 * 未在文件中出现的键保持默认值，实现最小化配置覆盖。
 */

#include "config.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <climits>
#include <unistd.h>

using json = nlohmann::json;

// ── 辅助：安全读 JSON 字段，不存在则跳过 ────────────

template <typename T>
static void try_get(const json& j, const char* key, T& target)
{
    if (j.contains(key)) {
        auto& val = j[key];
        if (!val.is_null()) {
            if constexpr (std::is_same_v<T, int>)
                target = val.get<int>();
            else if constexpr (std::is_same_v<T, float>)
                target = val.get<float>();
            else if constexpr (std::is_same_v<T, std::string>)
                target = val.get<std::string>();
        }
    }
}

bool PipelineConfig::load_from_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        return false;
    }

    json j;
    try {
        f >> j;
    } catch (const json::parse_error& e) {
        std::cerr << "⚠️ 配置文件 JSON 解析失败: " << e.what() << std::endl;
        return false;
    }

    // ── ASR ────────────────────────────────────────
    if (j.contains("asr")) {
        auto& a = j["asr"];
        try_get(a, "model_path", asr_model_path);
    }

    // ── LLM ────────────────────────────────────────
    if (j.contains("llm")) {
        auto& l = j["llm"];
        try_get(l, "host",          ollama_host);
        try_get(l, "model",         llm_model);
        try_get(l, "system_prompt", system_prompt);
    }

    // ── TTS ────────────────────────────────────────
    if (j.contains("tts")) {
        try_get(j["tts"], "rate",       tts_rate);
        try_get(j["tts"], "voice",      tts_voice);
        try_get(j["tts"], "backend",    tts_backend);
        try_get(j["tts"], "piper_model", piper_model_path);
    }

    // ── 唤醒词 ─────────────────────────────────────
    if (j.contains("wake_word")) {
        try_get(j["wake_word"], "keyword", wake_word);
    }

    // ── 声纹验证 ───────────────────────────────────
    if (j.contains("speaker_verification")) {
        auto& s = j["speaker_verification"];
        try_get(s, "enroll_dir", sv_enroll_dir);
        try_get(s, "threshold",  sv_threshold);
    }

    // ── 音频 ───────────────────────────────────────
    if (j.contains("audio")) {
        try_get(j["audio"], "sample_rate", sample_rate);
    }

    // ── VAD ────────────────────────────────────────
    if (j.contains("vad")) {
        auto& v = j["vad"];
        try_get(v, "backend",            vad_backend);
        try_get(v, "energy_threshold",   vad_energy_threshold);
        try_get(v, "min_speech_frames",  vad_min_speech_frames);
        try_get(v, "min_silence_frames", vad_min_silence_frames);
        try_get(v, "pre_speech_frames",  vad_pre_speech_frames);
        try_get(v, "adaptive_factor",    vad_adaptive_factor);
    }

    // ── 对话记忆 ───────────────────────────────────
    if (j.contains("memory")) {
        auto& m = j["memory"];
        try_get(m, "max_rounds", max_rounds);
        try_get(m, "max_tokens", max_tokens);
    }

    // ── 技能 ────────────────────────────────────────
    if (j.contains("skills")) {
        auto& s = j["skills"];
        if (s.contains("weather")) {
            try_get(s["weather"], "enabled", skill_weather);
        }
        if (s.contains("time")) {
            try_get(s["time"], "enabled", skill_time);
        }
        if (s.contains("web_search")) {
            try_get(s["web_search"], "enabled", skill_web_search);
        }
        if (s.contains("rag")) {
            auto& r = s["rag"];
            try_get(r, "enabled",  skill_rag);
            try_get(r, "docs_dir", rag_docs_dir);
        }
    }

    return true;
}

std::string PipelineConfig::auto_load_path()
{
    // 按优先级查找 config.json:
    //   1. 当前工作目录
    //   2. 可执行文件所在目录
    //   3. CONFIG_PATH 环境变量

    const std::string filename = "config.json";

    // 1. 当前目录
    {
        std::ifstream f(filename);
        if (f.is_open()) return filename;
    }

    // 2. 环境变量
    const char* env = std::getenv("VOICE_PIPELINE_CONFIG");
    if (env && env[0] != '\0') {
        std::ifstream f(env);
        if (f.is_open()) return env;
    }

    // 均未找到
    return "";
}
