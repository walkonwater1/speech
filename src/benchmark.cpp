/**
 * 延迟基准测试工具
 *
 * 测量各模块延迟：
 *   1. ASR 延迟（离线识别）
 *   2. LLM 首token / 完整回复延迟（需 Ollama 在线）
 *   3. TTS 合成延迟
 *   4. 端到端延迟估算
 *
 * 编译：cmake --build build --target benchmark
 * 运行：./build/benchmark [wav_file]
 */

#include "asr_engine.h"
#include "llm_engine.h"
#include "tts_engine.h"
#include "pipeline/config.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <cstdio>

using namespace std::chrono;

static void print_header(const std::string& title)
{
    std::cout << "\n━━━ " << title << " ━━━" << std::endl;
}

// ── ASR 基准 ──────────────────────────────────────────

static void benchmark_asr(const std::string& model_path,
                          const std::string& wav_path,
                          AsrModelType model_type,
                          int warmup_runs = 1,
                          int test_runs = 5)
{
    print_header("ASR 语音识别");

    ASREngine asr(model_path, model_type);
    if (!asr.initialize()) {
        std::cerr << "❌ ASR 初始化失败" << std::endl;
        return;
    }

    // 预热
    for (int i = 0; i < warmup_runs; ++i) {
        asr.transcribe(wav_path);
    }

    // 正式测试
    double total_ms = 0;
    double min_ms = 1e9, max_ms = 0;
    std::string last_text;

    for (int i = 0; i < test_runs; ++i) {
        auto t0 = high_resolution_clock::now();
        last_text = asr.transcribe(wav_path);
        auto t1 = high_resolution_clock::now();

        double ms = duration<double, std::milli>(t1 - t0).count();
        total_ms += ms;
        if (ms < min_ms) min_ms = ms;
        if (ms > max_ms) max_ms = ms;
    }

    double avg_ms = total_ms / test_runs;
    std::cout << "  模型: " << (model_type == AsrModelType::ZIPFORMER_CTC
                               ? "Zipformer CTC" : "SenseVoice") << std::endl;
    std::cout << "  测试轮数: " << test_runs << std::endl;
    std::cout << "  平均延迟: " << std::fixed << std::setprecision(0)
              << avg_ms << " ms" << std::endl;
    std::cout << "  最小/最大: " << min_ms << " / " << max_ms << " ms" << std::endl;
    std::cout << "  实时率: "
              << std::setprecision(1) << avg_ms / 1000.0 << "x"
              << std::endl;
    if (!last_text.empty()) {
        std::cout << "  识别结果: \"" << last_text << "\"" << std::endl;
    }
}

// ── LLM 基准 ──────────────────────────────────────────

static void benchmark_llm(const PipelineConfig& cfg,
                          const std::string& prompt,
                          int test_runs = 3)
{
    print_header("LLM 推理");

    LLMEngine llm(cfg.ollama_host, cfg.llm_model, cfg.system_prompt);

    // 简单测试：不预热（Ollama 已有缓存）
    double total_ms = 0;
    double first_token_ms = 0;
    int success = 0;

    for (int i = 0; i < test_runs; ++i) {
        auto t0 = high_resolution_clock::now();
        std::string reply = llm.chat(prompt, "", "");
        auto t1 = high_resolution_clock::now();

        if (!reply.empty()) {
            double ms = duration<double, std::milli>(t1 - t0).count();
            total_ms += ms;
            success++;
            if (i == 0) first_token_ms = ms;
        }
    }

    if (success == 0) {
        std::cout << "  ⚠️ Ollama 未响应，跳过 LLM 基准" << std::endl;
        return;
    }

    double avg_ms = total_ms / success;
    std::cout << "  模型: " << cfg.llm_model << std::endl;
    std::cout << "  Prompt: \"" << prompt << "\"" << std::endl;
    std::cout << "  成功/总: " << success << "/" << test_runs << std::endl;
    std::cout << "  平均延迟: " << std::fixed << std::setprecision(0)
              << avg_ms << " ms" << std::endl;
    std::cout << "  首轮延迟: " << first_token_ms << " ms" << std::endl;
    std::cout << "  Token/s: " << std::setprecision(1)
              << (prompt.size() / 3.0) / (first_token_ms / 1000.0) << " (估)"
              << std::endl;
}

// ── TTS 基准 ──────────────────────────────────────────

static void benchmark_tts(const PipelineConfig& cfg,
                          const std::string& text,
                          int test_runs = 3)
{
    print_header("TTS 语音合成");

    TTSEngine tts(cfg.tts_rate, cfg.tts_voice,
                  cfg.tts_backend, cfg.piper_model_path);
    if (!tts.initialize()) {
        std::cerr << "❌ TTS 初始化失败" << std::endl;
        return;
    }

    const std::string out_file = "temp_benchmark_tts.wav";
    double total_ms = 0;
    int success = 0;

    for (int i = 0; i < test_runs; ++i) {
        auto t0 = high_resolution_clock::now();
        bool ok = tts.synthesize(text, out_file, "");
        auto t1 = high_resolution_clock::now();

        if (ok) {
            double ms = duration<double, std::milli>(t1 - t0).count();
            total_ms += ms;
            success++;
        }
    }

    std::remove(out_file.c_str());

    if (success == 0) {
        std::cout << "  ⚠️ TTS 合成失败" << std::endl;
        return;
    }

    double avg_ms = total_ms / success;
    std::cout << "  后端: " << cfg.tts_backend << std::endl;
    std::cout << "  文本: \"" << text << "\"" << std::endl;
    std::cout << "  平均延迟: " << std::fixed << std::setprecision(0)
              << avg_ms << " ms" << std::endl;
}

// ── Main ───────────────────────────────────────────────

int main(int argc, char* argv[])
{
    std::cout << "\n╔═══════════════════════════════════╗" << std::endl;
    std::cout << "║   Voice Pipeline 延迟基准测试     ║" << std::endl;
    std::cout << "╚═══════════════════════════════════╝" << std::endl;

    // 加载配置
    PipelineConfig cfg;
    std::string config_path = "config.json";
    if (!cfg.load_from_file(config_path)) {
        // 尝试上级目录
        config_path = "../config.json";
        cfg.load_from_file(config_path);
    }
    std::cout << "\n📄 配置: " << config_path << std::endl;

    // 测试音频文件
    std::string test_wav = "test_recording.wav";
    if (argc >= 2) {
        test_wav = argv[1];
    }

    bool wav_exists = (std::ifstream(test_wav).good());

    // ── ASR 基准 ──────────────────────────────────────
    AsrModelType mt = (cfg.asr_model_type == "zipformer_ctc")
        ? AsrModelType::ZIPFORMER_CTC : AsrModelType::SENSE_VOICE;

    if (wav_exists) {
        benchmark_asr(cfg.asr_model_path, test_wav, mt, 1, 5);
    } else {
        std::cout << "\n⚠️  测试音频不存在: " << test_wav
                  << "，跳过 ASR 基准" << std::endl;
    }

    // ── LLM 基准 ──────────────────────────────────────
    benchmark_llm(cfg, "你好", 3);

    // ── TTS 基准 ──────────────────────────────────────
    benchmark_tts(cfg, "你好，我是小千，很高兴认识你。", 3);

    // ── 端到端汇总 ────────────────────────────────────
    print_header("端到端延迟估算");
    std::cout << "  典型交互流程:" << std::endl;
    std::cout << "  ASR (~2s) + LLM (~3-8s) + TTS (~1-2s) = 端到端 6-12s"
              << std::endl;
    std::cout << "  注：流式ASR可重叠ASR延迟 → 端到端 4-10s" << std::endl;

    std::cout << "\n✅ 基准测试完成" << std::endl;
    return 0;
}
