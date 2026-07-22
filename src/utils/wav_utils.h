/**
 * WAV 文件读写工具 — 统一 WAV 头解析和写入
 *
 * 用途：消除 voice_pipeline.cpp / tts_engine.cpp / audio_io.cpp 中
 *       重复的 WAV 读写代码。
 *
 * 用法：
 *   #include "wav_utils.h"
 *   wav_utils::write_wav(path, int16_samples, 22050);
 *   wav_utils::write_wav_float(path, float_samples, 16000);
 *   float dur = wav_utils::read_duration(path);
 */

#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <cstring>

namespace wav_utils {

/// 写入 int16 PCM 数据为 WAV 文件
inline bool write_wav(const std::string& path,
                      const std::vector<int16_t>& samples,
                      int sample_rate = 22050,
                      int num_channels = 1)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    uint32_t data_size   = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    uint32_t chunk_size  = 36 + data_size;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate   = static_cast<uint32_t>(sample_rate) * num_channels * bits_per_sample / 8;
    uint16_t block_align = static_cast<uint16_t>(num_channels * bits_per_sample / 8);

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&chunk_size), 4);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    out.write(reinterpret_cast<const char*>(&fmt_size), 4);
    out.write(reinterpret_cast<const char*>(&audio_format), 2);
    out.write(reinterpret_cast<const char*>(&num_channels), 2);
    out.write(reinterpret_cast<const char*>(&sample_rate), 4);
    out.write(reinterpret_cast<const char*>(&byte_rate), 4);
    out.write(reinterpret_cast<const char*>(&block_align), 2);
    out.write(reinterpret_cast<const char*>(&bits_per_sample), 2);

    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), 4);
    out.write(reinterpret_cast<const char*>(samples.data()), data_size);

    return true;
}

/// float [-1,1] 样本 → clip → int16 → WAV 文件
inline bool write_wav_float(const std::string& path,
                            const std::vector<float>& samples,
                            int sample_rate = 16000)
{
    std::vector<int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        float s = samples[i];
        if (s > 1.0f)  s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        pcm[i] = static_cast<int16_t>(s * 32767.0f);
    }
    return write_wav(path, pcm, sample_rate);
}

/// 从 WAV 文件头读取时长（秒），失败返回 0
inline float read_duration(const std::string& wav_path)
{
    std::ifstream file(wav_path, std::ios::binary);
    if (!file) return 0.0f;

    // 跳过 RIFF header
    file.seekg(12);
    // 查找 fmt chunk
    char chunk_id[5] = {};
    uint32_t chunk_size = 0;
    while (file.read(chunk_id, 4)) {
        file.read(reinterpret_cast<char*>(&chunk_size), 4);
        if (std::strncmp(chunk_id, "fmt ", 4) == 0) break;
        file.seekg(chunk_size, std::ios::cur);
    }

    uint16_t audio_format, num_channels, bits_per_sample;
    uint32_t sample_rate;
    file.read(reinterpret_cast<char*>(&audio_format), 2);
    file.read(reinterpret_cast<char*>(&num_channels), 2);
    file.read(reinterpret_cast<char*>(&sample_rate), 4);
    file.seekg(6, std::ios::cur);  // byte_rate + block_align
    file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

    // 查找 data chunk
    file.seekg(chunk_size - 16, std::ios::cur);
    uint32_t data_size = 0;
    while (file.read(chunk_id, 4)) {
        file.read(reinterpret_cast<char*>(&data_size), 4);
        if (std::strncmp(chunk_id, "data", 4) == 0) break;
        file.seekg(data_size, std::ios::cur);
    }

    if (sample_rate == 0 || num_channels == 0 || bits_per_sample == 0) return 0.0f;
    uint32_t total_samples = data_size / (num_channels * bits_per_sample / 8);
    return static_cast<float>(total_samples) / sample_rate;
}

} // namespace wav_utils
