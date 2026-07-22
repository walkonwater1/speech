/**
 * 语音交互管线编排器
 *
 * Python 对应: src/pipeline.py → VoicePipeline
 *
 * 数据流:
 *   单次模式:
 *     文字:   prompt ──→ LLM ──→ TTS ──→ 播放 → 更新记忆
 *     语音:   麦克风 ──→ ASR ──→ KWS ──→ SV ──→ LLM ──→ TTS ──→ 播放
 *
 *   交互模式:
 *     Capture 线程:  arecord → VAD → 语音段 → queue
 *     Process 线程:  queue → ASR → KWS → LLM → TTS → 异步播放
 *                    新语音到达时 → 打断播放 + generation++ → 旧结果丢弃
 */

#include "voice_pipeline.h"
#include "denoiser.h"

#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// ── 构造 / 初始化 ────────────────────────────────────

VoicePipeline::VoicePipeline(const PipelineConfig& cfg)
    : cfg_(cfg)
    , asr_(cfg.asr_model_path)
    , llm_(cfg.ollama_host, cfg.llm_model, cfg.system_prompt)
    , tts_(cfg.tts_rate, cfg.tts_voice, cfg.tts_backend, cfg.piper_model_path)
    , kws_(cfg.wake_word)
    , speaker_(cfg.sv_enroll_dir, cfg.sv_threshold)
    , memory_(cfg.max_rounds, cfg.max_tokens)
    , recorder_(cfg.sample_rate)
{
    // 根据配置启用/禁用技能
    skill_mgr_.set_enabled("weather",    cfg.skill_weather);
    skill_mgr_.set_enabled("time",       cfg.skill_time);
    skill_mgr_.set_enabled("web_search", cfg.skill_web_search);
}

bool VoicePipeline::initialize()
{
    std::cout << "============ 初始化管线 ============" << std::endl;

    if (!asr_.initialize()) {
        std::cerr << "❌ ASR 初始化失败" << std::endl;
    }

    if (!speaker_.initialize()) {
        std::cerr << "❌ 声纹模型初始化失败" << std::endl;
    }

    // ── 初始化多用户声纹库 (Layer 3.4) ──────────────────
    {
        float vp_threshold = cfg_.sv_threshold;
        std::string vp_dir = cfg_.sv_enroll_dir.empty()
            ? "voiceprint_library" : cfg_.sv_enroll_dir + "_library";
        if (voiceprint_.initialize(vp_dir, vp_threshold)) {
            voiceprint_.set_event_callback(
                [](const std::string& event, const std::string& detail) {
                    std::cout << "   [Voiceprint] 事件: " << event
                              << " → " << detail << std::endl;
                });
        }
    }

    if (!tts_.initialize()) {
        std::cerr << "❌ TTS 初始化失败" << std::endl;
        return false;
    }

    // ── 初始化 EmbeddingEngine + RAG ──────────────────
    if (cfg_.skill_rag) {
        std::cout << "[Embedding] 初始化 (Ollama /api/embed) ..." << std::endl;
        embed_ = std::make_shared<EmbeddingEngine>(cfg_.ollama_host, cfg_.llm_model);
        skill_mgr_.register_rag(embed_, cfg_.rag_docs_dir);
        skill_mgr_.set_enabled("rag", true);
    }

    // ── 初始化 Function Calling ──────────────────────
    if (cfg_.fc_enabled) {
        std::string fc_model = cfg_.fc_model.empty() ? cfg_.llm_model : cfg_.fc_model;
        std::cout << "[FunctionCalling] 启用 LLM 驱动工具选择 (模型: "
                  << fc_model << ")" << std::endl;
        fc_ = std::make_shared<FunctionCaller>(cfg_.ollama_host, fc_model);
        skill_mgr_.set_function_caller(fc_);
    } else {
        std::cout << "[FunctionCalling] 禁用，使用关键字匹配降级方案" << std::endl;
        skill_mgr_.set_function_calling_enabled(false);
    }

    // ── 初始化 ReAct 多步推理 ────────────────────────
    if (cfg_.react_enabled) {
        std::cout << "[ReAct] 🧠 启用多步推理 (最多 "
                  << cfg_.react_max_steps << " 步)" << std::endl;
        react_ = std::make_shared<ReActEngine>(
            cfg_.ollama_host, cfg_.llm_model, cfg_.system_prompt);
    } else {
        std::cout << "[ReAct] 禁用，使用单步推理" << std::endl;
    }

    // ── 初始化 Reflection 反思 ────────────────────────
    if (cfg_.reflect_enabled) {
        std::string rmodel = cfg_.reflect_model.empty()
            ? cfg_.llm_model : cfg_.reflect_model;
        std::cout << "[Reflect] 🔍 启用回复反思修正 (模型: "
                  << rmodel << ")" << std::endl;
        reflect_ = std::make_shared<ReflectionEngine>(cfg_.ollama_host, rmodel);
    } else {
        std::cout << "[Reflect] 禁用" << std::endl;
    }

    // ── 初始化 Multi-Agent ───────────────────────────
    if (cfg_.multi_agent_enabled) {
        std::string cmodel = cfg_.ma_critic_model.empty()
            ? cfg_.llm_model : cfg_.ma_critic_model;
        std::cout << "[MultiAgent] 🤝 启用双 Agent 协作 (Critic: "
                  << cmodel << ", 最多" << cfg_.ma_max_rounds << "轮)" << std::endl;
        multi_agent_ = std::make_shared<MultiAgentEngine>(cfg_.ollama_host);
    } else {
        std::cout << "[MultiAgent] 禁用" << std::endl;
    }

    // ── 初始化流式 ASR (Layer 3.2) ────────────────────
    if (cfg_.streaming_asr_enabled) {
        std::string sa_model = cfg_.streaming_asr_model.empty()
            ? cfg_.asr_model_path : cfg_.streaming_asr_model;
        std::cout << "[StreamASR] 🎤 流式语音识别 (后端: "
                  << cfg_.streaming_asr_backend << ")" << std::endl;

        StreamingASRConfig sa_cfg;
        sa_cfg.backend              = cfg_.streaming_asr_backend;
        sa_cfg.model_path           = sa_model;
        sa_cfg.min_chunk_seconds    = cfg_.streaming_min_chunk;
        sa_cfg.chunk_interval       = cfg_.streaming_chunk_intv;

        if (stream_asr_.initialize(sa_cfg)) {
            // 设置部分结果回调：实时显示识别文本
            stream_asr_.set_partial_callback([](const char* text) {
                std::cout << "   🎤 " << text << "\r" << std::flush;
            });
        } else {
            std::cout << "   ⚠️ 流式 ASR 初始化失败，回退到离线模式" << std::endl;
        }
    }

    std::cout << "[5/5] Ollama: " << cfg_.llm_model
              << " (" << cfg_.ollama_host << ")" << std::endl;

    std::cout << "   特性: ";
    if (kws_.enabled()) std::cout << "唤醒词=\"" << cfg_.wake_word << "\" ";
    std::cout << "声纹(阈值=" << cfg_.sv_threshold << ") ";
    std::cout << "记忆(最近" << cfg_.max_rounds << "轮)";
    if (cfg_.skill_rag) std::cout << " RAG(" << cfg_.rag_docs_dir << ")";
    std::cout << std::endl;

    std::cout << "   " << speaker_.status_text() << std::endl;
    std::cout << "   [Voiceprint] " << voiceprint_.status_text() << std::endl;
    std::cout << std::endl;

    initialized_ = true;
    return true;
}

// ── 单次模式 ────────────────────────────────────────

std::string VoicePipeline::process_text(const std::string& text)
{
    if (!initialized_) return "";

    std::string reply;

    // ── 策略 1: ReAct 多步推理（如果启用）─────────────
    if (react_) {
        auto tools = skill_mgr_.collect_function_defs();
        if (!tools.empty()) {
            auto exec_fn = [this](const std::string& name, const nlohmann::json& args) {
                return skill_mgr_.execute_tool(name, args, "");
            };

            std::string ctx = memory_.get_context();
            auto result = react_->run(text, tools, exec_fn, ctx, cfg_.react_max_steps);

            if (result.success && !result.final_answer.empty()) {
                reply = result.final_answer;
                // 不在此处返回，继续到下方 reflection + memory
            } else if (!result.success) {
                std::cout << "   [ReAct] 推理失败，降级到单步模式" << std::endl;
            }
        }
    }

    // ── 策略 2: Function Calling + LLM（降级方案）─────
    if (reply.empty()) {
        SkillResult sr = skill_mgr_.detect_and_execute(text);
        std::string extra = SkillManager::get_system_context();
        if (sr.hit) {
            extra += "\n" + sr.result_text;
            std::cout << "   [Skill] \"" << text << "\" → " << sr.skill_name << std::endl;
        }

        std::string context = memory_.get_context();
        reply = llm_.chat(text, context, extra);
    }

    if (reply.empty()) return "";

    // ── 质量优化：Multi-Agent 协作 或 Reflection 反思 ──
    if (multi_agent_) {
        std::string cmodel = cfg_.ma_critic_model.empty()
            ? cfg_.llm_model : cfg_.ma_critic_model;
        auto ma = multi_agent_->collaborate(
            text, reply, /*tool_context*/"",
            cfg_.system_prompt, cfg_.llm_model, cmodel, cfg_.ma_max_rounds);
        reply = ma.final_answer;
    } else if (reflect_) {
        auto r = reflect_->reflect(text, reply);
        reply = r.improved;
    }

    memory_.add(text, reply);
    speak_and_play(reply, text);

    return reply;
}

std::string VoicePipeline::process_voice()
{
    if (!initialized_) return "";

    const std::string wav_file = "temp_recording.wav";

    if (!recorder_.record(wav_file)) {
        return "";
    }

    std::string prompt = asr_.transcribe(wav_file);
    if (prompt.empty()) {
        std::cout << "   ⚠️ 未识别到语音" << std::endl;
        std::remove(wav_file.c_str());
        return "";
    }

    if (!kws_.detect(prompt)) {
        speak_and_play("请说出正确的唤醒词");
        std::remove(wav_file.c_str());
        return "";
    }

    // ── 多用户声纹识别 (Layer 3.4) ──────────────────────
    if (voiceprint_.has_any()) {
        auto id = voiceprint_.identify(wav_file);
        if (id.identified()) {
            // 自动切换到识别的用户
            if (id.name != voiceprint_.active_speaker()) {
                voiceprint_.set_active_speaker(id.name);
            }
            std::cout << "   👤 说话人: " << id.display_name
                      << " (置信度=" << id.confidence << ")" << std::endl;
        } else {
            speak_and_play("抱歉，我没有识别出你的声音，请先注册声纹");
            std::remove(wav_file.c_str());
            return "";
        }
    } else if (speaker_.has_enrolled()) {
        // 旧版单用户验证
        if (!speaker_.verify(wav_file)) {
            speak_and_play("声纹验证失败，我无法为您服务");
            std::remove(wav_file.c_str());
            return "";
        }
    } else {
        speak_and_play("请先注册声纹，输入 enroll 开始注册");
        std::remove(wav_file.c_str());
        return "";
    }

    std::remove(wav_file.c_str());

    std::string reply;

    // ── ReAct 多步推理（优先）─────────────────────────
    if (react_) {
        auto tools = skill_mgr_.collect_function_defs();
        if (!tools.empty()) {
            auto exec_fn = [this](const std::string& name, const nlohmann::json& args) {
                return skill_mgr_.execute_tool(name, args, "");
            };

            std::string ctx = memory_.get_context();
            auto result = react_->run(prompt, tools, exec_fn, ctx, cfg_.react_max_steps);

            if (result.success && !result.final_answer.empty()) {
                reply = result.final_answer;
            }
        }
    }

    // ── 降级：Function Calling → 关键字匹配 ─────────
    if (reply.empty()) {
        SkillResult sr = skill_mgr_.detect_and_execute(prompt);
        std::string extra = SkillManager::get_system_context();
        if (sr.hit) {
            extra += "\n" + sr.result_text;
            std::cout << "   [Skill] \"" << prompt << "\" → " << sr.skill_name << std::endl;
        }

        std::string context = memory_.get_context();
        reply = llm_.chat(prompt, context, extra);
    }

    if (reply.empty()) return "";

    // ── 质量优化 ───────────────────────────────────
    if (multi_agent_) {
        std::string cmodel = cfg_.ma_critic_model.empty()
            ? cfg_.llm_model : cfg_.ma_critic_model;
        auto ma = multi_agent_->collaborate(
            prompt, reply, "", cfg_.system_prompt,
            cfg_.llm_model, cmodel, cfg_.ma_max_rounds);
        reply = ma.final_answer;
    } else if (reflect_) {
        auto r = reflect_->reflect(prompt, reply);
        reply = r.improved;
    }

    memory_.add(prompt, reply);
    speak_and_play(reply, prompt);

    return reply;
}

bool VoicePipeline::enroll_speaker()
{
    const std::string wav_file = "temp_enroll.wav";

    std::cout << "   🎙️ 请说出你的名字（3秒后开始录音）..." << std::endl;
    speak_and_play("请在提示音后说出你的名字");

    if (!recorder_.record(wav_file)) {
        return false;
    }

    // 先用 ASR 获取用户名
    std::string user_name = asr_.transcribe(wav_file);
    if (user_name.empty()) {
        speak_and_play("没有识别到你说的名字，请重试");
        std::remove(wav_file.c_str());
        return false;
    }

    // 清理用户名（过滤中文标点和空格）
    std::string clean_name;
    for (size_t i = 0; i < user_name.size(); ) {
        unsigned char c = (unsigned char)user_name[i];
        // 跳过 ASCII 空格
        if (c == ' ') { i++; continue; }
        // 跳过 UTF-8 中文标点（3字节序列）
        if (c == 0xE3 || c == 0xEF) {
            i += 3;
            continue;
        }
        clean_name += user_name[i];
        i++;
    }

    // 重新录音（用于声纹注册，建议 >3 秒）
    std::cout << "   🎙️ 请说一段话（用于声纹注册，3秒后开始）..." << std::endl;
    speak_and_play("请说一小段话，用于声纹注册");
    std::remove(wav_file.c_str());

    if (!recorder_.record(wav_file)) {
        return false;
    }

    // 注册到声纹库
    if (!voiceprint_.enroll(wav_file, clean_name, clean_name)) {
        // 降级到旧版单用户注册
        if (!speaker_.enroll(wav_file)) {
            std::remove(wav_file.c_str());
            return false;
        }
        speak_and_play("声纹注册完成！");
    } else {
        std::string msg = "声纹注册完成！欢迎你，" + clean_name;
        speak_and_play(msg);
    }

    std::remove(wav_file.c_str());
    return true;
}

void VoicePipeline::clear_memory()
{
    memory_.clear();
    std::cout << "   🧹 对话记忆已清空" << std::endl;
}

void VoicePipeline::speak_and_play(const std::string& text,
                                     const std::string& user_context)
{
    std::cout << "   🔊 播放中..." << std::endl;

    if (cfg_.tts_backend == "piper") {
        tts_.synthesize(text, "", user_context);
    } else {
        const std::string tts_file = "temp_reply.wav";
        if (tts_.synthesize(text, tts_file, user_context)) {
            AudioPlayer::play(tts_file);
            std::remove(tts_file.c_str());
        }
    }
}

// ── 交互模式（可打断）───────────────────────────────

void VoicePipeline::run_interactive()
{
    if (!initialized_) {
        std::cerr << "❌ 管线未初始化" << std::endl;
        return;
    }

    // 避免僵尸进程
    signal(SIGCHLD, SIG_IGN);

    interactive_running_ = true;
    is_playing_ = false;
    player_pid_ = -1;
    generation_ = 0;

    // 清空队列
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        segment_queue_ = std::queue<Segment>();
    }

    std::cout << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "  🎤 交互模式已启动（支持语音打断）" << std::endl;
    std::cout << "  直接说话即可交互，机器人说话时你可以随时打断" << std::endl;
    std::cout << "  按 Ctrl+C 退出交互模式" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << std::endl;

    // 启动两个工作线程
    capture_thread_ = std::thread(&VoicePipeline::capture_loop, this);
    process_thread_ = std::thread(&VoicePipeline::process_loop, this);

    // 等待线程结束
    if (capture_thread_.joinable())  capture_thread_.join();
    if (process_thread_.joinable())  process_thread_.join();

    std::cout << std::endl << "👋 交互模式已退出" << std::endl;
}

void VoicePipeline::stop_interactive()
{
    std::cout << "\n⏹️  收到停止信号..." << std::endl;
    interactive_running_ = false;

    // 停止当前播放
    pid_t pid = player_pid_.load();
    if (pid > 0) {
        AudioPlayer::stop_async(pid);
        player_pid_ = -1;
    }

    // 唤醒 process 线程使其退出
    queue_cv_.notify_all();
}

// ── Capture 线程 ─────────────────────────────────────

void VoicePipeline::capture_loop()
{
    // 打开 arecord 管道: 持续输出 raw 16-bit PCM
    FILE* pipe = popen(
        "arecord -f S16_LE -r 16000 -c 1 -t raw -q 2>/dev/null", "r");
    if (!pipe) {
        std::cerr << "❌ 无法启动录音设备" << std::endl;
        interactive_running_ = false;
        queue_cv_.notify_all();
        return;
    }

    // 从 PipelineConfig 构造 VAD 配置
    VADConfig vad_cfg;
    vad_cfg.energy_threshold   = cfg_.vad_energy_threshold;
    vad_cfg.min_speech_frames  = cfg_.vad_min_speech_frames;
    vad_cfg.min_silence_frames = cfg_.vad_min_silence_frames;
    vad_cfg.pre_speech_frames  = cfg_.vad_pre_speech_frames;
    vad_cfg.adaptive_factor    = cfg_.vad_adaptive_factor;

    auto vad = create_vad(cfg_.vad_backend, vad_cfg);

    const int frame_samples = vad_cfg.frame_size_samples;   // 20ms @16kHz
    const int frame_bytes   = frame_samples * 2;  // int16 = 2 bytes

    std::vector<int16_t> raw_buf(frame_samples);
    std::vector<float>   float_buf(frame_samples);

    // 音频预处理器: 去直流偏置 + 噪声门
    Denoiser denoiser(16000);

    std::cout << "🎙️  麦克风已开启，开始监听... (VAD: " << cfg_.vad_backend << ")" << std::endl;

    while (interactive_running_) {
        // 读一帧音频
        size_t nread = fread(raw_buf.data(), 1, frame_bytes, pipe);
        if (nread != (size_t)frame_bytes) {
            if (interactive_running_) {
                std::cerr << "⚠️ 录音读取异常" << std::endl;
            }
            break;
        }

        // int16 → float [-1, 1]
        for (int i = 0; i < frame_samples; ++i) {
            float_buf[i] = raw_buf[i] / 32768.0f;
        }

        // 音频预处理: 去直流 + 噪声门
        denoiser.process_frame(float_buf.data(), frame_samples);

        // 持续喂预录音缓冲区
        vad->feed_pre_buffer(float_buf.data(), frame_samples);

        // VAD 处理
        VADState vad_state = vad->process_frame(float_buf.data(), frame_samples);

        // ── 流式 ASR：语音期间持续喂帧 ──────────────────
        // 注意：只喂语音帧（含 SPEECH_START/ONGOING），
        // VAD 的 pre_buffer（~300ms 静音）不喂入，影响可忽略
        bool in_speech = vad->in_speech();
        if (stream_asr_.initialized() && in_speech) {
            if (vad_state == VADState::SPEECH_START) {
                stream_asr_.start_utterance();
            }
            stream_asr_.feed(float_buf.data(), frame_samples);
        }

        // 🔴 打断：检测到语音开始 且 正在播放 → 立刻停止播放
        if (vad_state == VADState::SPEECH_START && is_playing_.load()) {
            pid_t pid = player_pid_.exchange(-1);
            if (pid > 0) {
                AudioPlayer::stop_async(pid);
                std::cout << "\n🔴 检测到语音，已打断当前播放！" << std::endl;
            }
            is_playing_ = false;
            // 取消旧的流式 ASR（开始新的）
            if (stream_asr_.initialized()) {
                stream_asr_.cancel();
                stream_asr_.start_utterance();
            }
        }

        // 语音段结束 → 入队（流式 ASR 预识别文字）
        if (vad->segment_ready()) {
            auto segment = vad->pop_segment();

            // 检查最小长度：至少 0.5 秒
            float duration = (float)segment.size() / 16000.0f;
            if (duration < 0.5f) {
                std::cout << "   ⏭️ 语音段太短 (" << duration << "s)，跳过" << std::endl;
                if (stream_asr_.initialized()) stream_asr_.cancel();
                continue;
            }

            int gen = generation_.fetch_add(1) + 1;

            // 流式 ASR 最终识别（在 capture 线程完成，隐藏 ASR 延迟）
            std::string stream_text;
            if (stream_asr_.initialized()) {
                stream_text = stream_asr_.finalize();
                std::cout << "\n📝 捕获语音段 #" << gen
                          << " (" << duration << "s)"
                          << " → \"" << stream_text << "\"" << std::endl;
            } else {
                std::cout << "\n📝 捕获语音段 #" << gen
                          << " (" << duration << "s)" << std::endl;
            }

            // ── 声学情感分析 (Layer 3.3) ──────────────────
            VoiceEmotionResult voice_emo;
            if (!segment.empty()) {
                voice_emo = voice_emo_.analyze(
                    segment.data(), (int)segment.size(), 16000);
                if (voice_emo.confidence > 0.0f) {
                    std::cout << "   🎭 " << voice_emo.label
                              << " (" << voice_emo.detail << ")"
                              << " 置信度=" << (int)(voice_emo.confidence * 100) << "%"
                              << std::endl;
                }
            }

            {
                std::lock_guard<std::mutex> lk(queue_mutex_);
                Segment seg;
                seg.samples    = std::move(segment);
                seg.text       = stream_text;   // 预识别文本
                seg.voice_emo  = voice_emo;     // 声学情感
                seg.generation = gen;
                segment_queue_.push(std::move(seg));
            }
            queue_cv_.notify_one();
        }
    }

    pclose(pipe);

    // 通知 process 线程退出
    interactive_running_ = false;
    queue_cv_.notify_all();
}

// ── Process 线程 ─────────────────────────────────────

void VoicePipeline::process_loop()
{
    while (true) {
        Segment seg;
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait(lk, [this] {
                return !segment_queue_.empty() || !interactive_running_;
            });

            if (!interactive_running_ && segment_queue_.empty()) {
                break;  // 退出
            }

            seg = std::move(segment_queue_.front());
            segment_queue_.pop();
        }

        int my_gen = seg.generation;

        // 检查是否已有更新的语音段（当前段已过期）
        if (my_gen < generation_.load()) {
            std::cout << "   ⏭️ 语音段 #" << my_gen << " 已过期，丢弃" << std::endl;
            continue;
        }

        // 1) 获取识别文本：优先使用流式 ASR 预识别结果
        std::string prompt;
        if (!seg.text.empty()) {
            // 流式 ASR 已在 capture 线程完成识别
            prompt = seg.text;
        } else {
            // 传统路径：写入 WAV → 离线 ASR
            const std::string wav_file = "temp_interactive_" + std::to_string(my_gen) + ".wav";
            if (!write_wav_float(wav_file, seg.samples, 16000)) {
                std::cerr << "   ❌ 写入 WAV 失败" << std::endl;
                continue;
            }
            prompt = asr_.transcribe(wav_file);
            std::remove(wav_file.c_str());
        }

        if (prompt.empty()) {
            std::cout << "   ⚠️ 未识别到有效语音" << std::endl;
            continue;
        }

        // 再次检查是否过期（ASR 可能耗时）
        if (my_gen < generation_.load()) {
            std::cout << "   ⏭️ 语音段 #" << my_gen << " 在 ASR 后过期" << std::endl;
            continue;
        }

        // 3) 唤醒词检查（可选的）／声纹识别
        if (kws_.enabled() && !kws_.detect(prompt)) {
            std::cout << "   ⚠️ 未检测到唤醒词，跳过" << std::endl;
            continue;
        }

        // 声纹识别（交互模式中可选，仅在声纹库有用户时启用）
        if (voiceprint_.has_any() && !seg.samples.empty()) {
            const std::string vp_wav = "temp_vp_" + std::to_string(my_gen) + ".wav";
            if (write_wav_float(vp_wav, seg.samples, 16000)) {
                auto id = voiceprint_.identify(vp_wav);
                std::remove(vp_wav.c_str());
                if (id.identified()) {
                    if (id.name != voiceprint_.active_speaker()) {
                        voiceprint_.set_active_speaker(id.name);
                    }
                }
            }
        }

        // 4) 推理（ReAct → Function Calling → 关键字匹配）
        // 获取活跃用户的 system prompt（声纹识别出的用户）
        std::string speaker_prompt = voiceprint_.active_system_prompt();
        std::string context = memory_.get_context();
        std::string reply;

        if (react_) {
            auto tools = skill_mgr_.collect_function_defs();
            if (!tools.empty()) {
                auto exec_fn = [this](const std::string& name, const nlohmann::json& args) {
                    return skill_mgr_.execute_tool(name, args, "");
                };
                auto result = react_->run(prompt, tools, exec_fn, context, cfg_.react_max_steps);
                if (result.success && !result.final_answer.empty()) {
                    reply = result.final_answer;
                }
            }
        }

        if (reply.empty()) {
            SkillResult sr = skill_mgr_.detect_and_execute(prompt);
            std::string extra = SkillManager::get_system_context();
            if (!speaker_prompt.empty()) {
                extra = speaker_prompt + "\n" + extra;
            }
            if (sr.hit) {
                extra += "\n" + sr.result_text;
                std::cout << "   [Skill] \"" << prompt << "\" → " << sr.skill_name << std::endl;
            }
            reply = llm_.chat(prompt, context, extra);
        }

        if (reply.empty()) continue;

        // 再次检查是否过期（ReAct/LLM 可能耗时较长）
        if (my_gen < generation_.load()) {
            std::cout << "   ⏭️ 语音段 #" << my_gen << " 在推理后过期，丢弃回复" << std::endl;
            continue;
        }

        // 5.5) 质量优化：Multi-Agent 或 Reflection
        if (multi_agent_) {
            std::string cmodel = cfg_.ma_critic_model.empty()
                ? cfg_.llm_model : cfg_.ma_critic_model;
            auto ma = multi_agent_->collaborate(
                prompt, reply, "", cfg_.system_prompt,
                cfg_.llm_model, cmodel, cfg_.ma_max_rounds);
            reply = ma.final_answer;
        } else if (reflect_) {
            auto r = reflect_->reflect(prompt, reply);
            reply = r.improved;
        }

        // 6) 更新记忆
        memory_.add(prompt, reply);

        // 7) TTS + 播放（传递声学情感用于韵律融合）
        const VoiceEmotionResult* ve = seg.voice_emo.confidence > 0.0f ? &seg.voice_emo : nullptr;
        if (cfg_.tts_backend == "piper") {
            // Piper 流式管道：边合成边播，阻塞直到播完
            std::cout << "   🔊 播放中..." << std::endl;
            is_playing_ = true;
            tts_.synthesize(reply, "", prompt, ve);
            is_playing_ = false;
        } else {
            const std::string tts_file = "temp_reply_interactive_" + std::to_string(my_gen) + ".wav";
            if (!tts_.synthesize(reply, tts_file, prompt, ve)) {
                std::cerr << "   ❌ TTS 合成失败" << std::endl;
                continue;
            }

            // 最后检查（TTS 后）
            if (my_gen < generation_.load()) {
                std::cout << "   ⏭️ 语音段 #" << my_gen << " 在 TTS 后过期" << std::endl;
                std::remove(tts_file.c_str());
                continue;
            }

            // 7) 异步播放（可被打断）
            std::cout << "   🔊 播放中..." << std::endl;
            is_playing_ = true;
            pid_t pid = AudioPlayer::play_async(tts_file);
            player_pid_ = pid;

            if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                pid_t expected = pid;
                player_pid_.compare_exchange_strong(expected, -1);
            }

            is_playing_ = false;
            std::remove(tts_file.c_str());
        }
    }
}

// ── WAV 写入工具 ──────────────────────────────────────

bool VoicePipeline::write_wav_float(const std::string& path,
                                     const std::vector<float>& samples,
                                     int sample_rate)
{
    // float → int16
    std::vector<int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        float s = samples[i];
        if (s > 1.0f)  s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        pcm[i] = (int16_t)(s * 32767.0f);
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    uint32_t data_size = (uint32_t)(pcm.size() * sizeof(int16_t));
    uint32_t chunk_size = 36 + data_size;

    // RIFF header
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&chunk_size), 4);
    out.write("WAVE", 4);

    // fmt chunk (16-bit PCM, mono)
    out.write("fmt ", 4);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = 1;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate = sample_rate * num_channels * bits_per_sample / 8;
    uint16_t block_align = num_channels * bits_per_sample / 8;

    out.write(reinterpret_cast<const char*>(&fmt_size), 4);
    out.write(reinterpret_cast<const char*>(&audio_format), 2);
    out.write(reinterpret_cast<const char*>(&num_channels), 2);
    out.write(reinterpret_cast<const char*>(&sample_rate), 4);
    out.write(reinterpret_cast<const char*>(&byte_rate), 4);
    out.write(reinterpret_cast<const char*>(&block_align), 2);
    out.write(reinterpret_cast<const char*>(&bits_per_sample), 2);

    // data chunk
    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), 4);
    out.write(reinterpret_cast<const char*>(pcm.data()), data_size);

    return true;
}
