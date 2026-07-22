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

#include <iostream>
#include <csignal>
#include <cstdlib>

static WsVoiceServer* g_server = nullptr;

// ── 信号处理 ──────────────────────────────────────────

static void signal_handler(int sig)
{
    std::cout << "\n📡 收到信号 " << sig << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[])
{
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

    std::cout << std::endl;

    // ── 3. 启动 WebSocket 服务 ────────────────────────
    WsVoiceServer server(ws_cfg);
    server.set_pipeline(&pipeline);

    g_server = &server;
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // 阻塞直到被信号中断
    server.run();

    std::cout << "👋 服务已退出" << std::endl;
    return 0;
}
