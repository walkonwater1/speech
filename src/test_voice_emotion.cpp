/**
 * 语音情感分析独立测试
 */
#include "voice_emotion.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>

int main()
{
    // 1. 读取 WAV 文件
    const char* wav_path = "/tmp/test_stream.wav";
    std::ifstream file(wav_path, std::ios::binary);
    if (!file) {
        std::cerr << "无法打开: " << wav_path << std::endl;
        return 1;
    }

    file.seekg(44, std::ios::beg);

    std::vector<int16_t> raw;
    int16_t sample;
    while (file.read((char*)&sample, 2)) {
        raw.push_back(sample);
    }
    file.close();

    std::vector<float> samples(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        samples[i] = raw[i] / 32768.0f;
    }

    float duration = (float)samples.size() / 16000.0f;
    std::cout << "📁 测试文件: " << wav_path
              << " (" << duration << "秒)" << std::endl;

    // 2. 分析情感
    VoiceEmotionAnalyzer analyzer;
    auto result = analyzer.analyze(samples.data(), (int)samples.size());

    std::cout << "\n========== 声学情感分析结果 ==========" << std::endl;
    std::cout << "  情绪:     " << result.label << std::endl;
    std::cout << "  置信度:   " << (int)(result.confidence * 100) << "%" << std::endl;
    std::cout << "  详情:     " << result.detail << std::endl;
    std::cout << "  tone ID:  " << result.to_emotion_tone_id() << std::endl;

    // 3. 测试融合
    std::cout << "\n========== 情感融合测试 ==========" << std::endl;
    for (int text_tone = 0; text_tone < 5; ++text_tone) {
        const char* labels[] = {"NEUTRAL","HAPPY","SAD","EMPATHETIC","URGENT"};
        auto fusion = fuse_emotions(result, text_tone);
        std::cout << "  文本=" << labels[text_tone]
                  << " → " << fusion.diagnostic << std::endl;
    }

    return 0;
}
