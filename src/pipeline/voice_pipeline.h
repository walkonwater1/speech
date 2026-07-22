#pragma once
/**
 * 语音交互管线编排器
 *
 * Python 对应: src/pipeline.py → VoicePipeline
 *
 * 两种模式:
 *   - 单次模式 (现有):  process_text() / process_voice()
 *   - 交互模式 (新增):  run_interactive() — 持续监听 + 语音打断
 *
 * 交互模式架构:
 *
 *   ┌─ Capture 线程 ──────────────────────────────┐
 *   │  arecord → 读音频帧 → VAD                   │
 *   │    │                                         │
 *   │    ├─ 检测到语音 且 正在播放 → 打断播放 🔴   │
 *   │    ├─ 语音段结束 → 写入 WAV → push 到队列    │
 *   │    └─ generation++  (旧推理结果作废)         │
 *   └──────────────────────────────────────────────┘
 *              │ queue
 *   ┌─ Process 线程 ──────────────────────────────┐
 *   │  pop 语音段 → ASR → KWS → LLM → TTS         │
 *   │    │                                         │
 *   │    ├─ 每步前检查 generation → 过期则丢弃     │
 *   │    └─ play_async(aplay) → 等播放完成/被打断  │
 *   └──────────────────────────────────────────────┘
 */

#include <string>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "config.h"
#include "asr_engine.h"
#include "llm_engine.h"
#include "tts_engine.h"
#include "wake_word.h"
#include "speaker_verifier.h"
#include "chat_memory.h"
#include "audio_io.h"
#include "vad.h"
#include "skill_manager.h"
#include "embedding_engine.h"
#include "function_caller.h"
#include "react_engine.h"
#include "reflection.h"
#include "multi_agent.h"
#include "streaming_asr.h"
#include "voice_emotion.h"

class VoicePipeline {
public:
    explicit VoicePipeline(const PipelineConfig& cfg = PipelineConfig());

    /// 加载所有模型（阻塞，首次可能下载）
    bool initialize();

    /// 文字输入 → LLM → TTS → 播放
    /// @return LLM 回复文本
    std::string process_text(const std::string& text);

    /// 录音 → ASR → 唤醒词 → 声纹 → LLM → TTS → 播放
    /// @return LLM 回复文本（失败返回空串）
    std::string process_voice();

    /// 声纹注册
    bool enroll_speaker();

    /// 清空对话记忆
    void clear_memory();

    // ── 交互模式（可打断）─────────────────────────

    /// 启动持续监听 + 语音打断模式（阻塞当前线程直到用户中断）
    void run_interactive();

    /// 停止交互模式（可从信号处理函数调用）
    void stop_interactive();

private:
    PipelineConfig cfg_;

    ASREngine         asr_;
    LLMEngine         llm_;
    TTSEngine         tts_;
    WakeWordDetector  kws_;
    SpeakerVerifier   speaker_;
    ChatMemory        memory_;
    SkillManager      skill_mgr_;
    AudioRecorder     recorder_;
    std::shared_ptr<EmbeddingEngine> embed_;  // RAG 共用
    std::shared_ptr<FunctionCaller> fc_;      // Function Calling
    std::shared_ptr<ReActEngine>       react_;    // ReAct 多步推理
    std::shared_ptr<ReflectionEngine>  reflect_;     // 回复反思修正
    std::shared_ptr<MultiAgentEngine> multi_agent_; // 双Agent协作
    StreamingASR      stream_asr_;    // 流式 ASR (Layer 3.2)
    VoiceEmotionAnalyzer voice_emo_;  // 声学情感分析 (Layer 3.3)

    bool initialized_ = false;

    /// TTS 合成 + 播放（单次模式使用）
    void speak_and_play(const std::string& text,
                         const std::string& user_context = "");

    // ── 交互模式内部状态 ─────────────────────────

    std::atomic<bool> interactive_running_{false};
    std::atomic<bool> is_playing_{false};
    std::atomic<pid_t> player_pid_{-1};
    std::atomic<int>   generation_{0};      // 每次新语音段 +1，推理线程检查是否过期

    std::thread capture_thread_;
    std::thread process_thread_;

    // 语音段队列
    std::mutex              queue_mutex_;
    std::condition_variable queue_cv_;
    struct Segment {
        std::vector<float> samples;
        std::string text;       // 流式 ASR 预识别文本（非空时跳过 process_loop 中的 ASR）
        VoiceEmotionResult voice_emo;  // 声学情感（Layer 3.3）
        int generation;
    };
    std::queue<Segment>     segment_queue_;

    // ── 线程函数 ────────────────────────────────

    void capture_loop();
    void process_loop();

    /// 将 float 样本写入 WAV 文件
    static bool write_wav_float(const std::string& path,
                                const std::vector<float>& samples,
                                int sample_rate = 16000);
};
