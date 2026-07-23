/**
 * WebSocket 语音服务 — 入口
 *
 * Layer 4.1: 服务化
 *
 * 用法:
 *   ./ws_voice_server [config.json] [port]
 *
 * 示例:
 *   ./ws_voice_server                  # 默认配置, 端口 9002
 *   ./ws_voice_server config.json      # 指定配置文件
 *   ./ws_voice_server config.json 8080 # 指定端口
 */

#include "pipeline/config.h"
#include "pipeline/voice_pipeline.h"
#include "server/ws_server.h"
#include "utils/config_watcher.h"
#include "logger.h"

#include <iostream>
#include <csignal>
#include <cstdlib>
#include <memory>

static WsVoiceServer* g_server = nullptr;
static VoicePipeline* g_pipeline = nullptr;
static std::string   g_config_path;

// ── 信号处理 ──────────────────────────────────────────

static void signal_handler(int sig)
{
    std::cout << "\n📡 收到信号 " << sig << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

/// SIGHUP: 热重载配置 (Layer 4.4)
static void reload_handler(int /*sig*/)
{
    LOG_INFO("[SIGHUP] 📝 收到 SIGHUP，重新加载配置...");
    if (g_config_path.empty() || !g_pipeline) {
        LOG_WARN("[SIGHUP] 无配置文件路径或 pipeline 未就绪");
        return;
    }

    PipelineConfig new_cfg;
    if (!new_cfg.load_from_file(g_config_path)) {
        LOG_ERROR("[SIGHUP] ❌ 配置文件加载失败: {}", g_config_path);
        return;
    }

    g_pipeline->reload_config(new_cfg);
}

int main(int argc, char* argv[])
{
    voice::init_logger("ws_voice_server.log");
    // ── 1. 加载配置 ──────────────────────────────────
    PipelineConfig cfg;

    std::string config_path;
    if (argc >= 2) {
        config_path = argv[1];
    }

    if (config_path.empty()) {
        // 自动查找
        std::string auto_path = PipelineConfig::auto_load_path();
        if (!auto_path.empty()) {
            cfg.load_from_file(auto_path);
            std::cout << "📄 已加载配置: " << auto_path << std::endl;
        } else {
            std::cout << "ℹ️  未找到 config.json，使用默认配置" << std::endl;
        }
    } else {
        if (cfg.load_from_file(config_path)) {
            std::cout << "📄 已加载配置: " << config_path << std::endl;
        } else {
            std::cerr << "⚠️ 加载配置文件失败: " << config_path << std::endl;
            return 1;
        }
    }

    // 端口覆盖
    WsServerConfig ws_cfg;
    ws_cfg.port = 9002;
    if (argc >= 3) {
        ws_cfg.port = std::atoi(argv[2]);
        if (ws_cfg.port <= 0) ws_cfg.port = 9002;
    }

    // ── 2. 初始化 VoicePipeline ───────────────────────
    VoicePipeline pipeline(cfg);
    if (!pipeline.initialize()) {
        std::cerr << "❌ VoicePipeline 初始化失败" << std::endl;
        return 1;
    }

    // 记住配置文件路径（供 SIGHUP 和 ConfigWatcher 使用）
    pipeline.set_config_path(config_path);
    g_config_path = config_path;
    g_pipeline = &pipeline;

    std::cout << std::endl;

    // ── 3. 启动 WebSocket 服务 ────────────────────────
    WsVoiceServer server(ws_cfg);
    server.set_pipeline(&pipeline);

    // 配置推流参数（从 PipelineConfig 映射到 VAD + ASR 配置）
    {
        VADConfig vad_cfg;
        vad_cfg.energy_threshold       = cfg.vad_energy_threshold;
        vad_cfg.min_speech_frames      = cfg.vad_min_speech_frames;
        vad_cfg.min_silence_frames     = cfg.vad_min_silence_frames;
        vad_cfg.pre_speech_frames      = cfg.vad_pre_speech_frames;
        vad_cfg.adaptive_factor        = cfg.vad_adaptive_factor;
        vad_cfg.min_energy_threshold   = cfg.vad_min_energy;
        vad_cfg.silence_cooldown_frames = cfg.vad_cooldown_frames;

        StreamingASRConfig asr_cfg;
        asr_cfg.backend           = cfg.streaming_asr_backend;
        asr_cfg.model_path        = cfg.streaming_asr_model.empty()
                                        ? cfg.asr_model_path : cfg.streaming_asr_model;
        asr_cfg.min_chunk_seconds = cfg.streaming_min_chunk;
        asr_cfg.chunk_interval    = cfg.streaming_chunk_intv;

        server.configure_streaming(vad_cfg, asr_cfg);
    }

    g_server = &server;
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP,  reload_handler);  // Layer 4.4: 热重载

    // ── 4. 启动配置文件监听 (Layer 4.4) ───────────────
    std::unique_ptr<ConfigWatcher> watcher;
    if (!config_path.empty()) {
        watcher = std::make_unique<ConfigWatcher>(config_path, [&pipeline, &config_path]() {
            PipelineConfig new_cfg;
            if (new_cfg.load_from_file(config_path)) {
                pipeline.reload_config(new_cfg);
            } else {
                LOG_ERROR("[ConfigWatcher] ❌ 配置文件加载失败: {}", config_path);
            }
        });
        watcher->start();
    }

    // 阻塞直到被信号中断
    server.run();

    // 清理
    if (watcher) watcher->stop();

    std::cout << "👋 服务已退出" << std::endl;
    return 0;
}
