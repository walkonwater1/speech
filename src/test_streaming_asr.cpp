/**
 * 流式 ASR 独立测试 — 验证 chunked 后端
 * 编译: cd src/build && cmake .. && make test_streaming
 */
#include "streaming_asr.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>

int main()
{
    // 1. 读取 WAV 文件
    const char* wav_path = "/tmp/test_stream.wav";
    std::ifstream file(wav_path, std::ios::binary);
    if (!file) {
        std::cerr << "无法打开: " << wav_path << std::endl;
        return 1;
    }

    // 跳过 WAV 头 (44 bytes)
    file.seekg(44, std::ios::beg);

    // 读 int16 样本
    std::vector<int16_t> raw;
    int16_t sample;
    while (file.read((char*)&sample, 2)) {
        raw.push_back(sample);
    }
    file.close();

    // 转 float [-1, 1]
    std::vector<float> samples(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        samples[i] = raw[i] / 32768.0f;
    }

    float duration = (float)samples.size() / 16000.0f;
    std::cout << "📁 测试文件: " << wav_path
              << " (" << samples.size() << " 样本, "
              << duration << "秒)" << std::endl;

    // 2. 初始化流式 ASR (chunked 后端)
    StreamingASRConfig cfg;
    cfg.backend              = "chunked";
    cfg.model_path           = "src/third_party/sherpa-onnx/sense-voice-model";
    cfg.min_chunk_seconds    = 0.8f;
    cfg.chunk_interval       = 0.3f;  // 更频繁触发以观察部分结果

    StreamingASR asr;
    if (!asr.initialize(cfg)) {
        std::cerr << "❌ 流式 ASR 初始化失败" << std::endl;
        return 1;
    }

    // 3. 模拟流式喂入（每 20ms 一帧 = 320 样本 @16kHz）
    asr.start_utterance();

    const int frame_size = 320;  // 20ms @ 16kHz
    int total_frames = (int)samples.size() / frame_size;

    std::cout << "\n🎤 开始流式识别 (" << total_frames << " 帧, "
              << frame_size << " 样本/帧):" << std::endl;
    std::cout << "--------------------------------------------------" << std::endl;

    for (int i = 0; i < total_frames; ++i) {
        asr.feed(&samples[i * frame_size], frame_size);

        // 每 10 帧检查一次部分结果
        if ((i + 1) % 10 == 0) {
            const char* p = asr.partial();
            float dur = asr.audio_duration();
            std::cout << "   [" << dur << "s] 部分: "
                      << (p ? p : "(无)") << std::endl;
        }
    }

    std::cout << "--------------------------------------------------" << std::endl;

    // 4. 最终结果
    std::string final_text = asr.finalize();
    std::cout << "\n✅ 最终识别: \"" << final_text << "\"" << std::endl;

    return 0;
}
