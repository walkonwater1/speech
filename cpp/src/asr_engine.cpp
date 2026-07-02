/**
 * 语音识别引擎 — sherpa-onnx SenseVoice
 *
 * Python 对应: src/asr.py → ASREngine (funasr AutoModel)
 * C++ 实现:   sherpa-onnx C API
 *
 * 编译依赖: libsherpa-onnx.so + sherpa-onnx/c-api/c-api.h
 * 下载:     https://github.com/k2-fsa/sherpa-onnx/releases
 * 放到:     cpp/third_party/sherpa-onnx/
 *
 * sherpa-onnx 的模型文件（如 SenseVoice Small）需单独下载。
 */

#include "asr_engine.h"

// 当你下载了 sherpa-onnx 后，取消下面这行注释:
// #define SHERPA_ONNX_AVAILABLE

#ifdef SHERPA_ONNX_AVAILABLE
  #include "sherpa-onnx/c-api/c-api.h"
#endif

#include <iostream>
#include <chrono>
#include <fstream>
#include <cstring>
#include <cstdint>
#include <vector>

// ── WAV 读取工具 ─────────────────────────────────────

#ifdef SHERPA_ONNX_AVAILABLE

/// 从 WAV 文件读取 PCM 数据，返回采样率
static bool read_wav(const std::string& path,
                     std::vector<float>& samples,
                     int* sample_rate)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    char riff[5] = {};
    uint32_t file_size;
    file.read(riff, 4);
    file.read(reinterpret_cast<char*>(&file_size), 4);
    file.read(riff, 4);   // "WAVE"

    if (std::strncmp(riff, "RIFF", 4) != 0) return false;

    // 读 fmt chunk
    char chunk_id[5] = {};
    uint32_t chunk_size;
    file.read(chunk_id, 4);
    file.read(reinterpret_cast<char*>(&chunk_size), 4);

    uint16_t audio_format, channels;
    uint32_t sr, byte_rate;
    uint16_t block_align, bits_per_sample;

    file.read(reinterpret_cast<char*>(&audio_format), 2);
    file.read(reinterpret_cast<char*>(&channels), 2);
    file.read(reinterpret_cast<char*>(&sr), 4);
    file.read(reinterpret_cast<char*>(&byte_rate), 4);
    file.read(reinterpret_cast<char*>(&block_align), 2);
    file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

    *sample_rate = sr;

    // 跳转到 data chunk
    while (file.read(chunk_id, 4)) {
        file.read(reinterpret_cast<char*>(&chunk_size), 4);
        if (std::strncmp(chunk_id, "data", 4) == 0) break;
        file.seekg(chunk_size, std::ios::cur);
    }

    // 读 PCM 数据
    int num_samples = chunk_size / (bits_per_sample / 8);
    samples.resize(num_samples);
    std::vector<int16_t> raw(num_samples);
    file.read(reinterpret_cast<char*>(raw.data()), chunk_size);

    // int16 → float32 [-1, 1]
    for (int i = 0; i < num_samples; ++i) {
        samples[i] = raw[i] / 32768.0f;
    }

    return true;
}

#endif  // SHERPA_ONNX_AVAILABLE

// ── ASREngine ────────────────────────────────────────

ASREngine::ASREngine(const std::string& model_path)
    : model_path_(model_path)
{}

ASREngine::~ASREngine()
{
#ifdef SHERPA_ONNX_AVAILABLE
    if (recognizer_) {
        SherpaOnnxOfflineRecognizerDestroy(recognizer_);
    }
#endif
}

bool ASREngine::initialize()
{
    std::cout << "[ASR] 加载 SenseVoice ... " << std::flush;

#ifdef SHERPA_ONNX_AVAILABLE
    SherpaOnnxOfflineRecognizerConfig* config =
        SherpaOnnxOfflineRecognizerConfigCreate();

    // 设置模型路径（SenseVoice / Zipformer 等）
    SherpaOnnxOfflineRecognizerConfigSetModelDir(config, model_path_.c_str());
    SherpaOnnxOfflineRecognizerConfigSetDecodingMethod(config, "greedy_search");

    recognizer_ = SherpaOnnxCreateOfflineRecognizer(config);
    SherpaOnnxOfflineRecognizerConfigDelete(config);

    if (!recognizer_) {
        std::cerr << "❌ 创建 recognizer 失败" << std::endl;
        return false;
    }
#else
    std::cerr << "⚠️  sherpa-onnx 未安装（跳过）" << std::endl;
    std::cerr << "   下载: https://github.com/k2-fsa/sherpa-onnx/releases" << std::endl;
    std::cerr << "   放到: cpp/third_party/sherpa-onnx/" << std::endl;
    std::cerr << "   然后取消 asr_engine.cpp 中 SHERPA_ONNX_AVAILABLE 的注释" << std::endl;
    initialized_ = true;  // 允许降级运行
    return true;
#endif

    initialized_ = true;
    std::cout << "✅" << std::endl;
    return true;
}

std::string ASREngine::transcribe(const std::string& wav_path)
{
    if (!initialized_) return "";

    auto t0 = std::chrono::steady_clock::now();

    std::string text;

#ifdef SHERPA_ONNX_AVAILABLE
    // 从 WAV 文件读取音频
    std::vector<float> samples;
    int sample_rate = 16000;
    if (!read_wav(wav_path, samples, &sample_rate)) {
        std::cerr << "   [ASR] 无法读取音频文件: " << wav_path << std::endl;
        return "";
    }

    // 创建 stream
    SherpaOnnxOfflineStream* stream = SherpaOnnxOfflineRecognizerCreateStream(
        recognizer_, wav_path.c_str());
    if (!stream) {
        std::cerr << "   [ASR] 创建 stream 失败" << std::endl;
        return "";
    }

    // 解码
    SherpaOnnxOfflineRecognizerDecode(recognizer_, stream);

    // 获取结果
    const SherpaOnnxOfflineRecognizerResult* result =
        SherpaOnnxOfflineRecognizerGetResult(recognizer_, stream);

    if (result) {
        text = SherpaOnnxOfflineRecognizerResultGetText(result);
        // SenseVoice 输出格式: "<|zh|><|NEUTRAL|>文本"
        // 去掉前面的标签
        auto pos = text.rfind('>');
        if (pos != std::string::npos) {
            text = text.substr(pos + 1);
        }
    }

    SherpaOnnxOfflineRecognizerDestroyStream(recognizer_, stream);
#else
    std::cerr << "   [ASR] ⚠️ sherpa-onnx 不可用，返回假结果" << std::endl;
    text = "[ASR 未就绪] " + wav_path;
#endif

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::cout << "   [ASR] \"" << text << "\"  (" << elapsed / 1000.0 << "s)" << std::endl;

    return text;
}
