#pragma once
/**
 * WebSocket 语音服务端 (WebSocket Voice Server)
 *
 * Layer 4.1: 服务化 — 把语音管线封装为 WebSocket 服务
 *
 * 架构:
 *   ┌─ WebSocket Server (port 9002) ──────────────────────┐
 *   │  ┌─ Session 1 ───────────────────────────────────┐  │
 *   │  │  connection → JSON router → VoicePipeline     │  │
 *   │  └──────────────────────────────────────────────┘  │
 *   │  ┌─ Session 2 ───────────────────────────────────┐  │
 *   │  │  connection → JSON router → VoicePipeline     │  │
 *   │  └──────────────────────────────────────────────┘  │
 *   │  ... (共享 VoicePipeline, 互斥访问)                 │
 *   └────────────────────────────────────────────────────┘
 *
 * JSON 协议:
 *   客户端 → 服务端:
 *     {"type":"text",   "content":"今天天气怎么样"}
 *     {"type":"audio",  "data":"<base64 wav>"}
 *     {"type":"enroll", "name":"张三"}
 *     {"type":"identify"}
 *   服务端 → 客户端:
 *     {"type":"reply",  "text":"今天天气晴朗...", "emotion":"中性"}
 *     {"type":"audio",  "data":"<base64 wav>",   "format":"wav"}
 *     {"type":"error",  "message":"..."}
 *     {"type":"enrolled","name":"张三","status":"ok"}
 */

#include <string>
#include <memory>
#include <mutex>
#include <map>
#include <atomic>
#include <functional>

// 前向声明
class VoicePipeline;

// ── 协议消息类型 ──────────────────────────────────────

namespace ws_protocol {

// 请求类型
inline constexpr const char* TYPE_TEXT     = "text";
inline constexpr const char* TYPE_AUDIO    = "audio";
inline constexpr const char* TYPE_ENROLL   = "enroll";
inline constexpr const char* TYPE_IDENTIFY = "identify";

// 响应类型
inline constexpr const char* TYPE_REPLY    = "reply";
inline constexpr const char* TYPE_AUDIO_OUT = "audio_out";
inline constexpr const char* TYPE_ERROR    = "error";
inline constexpr const char* TYPE_ENROLLED = "enrolled";
inline constexpr const char* TYPE_IDENTITY = "identity";

}

// ── 服务端配置 ────────────────────────────────────────

struct WsServerConfig {
    int     port        = 9002;
    int     num_threads = 4;         // ASIO 线程数
    bool    enable_cors = true;
    int     max_message_size = 10 * 1024 * 1024;  // 10MB（音频可能较大）
};

// ── WebSocket 语音服务 ────────────────────────────────

class WsVoiceServer {
public:
    /// 构造但尚未启动
    explicit WsVoiceServer(const WsServerConfig& cfg = {});

    ~WsVoiceServer();

    WsVoiceServer(const WsVoiceServer&) = delete;
    WsVoiceServer& operator=(const WsVoiceServer&) = delete;

    /// 设置共享的 VoicePipeline（必须先初始化好）
    void set_pipeline(VoicePipeline* pipeline);

    /// 启动服务（阻塞当前线程）
    void run();

    /// 停止服务（可从信号处理函数调用）
    void stop();

    /// 是否正在运行
    bool running() const { return running_.load(); }

    /// 活跃连接数
    int connection_count() const { return connection_count_.load(); }

private:
    WsServerConfig cfg_;
    VoicePipeline* pipeline_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<int>  connection_count_{0};

    // 管道互斥锁（序列化对共享 VoicePipeline 的访问）
    std::mutex pipeline_mutex_;

    // 内部实现（PIMPL 隐藏 websocketpp 头文件依赖）
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // ── 消息处理 ─────────────────────────────────────

    /// 处理 JSON 文本消息
    std::string handle_message(const std::string& raw_json);

    /// 处理文本输入
    std::string process_text_message(const std::string& content);

    /// 处理音频输入（base64 WAV → 保存 → ASR → LLM → TTS）
    std::string process_audio_message(const std::string& base64_wav);

    /// 处理声纹注册
    std::string process_enroll_message(const std::string& name);

    /// 处理身份识别
    std::string process_identify_message();

    /// 将 base64 解码为二进制
    static std::string base64_decode(const std::string& encoded);

    /// 将二进制编码为 base64
    static std::string base64_encode(const unsigned char* data, size_t len);

    /// 读取文件为 base64
    static std::string file_to_base64(const std::string& path);
};
