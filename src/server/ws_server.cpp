/**
 * WebSocket 语音服务端 — 实现
 *
 * Layer 4.1: 服务化
 *
 * 依赖: websocketpp (header-only) + Boost.Asio
 */

#include "ws_server.h"
#include "voice_pipeline.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <thread>
#include <chrono>
#include "logger.h"

using json = nlohmann::json;

// ── WebSocket++ 类型 ──────────────────────────────────

typedef websocketpp::server<websocketpp::config::asio> WsServer;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::frame::opcode::text;

// ── PIMPL 实现 ────────────────────────────────────────

struct WsVoiceServer::Impl {
    WsServer server;
    std::thread io_thread;  // ASIO 事件循环线程（避免阻塞）

    // 连接句柄 → 是否经过身份识别的映射
    std::map<connection_hdl, bool, std::owner_less<connection_hdl>> identified;
    std::mutex conn_mtx;
};

// ── 构造 / 析构 ──────────────────────────────────────

WsVoiceServer::WsVoiceServer(const WsServerConfig& cfg)
    : cfg_(cfg)
    , impl_(std::make_unique<Impl>())
{
    auto& srv = impl_->server;

    // 禁止日志洪水
    srv.set_access_channels(websocketpp::log::alevel::none);
    srv.set_error_channels(websocketpp::log::elevel::fatal);

    // 初始化 ASIO
    srv.init_asio();
    srv.set_reuse_addr(true);
}

WsVoiceServer::~WsVoiceServer()
{
    stop();
}

void WsVoiceServer::set_pipeline(VoicePipeline* pipeline)
{
    pipeline_ = pipeline;
}

// ── 启动 / 停止 ──────────────────────────────────────

void WsVoiceServer::run()
{
    if (!pipeline_) {
        LOG_ERROR("❌ [WS] VoicePipeline 未设置");
        return;
    }

    auto& srv = impl_->server;

    // ── 注册回调 ──────────────────────────────────────

    // 连接打开
    srv.set_open_handler([this](connection_hdl hdl) {
        connection_count_++;
        std::cout << "   [WS] 🔗 客户端连接 ("
                  << connection_count_.load() << " 活跃)" << std::endl;

        json welcome;
        welcome["type"]    = "welcome";
        welcome["version"] = "0.1.0";
        welcome["message"] = "语音助手 WebSocket 服务已就绪";

        try {
            impl_->server.send(hdl, welcome.dump(), text);
        } catch (...) {}
    });

    // 连接关闭
    srv.set_close_handler([this](connection_hdl /*hdl*/) {
        connection_count_--;
        std::cout << "   [WS] 🔌 客户端断开 ("
                  << connection_count_.load() << " 活跃)" << std::endl;
    });

    // 消息处理
    srv.set_message_handler([this](connection_hdl hdl, WsServer::message_ptr msg) {
        try {
            std::string response = handle_message(msg->get_payload());
            if (!response.empty()) {
                impl_->server.send(hdl, response, text);
            }
        } catch (const std::exception& e) {
            json err;
            err["type"]    = ws_protocol::TYPE_ERROR;
            err["message"] = std::string("处理消息异常: ") + e.what();
            try {
                impl_->server.send(hdl, err.dump(), text);
            } catch (...) {}
        }
    });

    // ── 启动监听 ──────────────────────────────────────

    try {
        srv.listen(cfg_.port);
        srv.start_accept();

        LOG_INFO("std::endl");
        LOG_INFO("========================================================");
        LOG_INFO("  🌐 WebSocket 语音服务已启动");
        std::cout << "  ws://0.0.0.0:" << cfg_.port << std::endl;
        LOG_INFO("  协议: JSON (type + content)");
        LOG_INFO("  示例: {\"type\":\"text\",\"content\":\"你好\"}");
        LOG_INFO("========================================================");
        LOG_INFO("std::endl");

        running_ = true;

        // 阻塞运行
        srv.run();
    } catch (const websocketpp::exception& e) {
        std::cerr << "❌ [WS] 启动失败: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "❌ [WS] 异常: " << e.what() << std::endl;
    }

    running_ = false;
}

void WsVoiceServer::stop()
{
    if (!running_) return;

    LOG_INFO("\n⏹️  [WS] 正在停止服务...");

    try {
        impl_->server.stop_listening();

        // 关闭所有连接
        std::lock_guard<std::mutex> lk(impl_->conn_mtx);
        // (websocketpp 的 stop() 会自动关闭连接)

        impl_->server.stop();
    } catch (...) {}

    running_ = false;
    LOG_INFO("   [WS] 服务已停止");
}

// ── 消息路由 ──────────────────────────────────────────

std::string WsVoiceServer::handle_message(const std::string& raw_json)
{
    json msg;
    try {
        msg = json::parse(raw_json);
    } catch (const json::parse_error& e) {
        json err;
        err["type"]    = ws_protocol::TYPE_ERROR;
        err["message"] = std::string("JSON 解析失败: ") + e.what();
        return err.dump();
    }

    std::string type = msg.value("type", "");

    if (type == ws_protocol::TYPE_TEXT) {
        std::string content = msg.value("content", "");
        if (content.empty()) {
            json err;
            err["type"]    = ws_protocol::TYPE_ERROR;
            err["message"] = "text 消息缺少 content";
            return err.dump();
        }
        return process_text_message(content);
    }

    if (type == ws_protocol::TYPE_AUDIO) {
        std::string data = msg.value("data", "");
        if (data.empty()) {
            json err;
            err["type"]    = ws_protocol::TYPE_ERROR;
            err["message"] = "audio 消息缺少 data";
            return err.dump();
        }
        return process_audio_message(data);
    }

    if (type == ws_protocol::TYPE_ENROLL) {
        std::string name = msg.value("name", "");
        return process_enroll_message(name);
    }

    if (type == ws_protocol::TYPE_IDENTIFY) {
        return process_identify_message();
    }

    // 未知消息类型
    json err;
    err["type"]    = ws_protocol::TYPE_ERROR;
    err["message"] = "未知消息类型: " + type;
    err["supported"] = {"text", "audio", "enroll", "identify"};
    return err.dump();
}

// ── 文本处理 ──────────────────────────────────────────

std::string WsVoiceServer::process_text_message(const std::string& content)
{
    std::cout << "   [WS] 💬 \"" << content << "\"" << std::endl;

    std::string reply;
    {
        std::lock_guard<std::mutex> lk(pipeline_mutex_);
        reply = pipeline_->process_text(content);
    }

    json resp;
    resp["type"] = ws_protocol::TYPE_REPLY;
    resp["text"] = reply;
    resp["emotion"] = "";  // TODO: 从 pipeline 获取韵律信息

    std::cout << "   [WS]    → \"" << reply << "\"" << std::endl;
    return resp.dump();
}

// ── 音频处理 ──────────────────────────────────────────

std::string WsVoiceServer::process_audio_message(const std::string& base64_wav)
{
    std::cout << "   [WS] 🎤 收到音频 ("
              << base64_wav.size() / 1024 << " KB base64)" << std::endl;

    // 解码 base64 → 写入临时 WAV
    std::string wav_data = base64_decode(base64_wav);
    if (wav_data.empty()) {
        json err;
        err["type"]    = ws_protocol::TYPE_ERROR;
        err["message"] = "base64 解码失败";
        return err.dump();
    }

    const std::string wav_path = "temp_ws_audio.wav";
    {
        std::ofstream f(wav_path, std::ios::binary);
        f.write(wav_data.data(), wav_data.size());
    }

    // 通过 pipeline 处理（使用文件路径，跳过录音步骤）
    std::string reply;
    {
        std::lock_guard<std::mutex> lk(pipeline_mutex_);
        reply = pipeline_->process_voice_file(wav_path);
    }

    std::remove(wav_path.c_str());

    // 读取合成的 TTS 音频
    std::string audio_b64;
    const std::string tts_file = "temp_reply.wav";
    std::ifstream af(tts_file, std::ios::binary);
    if (af.is_open()) {
        std::ostringstream oss;
        oss << af.rdbuf();
        audio_b64 = base64_encode(
            (const unsigned char*)oss.str().data(), oss.str().size());
        af.close();
        std::remove(tts_file.c_str());
    }

    json resp;
    resp["type"]  = ws_protocol::TYPE_REPLY;
    resp["text"]  = reply;
    if (!audio_b64.empty()) {
        resp["audio"]      = audio_b64;
        resp["audio_fmt"]  = "wav";
    }

    return resp.dump();
}

// ── 声纹注册 ──────────────────────────────────────────

std::string WsVoiceServer::process_enroll_message(const std::string& /*name*/)
{
    json resp;
    resp["type"] = ws_protocol::TYPE_ENROLLED;
    resp["status"] = "not_available_in_ws";
    resp["message"] = "请在 CLI 模式下使用 enroll 命令注册声纹";
    return resp.dump();
}

// ── 身份识别 ──────────────────────────────────────────

std::string WsVoiceServer::process_identify_message()
{
    json resp;
    resp["type"] = ws_protocol::TYPE_IDENTITY;
    resp["name"] = "";  // 未实现: 需要音频输入
    resp["status"] = "no_audio";
    return resp.dump();
}

// ── Base64 编解码 ─────────────────────────────────────

static const char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string WsVoiceServer::base64_decode(const std::string& encoded)
{
    std::string out;
    out.reserve(encoded.size() * 3 / 4);

    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;

        int idx = -1;
        if (c >= 'A' && c <= 'Z') idx = c - 'A';
        else if (c >= 'a' && c <= 'z') idx = c - 'a' + 26;
        else if (c >= '0' && c <= '9') idx = c - '0' + 52;
        else if (c == '+') idx = 62;
        else if (c == '/') idx = 63;

        if (idx < 0) continue;

        val = (val << 6) + idx;
        valb += 6;
        if (valb >= 0) {
            out.push_back((char)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string WsVoiceServer::base64_encode(const unsigned char* data, size_t len)
{
    std::string out;
    out.reserve((len + 2) / 3 * 4);

    int val = 0, valb = -6;
    for (size_t i = 0; i < len; ++i) {
        val = (val << 8) + data[i];
        valb += 8;
        while (valb >= 0) {
            out.push_back(BASE64_CHARS[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        out.push_back(BASE64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

std::string WsVoiceServer::file_to_base64(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";

    std::ostringstream oss;
    oss << f.rdbuf();
    std::string data = oss.str();
    return base64_encode((const unsigned char*)data.data(), data.size());
}
