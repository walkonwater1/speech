/**
 * 语音合成引擎 — espeak-ng
 *
 * Python 对应: src/tts.py → TTSEngine (pyttsx3)
 * C++ 实现:   libespeak-ng (系统库)
 *
 * espeak 输出 16-bit PCM → 手动写入 WAV (44 字节头 + PCM data)
 */

#include "tts_engine.h"

#include "espeak_min.h"   // libespeak-ng.so 已安装，不需要 -dev 包
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <iostream>

// ── 静态回调（espeak 在 synthesize 时通过此回调给音频数据）──
static std::vector<int16_t> g_tts_audio;

static int audio_callback(short* wav, int numsamples, espeak_EVENT* /*events*/)
{
    if (wav && numsamples > 0) {
        g_tts_audio.insert(g_tts_audio.end(), wav, wav + numsamples);
    }
    return 0;   // 继续
}

// ── WAV 写入工具 ─────────────────────────────────────

static bool write_wav(const std::string& path,
                      const std::vector<int16_t>& samples,
                      int sample_rate = 22050)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    uint32_t data_size = samples.size() * sizeof(int16_t);
    uint32_t chunk_size = 36 + data_size;

    // RIFF header
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&chunk_size), 4);
    out.write("WAVE", 4);

    // fmt  chunk
    out.write("fmt ", 4);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;    // PCM
    uint16_t num_channels = 1;    // mono
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
    out.write(reinterpret_cast<const char*>(samples.data()), data_size);

    return true;
}

// ── TTSEngine ────────────────────────────────────────

TTSEngine::TTSEngine(int rate)
    : rate_(rate)
{}

TTSEngine::~TTSEngine()
{
    if (initialized_) {
        espeak_Terminate();
    }
}

bool TTSEngine::initialize()
{
    std::cout << "[TTS] 初始化 espeak-ng ... " << std::flush;

    // espeak_Initialize 返回采样率 (如 22050) 表示成功，返回 -1 表示失败
    int sample_rate = espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 0, nullptr, 0);
    if (sample_rate <= 0) {
        std::cerr << "❌ espeak_Initialize 失败 (返回: " << sample_rate << ")" << std::endl;
        return false;
    }
    std::cout << "(" << sample_rate << "Hz) " << std::flush;

    // 设置中文语音
    espeak_SetVoiceByName("cmn");           // Mandarin Chinese
    espeak_SetParameter(espeakRATE, rate_, 0);

    initialized_ = true;
    std::cout << "✅" << std::endl;
    return true;
}

bool TTSEngine::synthesize(const std::string& text, const std::string& output_path)
{
    if (!initialized_) return false;

    g_tts_audio.clear();
    espeak_SetSynthCallback(audio_callback);

    espeak_ERROR err = espeak_Synth(text.c_str(), text.size() + 1,
                                    0, POS_CHARACTER, 0,
                                    espeakCHARS_UTF8, nullptr, nullptr);
    if (err != EE_OK) {
        std::cerr << "[TTS] espeak_Synth 失败" << std::endl;
        return false;
    }

    // 同步模式：等待合成完毕
    espeak_Synchronize();

    // 写入 WAV
    if (g_tts_audio.empty()) {
        std::cerr << "[TTS] 合成结果为空" << std::endl;
        return false;
    }

    if (!write_wav(output_path, g_tts_audio)) {
        std::cerr << "[TTS] 写 WAV 文件失败: " << output_path << std::endl;
        return false;
    }

    return true;
}
