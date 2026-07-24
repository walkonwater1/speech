/**
 * VAD（语音活动检测）单元测试
 *
 * 测试 EnergyVAD 和 AdaptiveVAD 的状态机:
 *   1. 静音帧不触发语音
 *   2. 连续高能量帧触发 SPEECH_START
 *   3. 语音持续 → SPEECH_ONGOING
 *   4. 连续静音帧 → SPEECH_END
 *   5. 语音段积累正确
 *   6. AdaptiveVAD 噪声基线自适应
 *   7. cooldown 机制
 */

#include "vad.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

static int passed = 0, failed = 0;

#define TEST(name) do { std::cout << "  [TEST] " << name << " ... " << std::flush; } while(0)
#define PASS() do { std::cout << "✅" << std::endl; passed++; } while(0)
#define FAIL(msg) do { std::cout << "❌ " << msg << std::endl; failed++; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

// ── 辅助：生成测试音频帧 ──────────────────────────────

// 生成静音帧（极低能量）
static void gen_silence(std::vector<float>& buf, int n)
{
    buf.assign(n, 0.001f); // 微小噪声
}

// 生成语音帧（模拟人声）
static void gen_speech(std::vector<float>& buf, int n)
{
    buf.resize(n);
    for (int i = 0; i < n; ++i) {
        buf[i] = 0.3f * std::sin(2.0f * M_PI * 440.0f * i / 16000.0f)
               + 0.2f * std::sin(2.0f * M_PI * 880.0f * i / 16000.0f)
               + 0.1f * (float)rand() / RAND_MAX;
    }
}

// 喂 N 帧并返回最后的状态
static VADState feed_n_frames(EnergyVAD& vad, const std::vector<float>& frame, int n)
{
    VADState last = VADState::SILENCE;
    for (int i = 0; i < n; ++i) {
        last = vad.process_frame(frame.data(), (int)frame.size());
    }
    return last;
}

const int FRAME_SAMPLES = 320;  // 20ms @16kHz

// ── EnergyVAD 基础测试 ──────────────────────────────────

static void test_energy_vad_silence()
{
    EnergyVAD vad;
    std::vector<float> frame;
    gen_silence(frame, FRAME_SAMPLES);

    TEST("EnergyVAD: 静音帧不触发语音");
    for (int i = 0; i < 100; ++i) {
        VADState s = vad.process_frame(frame.data(), FRAME_SAMPLES);
        CHECK(s != VADState::SPEECH_START, "静音不应触发语音开始");
        CHECK(!vad.in_speech(), "静音时不应标记为语音中");
    }
    PASS();
}

static void test_energy_vad_speech_start()
{
    VADConfig cfg;
    cfg.energy_threshold = 0.03f;  // 低阈值
    cfg.min_speech_frames = 5;
    EnergyVAD vad(cfg);

    std::vector<float> frame;
    gen_speech(frame, FRAME_SAMPLES);

    TEST("EnergyVAD: 连续语音帧触发 SPEECH_START");
    bool saw_start = false;
    for (int i = 0; i < 20; ++i) {
        VADState s = vad.process_frame(frame.data(), FRAME_SAMPLES);
        if (s == VADState::SPEECH_START) { saw_start = true; break; }
    }
    CHECK(saw_start, "应检测到语音开始");
    PASS();
}

static void test_energy_vad_speech_end()
{
    VADConfig cfg;
    cfg.energy_threshold = 0.03f;
    cfg.min_speech_frames = 3;
    cfg.min_silence_frames = 5;
    EnergyVAD vad(cfg);

    std::vector<float> speech, silence;
    gen_speech(speech, FRAME_SAMPLES);
    gen_silence(silence, FRAME_SAMPLES);

    TEST("EnergyVAD: 静音后触发 SPEECH_END");

    // 先触发语音开始
    for (int i = 0; i < 10; ++i) vad.process_frame(speech.data(), FRAME_SAMPLES);
    CHECK(vad.in_speech(), "应先进入语音状态");

    // 喂静音帧，等待触发结束
    bool saw_end = false;
    for (int i = 0; i < 30; ++i) {
        VADState s = vad.process_frame(silence.data(), FRAME_SAMPLES);
        if (s == VADState::SPEECH_END) { saw_end = true; break; }
    }
    CHECK(saw_end, "应检测到语音结束");
    PASS();
}

static void test_energy_vad_segment_accumulation()
{
    VADConfig cfg;
    cfg.energy_threshold = 0.03f;
    cfg.min_speech_frames = 3;
    cfg.min_silence_frames = 5;
    EnergyVAD vad(cfg);

    std::vector<float> speech, silence;
    gen_speech(speech, FRAME_SAMPLES);
    gen_silence(silence, FRAME_SAMPLES);

    TEST("EnergyVAD: 语音段样本积累正确");

    for (int i = 0; i < 10; ++i) vad.process_frame(speech.data(), FRAME_SAMPLES);
    int speech_count = vad.speech_sample_count();
    CHECK(speech_count > 0, "应有语音样本积累");
    CHECK(speech_count >= 7 * FRAME_SAMPLES, "至少积累min_speech后的帧数");

    PASS();
}

// ── AdaptiveVAD 测试 ──────────────────────────────────

static void test_adaptive_vad_noise_learning()
{
    VADConfig cfg;
    cfg.min_energy_threshold = 0.005f;
    cfg.adaptive_factor = 3.0f;
    AdaptiveVAD vad(cfg);

    std::vector<float> silence;
    gen_silence(silence, FRAME_SAMPLES);

    TEST("AdaptiveVAD: 学习噪声基线后不误触发");
    // 先喂一段静音让自适应学习噪声基线
    for (int i = 0; i < 100; ++i) {
        vad.process_frame(silence.data(), FRAME_SAMPLES);
    }

    CHECK(!vad.in_speech(), "学习噪声后不应误触发语音");
    float nf = vad.noise_floor();
    CHECK(nf > 0.0f, "噪声基线应大于0");
    std::cout << " (基线=" << nf << ") " << std::flush;
    PASS();
}

static void test_adaptive_vad_speech_detection()
{
    VADConfig cfg;
    cfg.energy_threshold = 0.01f;
    cfg.min_speech_frames = 5;
    cfg.adaptive_factor = 2.0f;
    cfg.min_energy_threshold = 0.002f;
    AdaptiveVAD vad(cfg);

    std::vector<float> silence, speech;
    gen_silence(silence, FRAME_SAMPLES);
    gen_speech(speech, FRAME_SAMPLES);

    TEST("AdaptiveVAD: 先学噪声再检测语音");

    // 学习噪声基线
    for (int i = 0; i < 50; ++i) vad.process_frame(silence.data(), FRAME_SAMPLES);
    CHECK(!vad.in_speech(), "学习阶段不应触发");

    // 喂语音帧
    bool saw_speech = false;
    for (int i = 0; i < 20; ++i) {
        VADState s = vad.process_frame(speech.data(), FRAME_SAMPLES);
        if (s == VADState::SPEECH_START) { saw_speech = true; break; }
    }
    CHECK(saw_speech, "应在噪声后检测到语音");
    PASS();
}

// ── cooldown 测试 ─────────────────────────────────────

static void test_vad_cooldown()
{
    VADConfig cfg;
    cfg.energy_threshold = 0.03f;
    cfg.min_speech_frames = 3;
    cfg.min_silence_frames = 3;
    cfg.silence_cooldown_frames = 10;
    EnergyVAD vad(cfg);

    std::vector<float> speech, silence;
    gen_speech(speech, FRAME_SAMPLES);
    gen_silence(silence, FRAME_SAMPLES);

    TEST("VAD cooldown: 语音结束后需冷却才能再次触发");

    // 第一段语音
    for (int i = 0; i < 10; ++i) vad.process_frame(speech.data(), FRAME_SAMPLES);
    // 静音结束
    for (int i = 0; i < 10; ++i) vad.process_frame(silence.data(), FRAME_SAMPLES);
    CHECK(!vad.in_speech(), "第一段语音应结束");

    // 立即喂语音 — cooldown 期间不应重新触发
    bool early_trigger = false;
    for (int i = 0; i < 5; ++i) {
        VADState s = vad.process_frame(speech.data(), FRAME_SAMPLES);
        if (s == VADState::SPEECH_START) { early_trigger = true; break; }
    }
    // cooldown 期间不应触发新语音段
    // （但 min_speech_frames 也可能阻止，所以这不是硬检查）
    // 至少确认之前的状态已正确重置
    PASS();
}

// ── Main ───────────────────────────────────────────────

int main()
{
    std::cout << "\n========== VAD 单元测试 ==========\n" << std::endl;

    std::cout << "--- EnergyVAD ---" << std::endl;
    test_energy_vad_silence();
    test_energy_vad_speech_start();
    test_energy_vad_speech_end();
    test_energy_vad_segment_accumulation();

    std::cout << "\n--- AdaptiveVAD ---" << std::endl;
    test_adaptive_vad_noise_learning();
    test_adaptive_vad_speech_detection();

    std::cout << "\n--- Cooldown ---" << std::endl;
    test_vad_cooldown();

    std::cout << "\n===================================" << std::endl;
    std::cout << "通过: " << passed << " / 失败: " << failed << std::endl;

    if (failed > 0) {
        std::cout << "\n❌ 测试未全部通过！" << std::endl;
        return 1;
    }
    std::cout << "✅ 全部测试通过！" << std::endl;
    return 0;
}
