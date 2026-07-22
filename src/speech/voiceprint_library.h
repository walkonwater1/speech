#pragma once
/**
 * 声纹库 (Voiceprint Library)
 *
 * Layer 3.4: 语音交互深化 — 多用户声纹管理
 *
 * 之前: SpeakerVerifier — 只有一个"default_user"，只能验证"是/否"
 * 现在: VoiceprintLibrary — 多用户注册/识别/管理
 *
 * 功能:
 *   1. 注册 — 添加新说话人（名字 + 语音样本）
 *   2. 识别 — "谁在说话？" 在所有已注册用户中搜索
 *   3. 验证 — "你是 XXX 吗？" 验证特定身份
 *   4. 管理 — 列表/删除/重命名/切换活跃用户
 *   5. 持久化 — library.json 元数据 + 每个用户的注册音频
 *
 * 存储结构:
 *   voiceprint_library/
 *   ├── library.json         # 用户元数据索引
 *   ├── {name1}/
 *   │   └── enroll.wav       # 注册音频（保留用于重新计算）
 *   └── {name2}/
 *       └── enroll.wav
 *
 * sherpa-onnx API 依赖:
 *   - SpeakerEmbeddingExtractor: 从音频提取声纹嵌入向量
 *   - SpeakerEmbeddingManager:   多用户注册/搜索/验证
 *     - Add(manager, name, embedding)
 *     - Search(manager, embedding, threshold) → name or NULL
 *     - Verify(manager, name, embedding, threshold) → 1/0
 *     - Remove(manager, name)
 *     - GetBestMatches(manager, embedding, threshold, N)
 */

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>

// 前向声明
struct SherpaOnnxSpeakerEmbeddingExtractor;
struct SherpaOnnxSpeakerEmbeddingManager;

// ── 用户声纹档案 ──────────────────────────────────────

struct VoiceprintProfile {
    std::string name;            // 唯一标识 (e.g., "zhangsan")
    std::string display_name;    // 显示名 (e.g., "张三")
    std::string system_prompt;   // 该用户的专属 system prompt
    int64_t     created_at = 0;  // 注册时间戳
    int         sample_count = 0;// 注册音频样本数

    /// 是否有效
    bool valid() const { return !name.empty(); }
};

// ── 识别结果 ──────────────────────────────────────────

struct IdentificationResult {
    std::string name;            // 最匹配用户名（空 = 未识别）
    float       confidence = 0.0f;  // 相似度 0-1
    std::string display_name;

    bool identified() const { return !name.empty(); }
};

// ── 声纹库 ────────────────────────────────────────────

class VoiceprintLibrary {
public:
    /// 回调: void(const std::string& event, const std::string& detail)
    using EventCallback = std::function<void(const std::string&, const std::string&)>;

    VoiceprintLibrary();
    ~VoiceprintLibrary();

    VoiceprintLibrary(const VoiceprintLibrary&) = delete;
    VoiceprintLibrary& operator=(const VoiceprintLibrary&) = delete;

    // ── 生命周期 ──────────────────────────────────────

    /// 初始化（加载模型 + 恢复已注册用户）
    /// @param library_dir 声纹库存放目录
    /// @param threshold   识别/验证阈值 (0-1, 越低越严格)
    /// @param model_path  CAM++ 模型路径
    bool initialize(const std::string& library_dir,
                    float threshold = 0.35f,
                    const std::string& model_path = "");

    bool initialized() const { return initialized_; }

    /// 设置事件回调
    void set_event_callback(EventCallback cb) { event_cb_ = std::move(cb); }

    // ── 注册 ──────────────────────────────────────────

    /// 注册新说话人（或更新已有说话人的声纹）
    /// @param wav_path  注册音频（建议 >3 秒）
    /// @param name      唯一标识（英文/拼音）
    /// @param display   显示名（中文）
    /// @return true 成功
    bool enroll(const std::string& wav_path, const std::string& name,
                const std::string& display_name = "");

    // ── 识别 ──────────────────────────────────────────

    /// 识别说话人（在所有已注册用户中搜索最佳匹配）
    /// @param wav_path 待识别音频
    /// @return 识别结果（name 为空 = 未匹配任何人）
    IdentificationResult identify(const std::string& wav_path);

    /// 验证说话人是否是指定用户
    /// @param wav_path  待验证音频
    /// @param name      声称的身份
    /// @return true 验证通过
    bool verify(const std::string& wav_path, const std::string& name);

    // ── 管理 ──────────────────────────────────────────

    /// 删除用户
    bool remove(const std::string& name);

    /// 列出所有已注册用户
    std::vector<VoiceprintProfile> list_all() const;

    /// 已注册用户数
    int count() const;

    /// 是否有已注册用户
    bool has_any() const { return count() > 0; }

    /// 更新用户档案
    bool update_profile(const std::string& name,
                        const VoiceprintProfile& profile);

    /// 获取用户档案
    VoiceprintProfile get_profile(const std::string& name) const;

    // ── 活跃用户 ──────────────────────────────────────

    /// 设置当前活跃用户（切换后 LLM system_prompt 随之改变）
    void set_active_speaker(const std::string& name);

    /// 当前活跃用户名
    std::string active_speaker() const { return active_speaker_; }

    /// 当前活跃用户档案
    VoiceprintProfile active_profile() const;

    /// 获取活跃用户的 system_prompt
    std::string active_system_prompt() const;

    /// 状态描述
    std::string status_text() const;

private:
    std::string library_dir_;
    float       threshold_ = 0.35f;
    bool        initialized_ = false;

    const SherpaOnnxSpeakerEmbeddingExtractor* extractor_ = nullptr;
    const SherpaOnnxSpeakerEmbeddingManager*   manager_   = nullptr;

    // 用户档案缓存（内存中）
    std::vector<VoiceprintProfile> profiles_;

    // 当前活跃用户
    std::string active_speaker_;

    // 事件回调
    EventCallback event_cb_;

    // ── 内部方法 ─────────────────────────────────────

    /// 从音频计算声纹嵌入向量
    bool compute_embedding(const std::string& wav_path,
                           std::vector<float>& embedding);

    /// 加载 library.json 并恢复所有用户
    bool load_library();

    /// 保存 library.json
    bool save_library() const;

    /// 用户注册音频路径
    std::string enroll_path(const std::string& name) const;

    /// 复制文件
    static bool copy_file(const std::string& src, const std::string& dst);

    /// 触发事件
    void emit(const std::string& event, const std::string& detail);
};
