/**
 * 声纹库 (Voiceprint Library) — 实现
 *
 * Layer 3.4: 语音交互深化 — 多用户声纹管理
 */

#include "voiceprint_library.h"

#ifdef SHERPA_ONNX_AVAILABLE
  #include "sherpa-onnx/c-api/c-api.h"
#endif

#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctime>
#include "logger.h"

using json = nlohmann::json;

// ── 辅助：递归创建目录 ────────────────────────────────

static bool mkdir_p(const std::string& path) {
    if (path.empty()) return false;
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        mkdir(sub.c_str(), 0755);
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

// ── VoiceprintLibrary ──────────────────────────────────

VoiceprintLibrary::VoiceprintLibrary() = default;

VoiceprintLibrary::~VoiceprintLibrary()
{
#ifdef SHERPA_ONNX_AVAILABLE
    if (manager_) {
        SherpaOnnxDestroySpeakerEmbeddingManager(manager_);
    }
    if (extractor_) {
        SherpaOnnxDestroySpeakerEmbeddingExtractor(extractor_);
    }
#endif
}

bool VoiceprintLibrary::initialize(const std::string& library_dir,
                                    float threshold,
                                    const std::string& model_path)
{
    library_dir_ = library_dir;
    threshold_   = threshold;

    std::cout << "[Voiceprint] 初始化声纹库 (" << library_dir_ << ")" << std::endl;

    // 创建库目录
    mkdir_p(library_dir_);

#ifdef SHERPA_ONNX_AVAILABLE
    // 1. 创建 embedding extractor
    std::string mp = model_path.empty()
        ? "src/third_party/sherpa-onnx/speaker-verification-model/"
          "3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx"
        : model_path;

    SherpaOnnxSpeakerEmbeddingExtractorConfig extractor_cfg;
    memset(&extractor_cfg, 0, sizeof(extractor_cfg));
    extractor_cfg.model = mp.c_str();
    extractor_cfg.num_threads = 4;
    extractor_cfg.provider = "cpu";

    extractor_ = SherpaOnnxCreateSpeakerEmbeddingExtractor(&extractor_cfg);
    if (!extractor_) {
        std::cerr << "   ❌ 加载声纹模型失败: " << mp << std::endl;
        return false;
    }

    int32_t dim = SherpaOnnxSpeakerEmbeddingExtractorDim(extractor_);

    // 2. 创建 embedding manager
    manager_ = SherpaOnnxCreateSpeakerEmbeddingManager(dim);
    if (!manager_) {
        LOG_ERROR("   ❌ 创建 embedding manager 失败");
        return false;
    }

    std::cout << "   ✅ 模型就绪 (dim=" << dim
              << ", 阈值=" << threshold_ << ")" << std::endl;
#else
    LOG_WARN("   ⚠️ sherpa-onnx 未安装");
    initialized_ = true;
    return true;
#endif

    // 3. 加载已注册用户
    if (!load_library()) {
        LOG_WARN("   ⚠️ 加载声纹库失败，将从空库开始");
    }

    initialized_ = true;
    std::cout << "   ✅ 声纹库就绪 (" << count() << " 个用户)" << std::endl;

    if (!active_speaker_.empty()) {
        std::cout << "   👤 活跃用户: " << active_speaker_ << std::endl;
    }

    return true;
}

// ── 注册 ──────────────────────────────────────────────

bool VoiceprintLibrary::enroll(const std::string& wav_path,
                                const std::string& name,
                                const std::string& display_name)
{
    if (name.empty()) return false;

    std::cout << "   [Voiceprint] 🎙️ 注册: " << name;

    if (!display_name.empty()) {
        std::cout << " (" << display_name << ")";
    }
    LOG_INFO("std::endl");

    // 创建用户目录
    std::string user_dir = library_dir_ + "/" + name;
    mkdir_p(user_dir);

    // 保存注册音频
    std::string saved_wav = enroll_path(name);
    if (!copy_file(wav_path, saved_wav)) {
        LOG_ERROR("   ❌ 保存注册音频失败");
        return false;
    }

#ifdef SHERPA_ONNX_AVAILABLE
    // 计算声纹嵌入
    std::vector<float> embedding;
    if (!compute_embedding(wav_path, embedding)) {
        LOG_ERROR("   ❌ 无法计算声纹嵌入");
        return false;
    }

    // 如果已存在同名用户，先移除旧的
    SherpaOnnxSpeakerEmbeddingManagerRemove(manager_, name.c_str());

    // 注册到 manager
    int32_t ret = SherpaOnnxSpeakerEmbeddingManagerAdd(
        manager_, name.c_str(), embedding.data());
    if (ret != 1) {
        LOG_ERROR("   ❌ 注册到 embedding manager 失败");
        return false;
    }
#endif

    // 更新/创建档案
    VoiceprintProfile profile;
    profile.name         = name;
    profile.display_name = display_name.empty() ? name : display_name;
    profile.created_at   = std::time(nullptr);
    profile.sample_count = 1;

    // 查找已有档案并更新
    bool found = false;
    for (auto& p : profiles_) {
        if (p.name == name) {
            p = profile;
            found = true;
            break;
        }
    }
    if (!found) {
        profiles_.push_back(profile);
    }

    // 如果是第一个用户，自动设为活跃
    if (active_speaker_.empty()) {
        active_speaker_ = name;
    }

    save_library();

    emit("enrolled", name);
    LOG_INFO("   ✅ 注册成功");
    return true;
}

// ── 识别 ──────────────────────────────────────────────

IdentificationResult VoiceprintLibrary::identify(const std::string& wav_path)
{
    IdentificationResult result;

    if (!has_any()) {
        LOG_INFO("   [Voiceprint] ⚠️ 声纹库为空，无法识别");
        return result;
    }

#ifdef SHERPA_ONNX_AVAILABLE
    if (!extractor_ || !manager_) return result;

    std::vector<float> embedding;
    if (!compute_embedding(wav_path, embedding)) {
        LOG_ERROR("   [Voiceprint] 无法计算嵌入向量");
        return result;
    }

    // 搜索最佳匹配
    const char* matched = SherpaOnnxSpeakerEmbeddingManagerSearch(
        manager_, embedding.data(), threshold_);

    if (matched) {
        result.name = matched;
        result.confidence = 0.5f;  // Search 只返回是否通过阈值，不返回分数
        // 用 GetBestMatches 获取更精确的置信度
        const auto* matches = SherpaOnnxSpeakerEmbeddingManagerGetBestMatches(
            manager_, embedding.data(), threshold_, 1);
        if (matches && matches->count > 0) {
            result.confidence = matches->matches[0].score;
            SherpaOnnxSpeakerEmbeddingManagerFreeBestMatches(matches);
        }

        SherpaOnnxSpeakerEmbeddingManagerFreeSearch(matched);
    }

#else
    (void)wav_path;
#endif

    if (result.identified()) {
        // 查找显示名
        for (auto& p : profiles_) {
            if (p.name == result.name) {
                result.display_name = p.display_name;
                break;
            }
        }
        std::cout << "   [Voiceprint] 🎯 识别: " << result.name
                  << " (置信度=" << result.confidence << ")" << std::endl;
    } else {
        LOG_INFO("   [Voiceprint] ❓ 未识别（可能说话人未注册）");
    }

    return result;
}

bool VoiceprintLibrary::verify(const std::string& wav_path,
                                const std::string& name)
{
    if (!has_any() || name.empty()) return false;

#ifdef SHERPA_ONNX_AVAILABLE
    if (!extractor_ || !manager_) return false;

    std::vector<float> embedding;
    if (!compute_embedding(wav_path, embedding)) return false;

    int32_t matched = SherpaOnnxSpeakerEmbeddingManagerVerify(
        manager_, name.c_str(), embedding.data(), threshold_);

    bool passed = (matched == 1);
    std::cout << "   [Voiceprint] " << (passed ? "✅" : "❌")
              << " 验证 " << name << " (阈值=" << threshold_ << ")" << std::endl;

    return passed;
#else
    (void)wav_path;
    (void)name;
    return true;
#endif
}

// ── 管理 ──────────────────────────────────────────────

bool VoiceprintLibrary::remove(const std::string& name)
{
    if (name.empty()) return false;

    std::cout << "   [Voiceprint] 🗑️ 删除: " << name << std::endl;

#ifdef SHERPA_ONNX_AVAILABLE
    if (manager_) {
        SherpaOnnxSpeakerEmbeddingManagerRemove(manager_, name.c_str());
    }
#endif

    // 清理活跃用户
    if (active_speaker_ == name) {
        active_speaker_.clear();
    }

    // 从档案列表移除
    profiles_.erase(
        std::remove_if(profiles_.begin(), profiles_.end(),
                       [&name](const VoiceprintProfile& p) {
                           return p.name == name;
                       }),
        profiles_.end());

    // 删除用户目录
    std::string user_dir = library_dir_ + "/" + name;
    std::string enroll_wav = user_dir + "/enroll.wav";
    std::remove(enroll_wav.c_str());
    rmdir(user_dir.c_str());

    save_library();
    emit("removed", name);
    return true;
}

std::vector<VoiceprintProfile> VoiceprintLibrary::list_all() const
{
    return profiles_;
}

int VoiceprintLibrary::count() const
{
    return (int)profiles_.size();
}

bool VoiceprintLibrary::update_profile(const std::string& name,
                                        const VoiceprintProfile& profile)
{
    for (auto& p : profiles_) {
        if (p.name == name) {
            p = profile;
            p.name = name;  // 不允许通过 update 改名
            save_library();
            return true;
        }
    }
    return false;
}

VoiceprintProfile VoiceprintLibrary::get_profile(const std::string& name) const
{
    for (auto& p : profiles_) {
        if (p.name == name) return p;
    }
    return {};
}

// ── 活跃用户 ──────────────────────────────────────────

void VoiceprintLibrary::set_active_speaker(const std::string& name)
{
    // 验证用户存在
    bool exists = false;
    for (auto& p : profiles_) {
        if (p.name == name) {
            exists = true;
            break;
        }
    }

    if (exists) {
        active_speaker_ = name;
        std::cout << "   [Voiceprint] 👤 切换活跃用户: " << name << std::endl;
        emit("switched", name);
    } else {
        std::cerr << "   [Voiceprint] ⚠️ 用户不存在: " << name << std::endl;
    }
}

VoiceprintProfile VoiceprintLibrary::active_profile() const
{
    return get_profile(active_speaker_);
}

std::string VoiceprintLibrary::active_system_prompt() const
{
    auto profile = active_profile();
    return profile.system_prompt;
}

std::string VoiceprintLibrary::status_text() const
{
    if (!has_any()) {
        return "声纹库为空";
    }

    std::string s = std::to_string(count()) + " 个用户";
    if (!active_speaker_.empty()) {
        s += ", 当前: " + active_speaker_;
    }
    return s;
}

// ── 内部方法 ──────────────────────────────────────────

bool VoiceprintLibrary::compute_embedding(const std::string& wav_path,
                                           std::vector<float>& embedding)
{
#ifdef SHERPA_ONNX_AVAILABLE
    if (!extractor_) return false;

    const SherpaOnnxWave* wave = SherpaOnnxReadWave(wav_path.c_str());
    if (!wave) {
        std::cerr << "   [Voiceprint] 无法读取音频: " << wav_path << std::endl;
        return false;
    }

    const SherpaOnnxOnlineStream* stream =
        SherpaOnnxSpeakerEmbeddingExtractorCreateStream(extractor_);
    if (!stream) {
        SherpaOnnxFreeWave(wave);
        return false;
    }

    SherpaOnnxOnlineStreamAcceptWaveform(
        stream, wave->sample_rate, wave->samples, wave->num_samples);
    SherpaOnnxOnlineStreamInputFinished(stream);

    if (!SherpaOnnxSpeakerEmbeddingExtractorIsReady(extractor_, stream)) {
        LOG_ERROR("   [Voiceprint] 音频不足，无法计算嵌入");
        SherpaOnnxDestroyOnlineStream(stream);
        SherpaOnnxFreeWave(wave);
        return false;
    }

    const float* emb = SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(
        extractor_, stream);
    if (!emb) {
        SherpaOnnxDestroyOnlineStream(stream);
        SherpaOnnxFreeWave(wave);
        return false;
    }

    int32_t dim = SherpaOnnxSpeakerEmbeddingExtractorDim(extractor_);
    embedding.assign(emb, emb + dim);

    SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding(emb);
    SherpaOnnxDestroyOnlineStream(stream);
    SherpaOnnxFreeWave(wave);

    return true;
#else
    (void)wav_path;
    (void)embedding;
    return false;
#endif
}

bool VoiceprintLibrary::load_library()
{
    std::string index_path = library_dir_ + "/library.json";
    std::ifstream f(index_path);
    if (!f.is_open()) {
        LOG_INFO("   [Voiceprint] 未找到 library.json，创建新库");
        return save_library();
    }

    try {
        json j;
        f >> j;

        profiles_.clear();
        if (j.contains("profiles")) {
            for (auto& pj : j["profiles"]) {
                VoiceprintProfile p;
                p.name         = pj.value("name", "");
                p.display_name = pj.value("display_name", p.name);
                p.system_prompt = pj.value("system_prompt", "");
                p.created_at   = pj.value("created_at", (int64_t)0);
                p.sample_count = pj.value("sample_count", 0);
                if (p.valid()) {
                    profiles_.push_back(p);
                }
            }
        }

        active_speaker_ = j.value("active_speaker", "");
    } catch (const json::exception& e) {
        std::cerr << "   ⚠️ library.json 解析失败: " << e.what() << std::endl;
        return false;
    }

    // 从磁盘恢复 embedding（重新计算）
    std::cout << "   [Voiceprint] 恢复 " << profiles_.size() << " 个用户..." << std::endl;

    profiles_.erase(
        std::remove_if(profiles_.begin(), profiles_.end(),
                       [this](const VoiceprintProfile& p) {
#ifdef SHERPA_ONNX_AVAILABLE
                           std::string wav = enroll_path(p.name);
                           std::vector<float> emb;
                           if (!compute_embedding(wav, emb)) {
                               std::cerr << "   ⚠️ 无法恢复 " << p.name
                                         << " 的声纹，跳过" << std::endl;
                               return true;  // 移除无效用户
                           }
                           SherpaOnnxSpeakerEmbeddingManagerAdd(
                               manager_, p.name.c_str(), emb.data());
#else
                           (void)p;
#endif
                           return false;
                       }),
        profiles_.end());

    // 验证活跃用户仍存在
    if (!active_speaker_.empty()) {
        bool active_exists = false;
        for (auto& p : profiles_) {
            if (p.name == active_speaker_) {
                active_exists = true;
                break;
            }
        }
        if (!active_exists) {
            active_speaker_.clear();
        }
    }

    return true;
}

bool VoiceprintLibrary::save_library() const
{
    json j;
    j["threshold"] = threshold_;
    j["active_speaker"] = active_speaker_;

    json profiles_arr = json::array();
    for (auto& p : profiles_) {
        json pj;
        pj["name"]         = p.name;
        pj["display_name"] = p.display_name;
        pj["system_prompt"] = p.system_prompt;
        pj["created_at"]   = p.created_at;
        pj["sample_count"] = p.sample_count;
        profiles_arr.push_back(pj);
    }
    j["profiles"] = profiles_arr;

    std::string index_path = library_dir_ + "/library.json";
    std::ofstream f(index_path);
    if (!f.is_open()) {
        LOG_WARN("   ⚠️ 无法写入 library.json");
        return false;
    }

    f << j.dump(2) << std::endl;
    return true;
}

std::string VoiceprintLibrary::enroll_path(const std::string& name) const
{
    return library_dir_ + "/" + name + "/enroll.wav";
}

bool VoiceprintLibrary::copy_file(const std::string& src,
                                   const std::string& dst)
{
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary);
    if (!out) return false;
    out << in.rdbuf();
    return out.good();
}

void VoiceprintLibrary::emit(const std::string& event,
                              const std::string& detail)
{
    if (event_cb_) {
        event_cb_(event, detail);
    }
}
