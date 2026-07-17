/**
 * 语音合成引擎 — 双后端（espeak-ng / Piper neural TTS）
 *
 * espeak: libespeak-ng 直接调用（快速但电音）
 * Piper:  常驻 Python 进程（模型只加载一次，后续调用低延迟）
 */

#include "tts_engine.h"

#include "espeak_min.h"
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

// ── espeak 回调 ─────────────────────────────────────

static std::vector<int16_t> g_tts_audio;

static int audio_callback(short* wav, int numsamples, espeak_EVENT* /*events*/)
{
    if (wav && numsamples > 0) {
        g_tts_audio.insert(g_tts_audio.end(), wav, wav + numsamples);
    }
    return 0;
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

    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&chunk_size), 4);
    out.write("WAVE", 4);

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

    out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&data_size), 4);
    out.write(reinterpret_cast<const char*>(samples.data()), data_size);

    return true;
}

// ── 路径工具 ─────────────────────────────────────────

static std::string expand_tilde(const std::string& path)
{
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    if (path.size() == 1) return home;
    return std::string(home) + path.substr(1);
}

static bool file_exists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static std::string find_script(const std::string& name)
{
    std::string src_prefix   = "src/" + name;
    std::string scripts_path = "src/scripts/" + name;
    const char* all[] = { name.c_str(), src_prefix.c_str(), scripts_path.c_str(), nullptr };

    for (int i = 0; all[i]; ++i) {
        if (file_exists(all[i])) return all[i];
    }
    return name;  // fallback
}

// ── TTSEngine ────────────────────────────────────────

TTSEngine::TTSEngine(int rate, const std::string& voice,
                     const std::string& backend, const std::string& piper_model)
    : rate_(rate)
    , voice_(voice)
    , backend_(backend)
    , piper_model_(piper_model)
{
    if (!piper_model_.empty()) {
        piper_model_ = expand_tilde(piper_model_);
    }
}

TTSEngine::~TTSEngine()
{
    if (backend_ == "piper") {
        shutdown_piper();
    }
    if (initialized_ && backend_ == "espeak") {
        espeak_Terminate();
    }
}

bool TTSEngine::initialize()
{
    if (backend_ == "piper") {
        return init_piper();
    } else {
        return init_espeak();
    }
}

bool TTSEngine::synthesize(const std::string& text, const std::string& output_path)
{
    if (!initialized_) return false;

    if (backend_ == "piper") {
        return synthesize_piper(text, output_path);
    } else {
        return synthesize_espeak(text, output_path);
    }
}

// ── espeak 后端 ─────────────────────────────────────

bool TTSEngine::init_espeak()
{
    std::cout << "[TTS] 初始化 espeak-ng ... " << std::flush;

    int sample_rate = espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 0, nullptr, 0);
    if (sample_rate <= 0) {
        std::cerr << "❌ espeak_Initialize 失败 (返回: " << sample_rate << ")" << std::endl;
        return false;
    }
    std::cout << "(" << sample_rate << "Hz) " << std::flush;

    espeak_SetVoiceByName(voice_.c_str());
    espeak_SetParameter(espeakRATE, rate_, 0);

    initialized_ = true;
    std::cout << "✅" << std::endl;
    return true;
}

bool TTSEngine::synthesize_espeak(const std::string& text, const std::string& output_path)
{
    g_tts_audio.clear();
    espeak_SetSynthCallback(audio_callback);

    espeak_ERROR err = espeak_Synth(text.c_str(), text.size() + 1,
                                    0, POS_CHARACTER, 0,
                                    espeakCHARS_UTF8, nullptr, nullptr);
    if (err != EE_OK) {
        std::cerr << "[TTS] espeak_Synth 失败" << std::endl;
        return false;
    }

    espeak_Synchronize();

    if (g_tts_audio.empty()) {
        std::cerr << "[TTS] 合成结果为空" << std::endl;
        return false;
    }

    return write_wav(output_path, g_tts_audio);
}

// ── Piper 后端（常驻进程）──────────────��─────────────

bool TTSEngine::init_piper()
{
    std::cout << "[TTS] Piper 后端 (xiaoya - 小雅) ... " << std::flush;

    // 找 conda Python
    std::string python = "python3";
    const char* home = std::getenv("HOME");
    const char* conda_prefix = std::getenv("CONDA_PREFIX");

    std::vector<std::string> py_candidates;
    if (conda_prefix) py_candidates.push_back(std::string(conda_prefix) + "/bin/python3");
    if (home) {
        py_candidates.push_back(std::string(home) + "/miniconda3/envs/chatAudio/bin/python3");
        py_candidates.push_back(std::string(home) + "/miniconda3/bin/python3");
    }
    py_candidates.push_back("/usr/bin/python3");

    for (const auto& p : py_candidates) {
        if (file_exists(p)) { python = p; break; }
    }

    // 找 piper_server.py
    piper_script_ = find_script("piper_server.py");
    if (!file_exists(piper_script_)) {
        std::cerr << "❌ 脚本未找到: " << piper_script_ << std::endl;
        return false;
    }

    if (!file_exists(piper_model_)) {
        std::cerr << "❌ 模型未找到: " << piper_model_ << std::endl;
        return false;
    }

    // 启动常驻 Python 进程（双向管道）
    std::string cmd = "HF_ENDPOINT=https://hf-mirror.com "
                    + python + " -u " + piper_script_
                    + " \"" + piper_model_ + "\"";

    piper_in_ = popen(cmd.c_str(), "w");   // 我们往 Python 写文本
    if (!piper_in_) {
         // popen "w" 只能写，不能读。需要双向管道用 pipe+fdopen。
         // 改用 fork+pipe 方案。
    }

    // 不能用 popen 做双向！需要用 pipe + fork
    // 重新实现...
    std::cerr << "⚠️  需要双向管道，改用 fork/pipe 方案" << std::endl;

    // 先清理
    if (piper_in_) { pclose(piper_in_); piper_in_ = nullptr; }

    int to_child[2];   // 父写 → 子读 (stdin)
    int from_child[2]; // 子写 → 父读 (stdout)

    if (pipe(to_child) < 0 || pipe(from_child) < 0) {
        std::cerr << "❌ pipe 创建失败" << std::endl;
        return false;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // ── 子进程 ──
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);

        // 清除 LD_LIBRARY_PATH（避免 conda ALSA 冲突）
        unsetenv("LD_LIBRARY_PATH");
        setenv("HF_ENDPOINT", "https://hf-mirror.com", 1);

        execlp(python.c_str(), python.c_str(), "-u",
               piper_script_.c_str(), piper_model_.c_str(), nullptr);
        _exit(1);
    }

    // ── 父进程 ──
    close(to_child[0]);
    close(from_child[1]);

    piper_in_  = fdopen(to_child[1], "w");
    piper_out_ = fdopen(from_child[0], "r");

    if (!piper_in_ || !piper_out_) {
        std::cerr << "❌ fdopen 失败" << std::endl;
        return false;
    }

    // 读第一行 stderr 输出的 JSON 元数据（通过 pipe 劫持？不，stderr 不走 pipe）
    // 直接硬编码已知参数
    piper_sample_rate_ = 22050;

    initialized_ = true;
    std::cout << "✅ (模型已预加载)" << std::endl;
    return true;
}

void TTSEngine::shutdown_piper()
{
    if (piper_in_) {
        fclose(piper_in_);   // 关闭 stdin → Python 收到 EOF → 退出
        piper_in_ = nullptr;
    }
    if (piper_out_) {
        fclose(piper_out_);
        piper_out_ = nullptr;
    }
}

bool TTSEngine::read_exact(void* buf, size_t len)
{
    size_t total = 0;
    auto* p = static_cast<char*>(buf);
    while (total < len) {
        size_t n = fread(p + total, 1, len - total, piper_out_);
        if (n == 0) {
            if (ferror(piper_out_)) {
                std::cerr << "[TTS] 读取 Piper 输出失败" << std::endl;
            }
            return false;
        }
        total += n;
    }
    return true;
}

bool TTSEngine::synthesize_piper(const std::string& text, const std::string& /*output_path*/)
{
    if (!piper_in_ || !piper_out_) return false;

    // 1. 发文本给 Python 进程
    fputs(text.c_str(), piper_in_);
    fputc('\n', piper_in_);
    fflush(piper_in_);

    // 2. 读 4 字节长度头（大端 uint32）
    uint32_t pcm_len_be = 0;
    if (!read_exact(&pcm_len_be, 4)) return false;
    uint32_t pcm_len = (pcm_len_be >> 24) | ((pcm_len_be >> 8) & 0xFF00)
                     | ((pcm_len_be << 8) & 0xFF0000) | (pcm_len_be << 24);

    if (pcm_len == 0) return false;

    // 3. 读 PCM 数据
    std::vector<char> pcm(pcm_len);
    if (!read_exact(pcm.data(), pcm_len)) return false;

    // 4. 通过管道喂给 aplay，清除 conda LD_LIBRARY_PATH
    std::string aplay_cmd = "env -u LD_LIBRARY_PATH aplay -q -f S16_LE -r "
                          + std::to_string(piper_sample_rate_) + " -c 1";

    FILE* aplay = popen(aplay_cmd.c_str(), "w");
    if (!aplay) {
        std::cerr << "[TTS] 启动 aplay 失败" << std::endl;
        return false;
    }

    fwrite(pcm.data(), 1, pcm_len, aplay);
    fflush(aplay);

    int status = pclose(aplay);
    return status == 0;
}
