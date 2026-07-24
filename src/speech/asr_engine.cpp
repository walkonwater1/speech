/**
 * 语音识别引擎 — sherpa-onnx SenseVoice
 *
 * Python 对应: src/asr.py → ASREngine (funasr AutoModel)
 * C++ 实现:   sherpa-onnx C API v1.13+
 *
 * 编译依赖: libsherpa-onnx-c-api.so + sherpa-onnx/c-api/c-api.h
 * 下载:     https://github.com/k2-fsa/sherpa-onnx/releases
 * 放到:     cpp/third_party/sherpa-onnx/
 *
 * sherpa-onnx 的模型文件（如 SenseVoice Small）需单独下载。
 */

#include "asr_engine.h"

// SHERPA_ONNX_AVAILABLE 由 CMakeLists.txt 通过 add_compile_definitions 定义
#ifdef SHERPA_ONNX_AVAILABLE
  #include "sherpa-onnx/c-api/c-api.h"
#endif

#include <iostream>
#include <chrono>
#include <cstring>
#include "logger.h"

// ── ASREngine ────────────────────────────────────────

ASREngine::ASREngine(const std::string& model_path, AsrModelType model_type)
    : model_path_(model_path), model_type_(model_type)
{}

ASREngine::~ASREngine()
{
#ifdef SHERPA_ONNX_AVAILABLE
    if (recognizer_) {
        SherpaOnnxDestroyOfflineRecognizer(recognizer_);
    }
#endif
}

bool ASREngine::initialize()
{
    std::cout << "[ASR] 加载 "
              << (model_type_ == AsrModelType::ZIPFORMER_CTC ? "Zipformer CTC" : "SenseVoice")
              << " ... " << std::flush;

#ifdef SHERPA_ONNX_AVAILABLE
    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    // 特征配置
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;

    // 模型配置 — 按类型切换
    std::string model_file = model_path_ + "/model.int8.onnx";
    std::string tokens_file = model_path_ + "/tokens.txt";

    if (model_type_ == AsrModelType::ZIPFORMER_CTC) {
        // Zipformer CTC: 中文专用 CTC，单模型文件，无需 language/use_itn
        config.model_config.zipformer_ctc.model = model_file.c_str();
    } else {
        // SenseVoice: 多语言模型，支持 auto language + ITN
        config.model_config.sense_voice.model = model_file.c_str();
        config.model_config.sense_voice.language = "auto";
        config.model_config.sense_voice.use_itn = 1;
    }

    config.model_config.tokens = tokens_file.c_str();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 4;
    config.decoding_method = "greedy_search";

    recognizer_ = SherpaOnnxCreateOfflineRecognizer(&config);

    if (!recognizer_) {
        LOG_ERROR("❌ 创建 recognizer 失败");
        std::cerr << "   请确保模型文件存在: " << model_path_ << std::endl;
        LOG_ERROR("   下载: https://github.com/k2-fsa/sherpa-onnx/releases");
        return false;
    }
#else
    LOG_WARN("⚠️  sherpa-onnx 未安装（跳过）");
    initialized_ = true;
    return true;
#endif

    initialized_ = true;
    LOG_INFO("✅");
    return true;
}

std::string ASREngine::transcribe(const std::string& wav_path)
{
    if (!initialized_) return "";

    auto t0 = std::chrono::steady_clock::now();

    std::string text;

#ifdef SHERPA_ONNX_AVAILABLE
    // 使用 sherpa-onnx 内置的 WAV 读取
    const SherpaOnnxWave* wave = SherpaOnnxReadWave(wav_path.c_str());
    if (!wave) {
        std::cerr << "   [ASR] 无法读取音频文件: " << wav_path << std::endl;
        return "";
    }

    // 创建离线 stream
    const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(recognizer_);
    if (!stream) {
        LOG_ERROR("   [ASR] 创建 stream 失败");
        SherpaOnnxFreeWave(wave);
        return "";
    }

    // 喂入音频数据
    SherpaOnnxAcceptWaveformOffline(stream, wave->sample_rate, wave->samples, wave->num_samples);

    // 解码
    SherpaOnnxDecodeOfflineStream(recognizer_, stream);

    // 获取结果
    const SherpaOnnxOfflineRecognizerResult* result =
        SherpaOnnxGetOfflineStreamResult(stream);

    if (result && result->text) {
        text = result->text;
        // SenseVoice 输出格式: "<|zh|><|NEUTRAL|>文本" → 去掉前面的标签
        // Zipformer CTC 输出纯中文文本，无需处理
        if (model_type_ == AsrModelType::SENSE_VOICE) {
            auto pos = text.rfind('>');
            if (pos != std::string::npos) {
                text = text.substr(pos + 1);
            }
        }

        SherpaOnnxDestroyOfflineRecognizerResult(result);
    }

    SherpaOnnxDestroyOfflineStream(stream);
    SherpaOnnxFreeWave(wave);
#else
    LOG_WARN("   [ASR] ⚠️ sherpa-onnx 不可用，返回假结果");
    text = "[ASR 未就绪] " + wav_path;
#endif

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (!quiet_) {
        std::cout << "   [ASR] \"" << text << "\"  (" << elapsed / 1000.0 << "s)" << std::endl;
    }

    return text;
}
