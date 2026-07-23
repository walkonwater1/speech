/**
 * WebSocket 语音服务端 — 实现
 *
 * Layer 4.1: 服务化
 *
 * 依赖: websocketpp (header-only) + Boost.Asio
 */

#include "ws_server.h"
#include "voice_pipeline.h"
#include "streaming_session.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdint>
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

void WsVoiceServer::configure_streaming(const VADConfig& vad_cfg,
                                          const StreamingASRConfig& asr_cfg)
{
    vad_cfg_ = vad_cfg;
    stream_asr_cfg_ = asr_cfg;
    stream_configured_ = true;
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
    srv.set_close_handler([this](connection_hdl hdl) {
        connection_count_--;
        // 清理推流会话
        uint64_t conn_id = reinterpret_cast<uint64_t>(hdl.lock().get());
        {
            std::lock_guard<std::mutex> lk(session_mtx_);
            sessions_.erase(conn_id);
        }
        std::cout << "   [WS] 🔌 客户端断开 ("
                  << connection_count_.load() << " 活跃)" << std::endl;
    });

    // 消息处理（支持 text + binary + 推流控制）
    srv.set_message_handler([this](connection_hdl hdl, WsServer::message_ptr msg) {
        auto op = msg->get_opcode();
        uint64_t conn_id = reinterpret_cast<uint64_t>(hdl.lock().get());

        if (op == websocketpp::frame::opcode::binary) {
            // ── 二进制帧 → 推流音频 ──
            StreamingSession* session = nullptr;
            {
                std::lock_guard<std::mutex> lk(session_mtx_);
                auto it = sessions_.find(conn_id);
                if (it != sessions_.end()) session = it->second.get();
            }

            if (!session) {
                json err;
                err["type"] = ws_protocol::TYPE_ERROR;
                err["message"] = "推流未启动，请先发送 {\"type\":\"stream_start\"}";
                try { impl_->server.send(hdl, err.dump(), text); } catch (...) {}
                return;
            }

            const std::string& payload = msg->get_payload();
            const int16_t* raw = reinterpret_cast<const int16_t*>(payload.data());
            int n_samples = payload.size() / sizeof(int16_t);
            if (n_samples == 0) return;

            std::string event_json = session->process_audio_frame(raw, n_samples);
            if (!event_json.empty()) {
                try { impl_->server.send(hdl, event_json, text); } catch (...) {}
            }

            // 语音段完成 → LLM + TTS
            if (session->segment_complete()) {
                std::string final_text = session->final_text();

                std::string reply_text;
                std::string tts_wav = "temp_ws_stream_"
                    + std::to_string(conn_id) + ".wav";
                {
                    std::lock_guard<std::mutex> lk(pipeline_mutex_);
                    reply_text = pipeline_->process_text_for_ws(final_text, tts_wav);
                }

                // 读取 TTS WAV → base64
                std::string audio_b64 = file_to_base64(tts_wav);
                std::remove(tts_wav.c_str());

                json resp;
                resp["type"] = ws_protocol::TYPE_REPLY;
                resp["text"] = reply_text;
                if (!audio_b64.empty()) {
                    resp["audio"] = audio_b64;
                    resp["audio_fmt"] = "wav";
                }
                try { impl_->server.send(hdl, resp.dump(), text); } catch (...) {}

                session->reset();
            }
            return;
        }

        // ── 文本帧：先检查推流控制消息 ──
        std::string payload = msg->get_payload();
        try {
            auto j = json::parse(payload);
            std::string type = j.value("type", "");

            if (type == ws_protocol::TYPE_STREAM_START) {
                std::string resp = process_stream_start(conn_id);
                if (!resp.empty()) impl_->server.send(hdl, resp, text);
                return;
            }
            if (type == ws_protocol::TYPE_STREAM_END) {
                // 强制结束 → 可能产生 speech_end
                std::string event_json = process_stream_end(conn_id);
                if (!event_json.empty()) {
                    impl_->server.send(hdl, event_json, text);
                }
                // 如果有完整语音段 → LLM+TTS
                StreamingSession* session = nullptr;
                {
                    std::lock_guard<std::mutex> lk(session_mtx_);
                    auto it = sessions_.find(conn_id);
                    if (it != sessions_.end()) session = it->second.get();
                }
                if (session && session->segment_complete()) {
                    std::string final_text = session->final_text();
                    std::string reply_text;
                    std::string tts_wav = "temp_ws_stream_"
                        + std::to_string(conn_id) + ".wav";
                    {
                        std::lock_guard<std::mutex> lk(pipeline_mutex_);
                        reply_text = pipeline_->process_text_for_ws(final_text, tts_wav);
                    }
                    std::string audio_b64 = file_to_base64(tts_wav);
                    std::remove(tts_wav.c_str());
                    json resp;
                    resp["type"] = ws_protocol::TYPE_REPLY;
                    resp["text"] = reply_text;
                    if (!audio_b64.empty()) {
                        resp["audio"] = audio_b64;
                        resp["audio_fmt"] = "wav";
                    }
                    try { impl_->server.send(hdl, resp.dump(), text); } catch (...) {}
                    session->reset();
                }
                return;
            }
        } catch (...) {}

        // ── 普通文本消息 ──
        try {
            std::string response = handle_message(payload);
            if (!response.empty()) {
                impl_->server.send(hdl, response, text);
            }
        } catch (const std::exception& e) {
            json err;
            err["type"]    = ws_protocol::TYPE_ERROR;
            err["message"] = std::string("处理消息异常: ") + e.what();
            try { impl_->server.send(hdl, err.dump(), text); } catch (...) {}
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
    err["supported"] = {"text", "audio", "enroll", "identify", "stream_start", "stream_end"};
    return err.dump();
}

// ── 推流控制 ──────────────────────────────────────────

std::string WsVoiceServer::process_stream_start(uint64_t conn_id)
{
    if (!stream_configured_) {
        json err;
        err["type"] = ws_protocol::TYPE_ERROR;
        err["message"] = "推流未配置，请联系管理员";
        return err.dump();
    }

    std::lock_guard<std::mutex> lk(session_mtx_);
    sessions_[conn_id] = std::make_unique<StreamingSession>(vad_cfg_, stream_asr_cfg_);

    std::cout << "   [WS] 🎙️ 推流开始 (conn=" << conn_id
              << ", 活跃推流=" << sessions_.size() << ")" << std::endl;

    json resp;
    resp["type"] = ws_protocol::TYPE_STREAM_READY;
    return resp.dump();
}

std::string WsVoiceServer::process_stream_end(uint64_t conn_id)
{
    StreamingSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lk(session_mtx_);
        auto it = sessions_.find(conn_id);
        if (it != sessions_.end()) {
            session = it->second.get();
        }
    }

    if (!session) return "";

    std::string event_json = session->force_end_utterance();
    if (session->segment_complete()) {
        // 需要在 lambda 中处理 LLM+TTS（需要 connection_hdl 来发送 reply）
        // 这里只返回 speech_end event
    }
    return event_json;
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

    const std::string in_wav = "temp_ws_audio.wav";
    const std::string tts_wav = "temp_ws_reply.wav";
    {
        std::ofstream f(in_wav, std::ios::binary);
        f.write(wav_data.data(), wav_data.size());
    }

    std::string reply;
    {
        std::lock_guard<std::mutex> lk(pipeline_mutex_);

        // ASR 转写
        std::string prompt = pipeline_->transcribe_file(in_wav);
        std::remove(in_wav.c_str());

        if (prompt.empty()) {
            json err;
            err["type"]    = ws_protocol::TYPE_ERROR;
            err["message"] = "语音识别未检测到内容";
            return err.dump();
        }

        // LLM + TTS（不本地播放）
        reply = pipeline_->process_text_for_ws(prompt, tts_wav);
    }

    // 读取 TTS 音频 → base64
    std::string audio_b64 = file_to_base64(tts_wav);
    std::remove(tts_wav.c_str());

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
