#pragma once
/**
 * 流式语音识别引擎 (Streaming ASR)
 *
 * Layer 3.2: 语音交互深化 — 边说边识别，实时出字
 *
 * 与离线 ASR (ASREngine) 的关键区别:
 *   离线: 等一句话说完 → 整段 WAV → 一次识别 → 出结果
 *   流式: 边说边喂音频 → 增量解码 → 部分结果实时可见 → 最终结果
 *
 * 双后端架构:
 *   1. online  — sherpa-onnx OnlineRecognizer (Zipformer transducer)
 *      真正的逐帧增量解码，每次 decode 都可能更新识别文本
 *   2. chunked — 基于现有 SenseVoice 离线模型
 *      累积音频到缓冲区，周期性（~0.5s）重新识别全部累积音频
 *      语音结束时做最终完整识别
 *
 * 使用示例 (在 capture_loop 中):
 *
 *   StreamingASR asr;
 *   asr.initialize(cfg);
 *   asr.set_partial_callback([](const char* t) {
 *       std::cout << "🎤 " << t << "\r" << std::flush;
 *   });
 *
 *   asr.start_utterance();
 *   while (还在说话) {
 *       asr.feed(samples, n);
 *   }
 *   std::string result = asr.finalize();
 */

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// 前向声明 — 避免头文件依赖
struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineStream;

// ── 配置 ──────────────────────────────────────────────

struct StreamingASRConfig {
    /// 模型路径
    ///   online:  Zipformer 模型目录 (encoder/decoder/joiner/tokens)
    ///   chunked: SenseVoice 模型目录 (model.int8.onnx + tokens.txt)
    std::string model_path;

    /// 后端: "online" | "chunked"（默认 chunked，使用现有 SenseVoice）
    std::string backend = "chunked";

    int sample_rate    = 16000;
    int num_threads    = 4;
    std::string decoding_method = "greedy_search";

    // chunked 后端专用
    float min_chunk_seconds   = 0.8f;  // 最短累积多久才触发部分识别
    float chunk_interval      = 0.5f;  // 部分识别的最小间隔
};

// ── 流式 ASR 引擎 ──────────────────────────────────────

class StreamingASR {
public:
    /// 部分结果回调: void(const char* partial_text)
    using PartialCallback = std::function<void(const char*)>;

    StreamingASR();
    ~StreamingASR();

    StreamingASR(const StreamingASR&) = delete;
    StreamingASR& operator=(const StreamingASR&) = delete;

    // ── 生命周期 ──────────────────────────────────────

    /// 加载模型
    bool initialize(const StreamingASRConfig& cfg);
    bool initialized() const { return initialized_; }
    const std::string& backend() const { return cfg_.backend; }

    /// 设置部分结果回调（每次部分识别有变化时触发）
    void set_partial_callback(PartialCallback cb) { partial_cb_ = std::move(cb); }

    // ── 单次语音输入 API ─────────────────────────────

    /// 开始一次新的语音输入（重置所有状态）
    void start_utterance();

    /// 喂入音频帧 (float, mono, [-1, 1], 16kHz)
    void feed(const float* samples, int n);

    /// 获取当前部分识别文本（可能为 nullptr 表示尚无结果）
    const char* partial();

    /// 通知语音结束，执行最终解码并返回完整识别文本
    std::string finalize();

    /// 取消当前语音输入（用户打断等）
    void cancel();

    /// 当前累积音频时长（秒）
    float audio_duration() const;

private:
    StreamingASRConfig cfg_;
    bool initialized_ = false;

    // ── 回调 ─────────────────────────────────────────
    PartialCallback partial_cb_;

    // ── online 后端状态 ──────────────────────────────
    const SherpaOnnxOnlineRecognizer* online_rec_ = nullptr;
    const SherpaOnnxOnlineStream*     online_stream_ = nullptr;
    std::string online_partial_;   // 上次 partial() 返回的文本（用于去重）

    // ── chunked 后端状态 ─────────────────────────────
    void* chunked_engine_ = nullptr;   // ASREngine*（避免头文件依赖）
    std::vector<float> audio_buf_;
    std::string partial_text_;
    float last_chunk_time_ = 0.0f;
    int   chunk_count_     = 0;

    // ── 内部方法 ─────────────────────────────────────
    bool init_online();
    bool init_chunked();
    void decode_online();            // online: 解码就绪帧 → 更新 online_partial_
    void run_chunked_partial();      // chunked: 对累积音频做部分识别
    std::string run_chunked_final(); // chunked: 完整音频最终识别
};
