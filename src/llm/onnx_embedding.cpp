/**
 * 本地 ONNX 嵌入引擎 — 实现
 *
 * Layer 4.2: 本地模型嵌入
 */

#include "onnx_embedding.h"
#include <onnxruntime_c_api.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <iostream>
#include <algorithm>
#include "logger.h"

using json = nlohmann::json;

// ── UTF-8 工具 ──────────────────────────────────────

namespace {

/// 判断一个 UTF-8 code point 是否是 CJK 字符
inline bool is_cjk_char(const uint8_t* p, int len)
{
    if (len < 3) return false;
    // Unicode 范围: U+4E00-U+9FFF (基本汉字), U+3400-U+4DBF (扩展A),
    //               U+F900-U+FAFF (兼容), U+3000-U+303F (中文标点),
    //               U+FF00-U+FFEF (全角字符)
    uint32_t cp = 0;
    for (int i = 0; i < len; ++i) {
        cp = (cp << 8) | p[i];
    }
    // 处理 3 字节 UTF-8 (U+0800-U+FFFF)
    if (len == 3) {
        cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    return (cp >= 0x2E80 && cp <= 0x9FFF)  // 中日韩汉字
        || (cp >= 0xF900 && cp <= 0xFAFF)  // 兼容汉字
        || (cp >= 0x3000 && cp <= 0x303F)  // 中文标点
        || (cp >= 0xFF00 && cp <= 0xFFEF)  // 全角
        || (cp >= 0x3400 && cp <= 0x4DBF); // 扩展 A
}

/// 返回一个 UTF-8 字符的字节数
/// @return 1/2/3/4, 非法字节返回 1
inline int utf8_char_len(uint8_t byte)
{
    if ((byte & 0x80) == 0) return 1;
    if ((byte & 0xE0) == 0xC0) return 2;
    if ((byte & 0xF0) == 0xE0) return 3;
    if ((byte & 0xF8) == 0xF0) return 4;
    return 1;  // 跳过非法字节
}

/// 检查字符是否为空白
inline bool is_whitespace_char(const uint8_t* p, int len)
{
    return len == 1 && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r');
}

/// 检查字符是否为标点
inline bool is_punctuation_char(const uint8_t* p, int len)
{
    if (len == 1) {
        uint8_t c = *p;
        return (c >= 33 && c <= 47) || (c >= 58 && c <= 64)
            || (c >= 91 && c <= 96) || (c >= 123 && c <= 126);
    }
    // 中文标点（全角）
    if (len == 3) {
        uint32_t cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return (cp >= 0x3000 && cp <= 0x303F)  // 、。！？【】「」 etc.
            || (cp >= 0xFF01 && cp <= 0xFF0F)
            || (cp >= 0xFF1A && cp <= 0xFF20);
    }
    return false;
}

}  // anonymous namespace

// ── Tokenizer 实现 ──────────────────────────────────

bool BertTokenizer::load(const std::string& vocab_path)
{
    vocab_.clear();

    std::ifstream f(vocab_path);
    if (!f.is_open()) {
        std::cerr << "❌ [OnnxEmbedding] 无法打开 vocab.txt: " << vocab_path << std::endl;
        return false;
    }

    std::string line;
    int id = 0;
    while (std::getline(f, line)) {
        // 去除行尾 \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        vocab_[line] = id++;

        // 记录特殊 token ID
        if (line == "[CLS]") cls_id_ = vocab_[line];
        else if (line == "[SEP]") sep_id_ = vocab_[line];
        else if (line == "[PAD]") pad_id_ = vocab_[line];
        else if (line == "[UNK]") unk_id_ = vocab_[line];
    }

    std::cout << "   [OnnxEmbedding] vocab 加载完成: " << vocab_.size()
              << " tokens (CLS=" << cls_id_ << " SEP=" << sep_id_
              << " PAD=" << pad_id_ << " UNK=" << unk_id_ << ")" << std::endl;
    return true;
}

std::vector<std::string> BertTokenizer::basic_tokenize(const std::string& text) const
{
    std::vector<std::string> tokens;

    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    size_t len = text.size();
    size_t i = 0;

    // 收集所有非空白 token
    std::vector<std::string> raw_tokens;
    std::string current;

    while (i < len) {
        int clen = utf8_char_len(data[i]);
        if (i + clen > len) clen = 1;

        // 空白处理
        if (is_whitespace_char(data + i, clen)) {
            if (!current.empty()) {
                raw_tokens.push_back(current);
                current.clear();
            }
            i += clen;
            continue;
        }

        // 中文/全角字符：在每个汉字前后加空格（BERT BasicTokenizer 行为）
        // 这样 WordPiece 会把每个汉字当作独立 token
        if (is_cjk_char(data + i, clen)) {
            if (!current.empty()) {
                raw_tokens.push_back(current);
                current.clear();
            }
            // 汉字自己就是一个 token
            current.assign(reinterpret_cast<const char*>(data + i), clen);
            raw_tokens.push_back(current);
            current.clear();
            i += clen;
            continue;
        }

        // 标点
        if (is_punctuation_char(data + i, clen)) {
            if (!current.empty()) {
                raw_tokens.push_back(current);
                current.clear();
            }
            current.assign(reinterpret_cast<const char*>(data + i), clen);
            raw_tokens.push_back(current);
            current.clear();
            i += clen;
            continue;
        }

        // ASCII / 其他字符：累积在一起
        current.append(reinterpret_cast<const char*>(data + i), clen);
        i += clen;
    }

    if (!current.empty()) {
        raw_tokens.push_back(current);
    }

    // 对每个 raw token 进行小写 + vocab 查找
    for (const auto& tok : raw_tokens) {
        std::string lower = tok;
        // ASCII 小写
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        }

        // 直接查 vocab
        if (vocab_.count(lower)) {
            tokens.push_back(lower);
        } else {
            // WordPiece 子词分割
            std::string remaining = lower;

            while (!remaining.empty()) {
                // 尝试匹配最长的前缀（前 1 个字或多个字符）
                int best_len = 0;
                // 查找最长的匹配项
                for (int try_len = (int)remaining.size(); try_len >= 1; --try_len) {
                    std::string candidate;
                    if (!tokens.empty()) {
                        candidate = "##" + remaining.substr(0, try_len);
                    } else {
                        candidate = remaining.substr(0, try_len);
                    }
                    if (vocab_.count(candidate)) {
                        best_len = try_len;
                        break;
                    }
                }

                if (best_len == 0) {
                    // 未找到任何匹配，回退到单个字符
                    // 对于中文，单个字符通常存在
                    std::string first_char = remaining.substr(0, 1);
                    // 中文是多字节的，需要按 UTF-8 长度取
                    int clen = utf8_char_len((uint8_t)remaining[0]);
                    first_char = remaining.substr(0, clen);

                    if (vocab_.count(first_char)) {
                        tokens.push_back(first_char);
                        remaining = remaining.substr(clen);
                    } else {
                        // [UNK]
                        tokens.push_back("[UNK]");
                        if (clen == 1) {
                            remaining = remaining.substr(1);
                        } else {
                            remaining = remaining.substr(clen);
                        }
                    }
                } else {
                    std::string matched;
                    if (!tokens.empty()) {
                        matched = "##" + remaining.substr(0, best_len);
                    } else {
                        matched = remaining.substr(0, best_len);
                    }
                    tokens.push_back(matched);
                    remaining = remaining.substr(best_len);
                }
            }
        }
    }

    return tokens;
}

void BertTokenizer::tokenize(const std::string& text,
                             std::vector<int64_t>& input_ids,
                             std::vector<int64_t>& attention_mask,
                             int max_length) const
{
    input_ids.clear();
    attention_mask.clear();

    // 1. [CLS]
    input_ids.push_back(cls_id_);

    // 2. Tokenize 文本
    auto tokens = basic_tokenize(text);
    for (const auto& tok : tokens) {
        auto it = vocab_.find(tok);
        if (it != vocab_.end()) {
            input_ids.push_back(it->second);
        } else {
            input_ids.push_back(unk_id_);
        }

        if ((int)input_ids.size() >= max_length - 1) break;
    }

    // 3. [SEP]
    input_ids.push_back(sep_id_);

    // 4. Pad
    int real_len = (int)input_ids.size();
    if (real_len > max_length) {
        input_ids.resize(max_length);
        real_len = max_length;
    }

    // attention_mask: 1 for real, 0 for padding
    attention_mask.resize(max_length, 0);
    for (int i = 0; i < real_len; ++i) {
        attention_mask[i] = 1;
    }

    // Pad input_ids
    while ((int)input_ids.size() < max_length) {
        input_ids.push_back(pad_id_);
    }
}

// ── ONNX 嵌入引擎实现 ───────────────────────────────

#define ORT_RETURN_IF_FAILED(expr, msg) \
    do { \
        OrtStatus* s = (expr); \
        if (s != nullptr) { \
            const char* err = api_->GetErrorMessage(s); \
            std::cerr << "❌ [OnnxEmbedding] " << msg << ": " << err << std::endl; \
            api_->ReleaseStatus(s); \
            return false; \
        } \
    } while (0)

#define ORT_IGNORE(expr) \
    do { OrtStatus* _s_ = (expr); if (_s_) api_->ReleaseStatus(_s_); } while (0)

OnnxEmbedding::OnnxEmbedding() = default;

OnnxEmbedding::~OnnxEmbedding()
{
    if (session_) {
        api_->ReleaseSession(session_);
        session_ = nullptr;
    }
    if (memory_info_) {
        api_->ReleaseMemoryInfo(memory_info_);
        memory_info_ = nullptr;
    }
    if (env_) {
        api_->ReleaseEnv(env_);
        env_ = nullptr;
    }
    // api_ 由 OrtGetApiBase 管理，不需要释放
}

bool OnnxEmbedding::initialize(const std::string& model_dir)
{
    // ── 1. 读取 config.json ─────────────────────────
    std::string cfg_path = model_dir + "/config.json";
    dim_ = 512;
    max_length_ = 512;

    try {
        std::ifstream cf(cfg_path);
        if (cf.is_open()) {
            json cfg = json::parse(cf);
            dim_ = cfg.value("dim", 512);
            max_length_ = cfg.value("max_length", 512);

            // 读取特殊 token ID（config 中已记录，tokenizer.load 会覆盖）
            if (cfg.contains("special_tokens")) {
                (void)cfg["special_tokens"];  // 已通过 tokenizer 加载
            }
        }
    } catch (...) {
        // 使用默认值
    }

    // ── 2. 加载 tokenizer ────────────────────────────
    std::string vocab_path = model_dir + "/vocab.txt";
    if (!tokenizer_.load(vocab_path)) {
        return false;
    }

    // ── 3. 获取 ONNX Runtime API ──────────────────────
    api_ = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!api_) {
        LOG_ERROR("❌ [OnnxEmbedding] 无法获取 ONNX Runtime API");
        return false;
    }

    // ── 4. 创建环境 ───────────────────────────────────
    OrtStatus* status = api_->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "embedding", &env_);
    if (status) {
        std::cerr << "❌ [OnnxEmbedding] CreateEnv 失败: "
                  << api_->GetErrorMessage(status) << std::endl;
        api_->ReleaseStatus(status);
        return false;
    }

    // ── 5. 创建 Session ───────────────────────────────
    OrtSessionOptions* session_opts = nullptr;
    status = api_->CreateSessionOptions(&session_opts);
    if (status) {
        api_->ReleaseSessionOptions(session_opts);
    }

    // 设置线程数
    if (session_opts) {
        ORT_IGNORE(api_->SetIntraOpNumThreads(session_opts, 4));
        ORT_IGNORE(api_->SetSessionGraphOptimizationLevel(session_opts, ORT_ENABLE_ALL));
    }

    std::string model_path = model_dir + "/model.onnx";
    status = api_->CreateSession(env_, model_path.c_str(), session_opts, &session_);

    if (session_opts) {
        api_->ReleaseSessionOptions(session_opts);
    }

    if (status) {
        std::cerr << "❌ [OnnxEmbedding] 加载模型失败: "
                  << model_path << " - " << api_->GetErrorMessage(status) << std::endl;
        api_->ReleaseStatus(status);
        return false;
    }

    // ── 6. 创建 MemoryInfo ────────────────────────────
    status = api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_info_);
    if (status) {
        std::cerr << "❌ [OnnxEmbedding] CreateCpuMemoryInfo 失败: "
                  << api_->GetErrorMessage(status) << std::endl;
        api_->ReleaseStatus(status);
        return false;
    }

    // ── 7. 获取输入/输出名称 ──────────────────────────
    OrtAllocator* allocator = nullptr;
    ORT_IGNORE(api_->GetAllocatorWithDefaultOptions(&allocator));

    // 输入名称
    char* name_ids = nullptr;
    ORT_RETURN_IF_FAILED(api_->SessionGetInputName(session_, 0, allocator, &name_ids),
                         "获取输入名 0");
    input_name_ids_ = name_ids;
    ORT_IGNORE(api_->AllocatorFree(allocator, name_ids));

    char* name_mask = nullptr;
    ORT_RETURN_IF_FAILED(api_->SessionGetInputName(session_, 1, allocator, &name_mask),
                         "获取输入名 1");
    input_name_mask_ = name_mask;
    ORT_IGNORE(api_->AllocatorFree(allocator, name_mask));

    char* name_type = nullptr;
    ORT_RETURN_IF_FAILED(api_->SessionGetInputName(session_, 2, allocator, &name_type),
                         "获取输入名 2");
    input_name_type_ids_ = name_type;
    ORT_IGNORE(api_->AllocatorFree(allocator, name_type));

    // 输出名称
    char* name_hidden = nullptr;
    ORT_RETURN_IF_FAILED(api_->SessionGetOutputName(session_, 0, allocator, &name_hidden),
                         "获取输出名");
    output_name_hidden_ = name_hidden;
    ORT_IGNORE(api_->AllocatorFree(allocator, name_hidden));

    std::cout << "   [OnnxEmbedding] 模型已加载 (dim=" << dim_
              << ", max_len=" << max_length_ << ")" << std::endl;
    std::cout << "   输入: [" << input_name_ids_ << ", "
              << input_name_mask_ << ", " << input_name_type_ids_ << "]" << std::endl;
    std::cout << "   输出: [" << output_name_hidden_ << "]" << std::endl;

    initialized_ = true;
    return true;
}

bool OnnxEmbedding::run_inference(const std::vector<int64_t>& input_ids,
                                  const std::vector<int64_t>& attention_mask,
                                  std::vector<float>& embedding) const
{
    if (!initialized_) return false;

    int seq_len = max_length_;
    int64_t shape[] = {1, seq_len};

    // ── 创建 tensor ─────────────────────────────────

    // token_type_ids: 全零（单句输入）
    std::vector<int64_t> token_type_ids(seq_len, 0);

    OrtValue* input_tensors[3] = {nullptr, nullptr, nullptr};
    const char* input_names[] = {
        input_name_ids_.c_str(),
        input_name_mask_.c_str(),
        input_name_type_ids_.c_str()
    };

    OrtStatus* status;

    status = api_->CreateTensorWithDataAsOrtValue(
        memory_info_,
        (void*)input_ids.data(),
        seq_len * sizeof(int64_t),
        shape, 2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
        &input_tensors[0]);
    ORT_RETURN_IF_FAILED(status, "创建 input_ids tensor");

    status = api_->CreateTensorWithDataAsOrtValue(
        memory_info_,
        (void*)attention_mask.data(),
        seq_len * sizeof(int64_t),
        shape, 2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
        &input_tensors[1]);
    ORT_RETURN_IF_FAILED(status, "创建 attention_mask tensor");

    status = api_->CreateTensorWithDataAsOrtValue(
        memory_info_,
        (void*)token_type_ids.data(),
        seq_len * sizeof(int64_t),
        shape, 2,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
        &input_tensors[2]);
    ORT_RETURN_IF_FAILED(status, "创建 token_type_ids tensor");

    // ── 运行推理 ─────────────────────────────────────

    OrtRunOptions* run_opts = nullptr;
    ORT_IGNORE(api_->CreateRunOptions(&run_opts));

    OrtValue* output_tensor = nullptr;
    const char* output_names[] = {output_name_hidden_.c_str()};

    status = api_->Run(
        session_,
        run_opts,
        input_names, input_tensors, 3,
        output_names, 1,
        &output_tensor);

    if (run_opts) api_->ReleaseRunOptions(run_opts);

    for (auto& t : input_tensors) {
        if (t) api_->ReleaseValue(t);
    }

    if (status) {
        std::cerr << "❌ [OnnxEmbedding] 推理失败: "
                  << api_->GetErrorMessage(status) << std::endl;
        api_->ReleaseStatus(status);
        return false;
    }

    // ── 提取输出 ─────────────────────────────────────

    float* output_data = nullptr;
    status = api_->GetTensorMutableData(output_tensor, (void**)&output_data);
    if (status) {
        LOG_ERROR("❌ [OnnxEmbedding] 获取输出数据失败");
        api_->ReleaseStatus(status);
        api_->ReleaseValue(output_tensor);
        return false;
    }

    // last_hidden_state: [1, seq_len, hidden_dim]
    // Mean pooling with attention_mask
    embedding.resize(dim_, 0.0f);
    mean_pool(output_data, attention_mask.data(), seq_len, dim_, embedding);

    // L2 normalize
    l2_normalize(embedding);

    api_->ReleaseValue(output_tensor);
    return true;
}

std::vector<float> OnnxEmbedding::encode(const std::string& text) const
{
    auto batch = encode_batch({text});
    if (batch.empty()) {
        return std::vector<float>(dim_, 0.0f);
    }
    return batch[0];
}

std::vector<std::vector<float>> OnnxEmbedding::encode_batch(
    const std::vector<std::string>& texts) const
{
    std::vector<std::vector<float>> results;

    if (!initialized_) {
        LOG_ERROR("❌ [OnnxEmbedding] 引擎未初始化");
        return results;
    }

    for (const auto& text : texts) {
        std::vector<int64_t> input_ids, attention_mask;
        tokenizer_.tokenize(text, input_ids, attention_mask, max_length_);

        std::vector<float> emb;
        if (run_inference(input_ids, attention_mask, emb)) {
            results.push_back(std::move(emb));
        } else {
            // 失败则返回零向量
            results.push_back(std::vector<float>(dim_, 0.0f));
        }
    }

    return results;
}

// ── Mean Pooling ────────────────────────────────────

void OnnxEmbedding::mean_pool(const float* hidden_state,
                              const int64_t* attention_mask,
                              int seq_len, int hidden_dim,
                              std::vector<float>& pooled)
{
    pooled.assign(hidden_dim, 0.0f);

    float count = 0.0f;
    for (int i = 0; i < seq_len; ++i) {
        float weight = (float)attention_mask[i];
        if (weight > 0.0f) {
            for (int j = 0; j < hidden_dim; ++j) {
                pooled[j] += hidden_state[i * hidden_dim + j];
            }
            count += 1.0f;
        }
    }

    if (count > 0.0f) {
        for (int j = 0; j < hidden_dim; ++j) {
            pooled[j] /= count;
        }
    }
}

// ── L2 归一化 ──────────────────────────────────────

void OnnxEmbedding::l2_normalize(std::vector<float>& vec)
{
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-9f) {
        for (float& v : vec) v /= norm;
    }
}
