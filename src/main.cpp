/**
 * 语音交互 Demo — 主函数
 *
 * 用法:
 *   ./build/voice_pipeline
 *
 * 输入:
 *   文字     → 直接 LLM 对话（跳过唤醒词/声纹检查）
 *   r        → 录音 → ASR → KWS → SV → LLM → TTS（单次）
 *   listen   → 持续监听 + 语音打断交互模式
 *   enroll   → 声纹注册
 *   clear    → 清空对话记忆
 *   quit     → 退出
 *
 * 编译: mkdir build && cd build && cmake .. && make
 *
 * Python 对应: scripts/full_pipeline.py + run_realtime.py
 */

#include "voice_pipeline.h"

#include <iostream>
#include <string>
#include <csignal>

// 全局指针，用于 Ctrl+C 信号处理
static VoicePipeline* g_pipeline = nullptr;

static void signal_handler(int /*sig*/)
{
    std::cout << std::endl;
    if (g_pipeline) {
        g_pipeline->stop_interactive();
    }
}

static void print_menu()
{
    std::cout << "============================================================" << std::endl;
    std::cout << "  输入文字   → 直接 LLM 对话（跳过唤醒词/声纹检查）" << std::endl;
    std::cout << "  输入 r      → 单次录音 → 唤醒词检查 → 声纹验证 → LLM" << std::endl;
    std::cout << "  输入 listen → 🎤 交互模式（持续监听 + 语音打断）" << std::endl;
    std::cout << "  输入 enroll → 注册声纹（说 3 秒以上）" << std::endl;
    std::cout << "  输入 clear  → 清空对话记忆" << std::endl;
    std::cout << "  输入 quit   → 退出" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "🎉 就绪！" << std::endl << std::endl;
}

int main(int /*argc*/, char** /*argv*/)
{
    // ── 配置 ──────────────────────────────────────────
    PipelineConfig cfg;
    cfg.wake_word = "";                // 交互模式下唤醒词可选，"" 关闭
    cfg.llm_model = "qwen2.5:1.5b";   // Ollama 模型

    // ── 初始化 ────────────────────────────────────────
    VoicePipeline pipeline(cfg);
    if (!pipeline.initialize()) {
        std::cerr << "初始化失败，退出。" << std::endl;
        return 1;
    }

    g_pipeline = &pipeline;

    // Ctrl+C 处理
    signal(SIGINT, signal_handler);

    print_menu();

    // ── 主循环 ────────────────────────────────────────
    std::string line;
    while (true) {
        std::cout << ">>> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;  // EOF / Ctrl+D
        }

        // 去首尾空格
        auto start = line.find_first_not_of(" \t\r\n");
        auto end   = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;  // 空行
        std::string cmd = line.substr(start, end - start + 1);

        if (cmd == "quit") {
            std::cout << "👋 再见！" << std::endl;
            break;
        }

        if (cmd == "clear") {
            pipeline.clear_memory();
            continue;
        }

        if (cmd == "enroll") {
            pipeline.enroll_speaker();
            continue;
        }

        if (cmd == "listen") {
            // 交互模式：持续监听 + 语音打断
            pipeline.run_interactive();
            print_menu();
            continue;
        }

        if (cmd == "r") {
            pipeline.process_voice();
        } else {
            pipeline.process_text(cmd);
        }
    }

    g_pipeline = nullptr;
    return 0;
}
