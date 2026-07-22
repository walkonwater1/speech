#pragma once
/**
 * 本地 ONNX 嵌入引擎
 *
 * Layer 4.2: 本地模型嵌入
 *
 * 使用 ONNX Runtime C API 在进程内运行 BGE 嵌入模型，
 * 替代 Ollama /api/embed HTTP 调用。
 *
 * 依赖: libonnxruntime.so (sherpa-onnx 已附带)
 *
 * 架构:
 *   文本 → BertTokenizer (WordPiece) → input_ids
 *       → ONNX Runtime Inference → last_hidden_state
 *       → Mean Pooling (masked) → L2 Normalize → vector<float>
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

// ── ONNX Runtime C API 前向声明 ─────────────────────

struct OrtEnv;
struct OrtSession;
struct OrtMemoryInfo;
struct OrtRunOptions;
struct OrtApi;

// ── BERT Tokenizer (简化版，专注中文) ──────────────

class BertTokenizer {
public:
    /// 加载 vocab.txt（每行一个 token，行号 = token ID）
    bool load(const std::string& vocab_path);

    /// 对文本进行 tokenize
    /// @param text          输入文本（UTF-8）
    /// @param input_ids     输出：token ID 序列（含 [CLS]/[SEP]/[PAD]）
    /// @param attention_mask 输出：1=有效 token, 0=padding
    /// @param max_length    最大序列长度（默认 512）
    void tokenize(const std::string& text,
                  std::vector<int64_t>& input_ids,
                  std::vector<int64_t>& attention_mask,
                  int max_length = 512) const;

    /// vocab 大小
    int vocab_size() const { return (int)vocab_.size(); }

    /// 特殊 token ID
    int cls_id() const { return cls_id_; }
    int sep_id() const { return sep_id_; }
    int pad_id() const { return pad_id_; }
    int unk_id() const { return unk_id_; }

private:
    std::unordered_map<std::string, int> vocab_;  // token → id

    int cls_id_ = 101;
    int sep_id_ = 102;
    int pad_id_ = 0;
    int unk_id_ = 100;

    /// 将 UTF-8 字符串按 Code Point 拆分为 token 列表
    /// BERT Chinese 基本是逐字拆分（一个汉字 = 一个 token）
    std::vector<std::string> basic_tokenize(const std::string& text) const;
};

// ── ONNX 嵌入引擎 ──────────────────────────────────

class OnnxEmbedding {
public:
    OnnxEmbedding();
    ~OnnxEmbedding();

    OnnxEmbedding(const OnnxEmbedding&) = delete;
    OnnxEmbedding& operator=(const OnnxEmbedding&) = delete;

    /// 初始化：加载模型 + tokenizer
    /// @param model_dir  包含 model.onnx, vocab.txt, config.json 的目录
    bool initialize(const std::string& model_dir);

    bool initialized() const { return initialized_; }

    /// embedding 维度
    int dimension() const { return dim_; }

    /// 将文本编码为向量
    /// @param text  输入文本（UTF-8）
    /// @return L2 归一化的 embedding 向量
    std::vector<float> encode(const std::string& text) const;

    /// 批量编码
    std::vector<std::vector<float>> encode_batch(const std::vector<std::string>& texts) const;

private:
    bool initialized_ = false;
    int max_length_ = 512;
    int dim_ = 512;

    // ONNX Runtime 对象
    const OrtApi* api_ = nullptr;
    OrtEnv* env_ = nullptr;
    OrtSession* session_ = nullptr;
    OrtMemoryInfo* memory_info_ = nullptr;

    // Tokenizer
    BertTokenizer tokenizer_;

    // 输入/输出名称（C 风格字符串副本）
    std::string input_name_ids_;
    std::string input_name_mask_;
    std::string input_name_type_ids_;
    std::string output_name_hidden_;

    /// 对单个预处理好的样本运行推理
    bool run_inference(const std::vector<int64_t>& input_ids,
                       const std::vector<int64_t>& attention_mask,
                       std::vector<float>& embedding) const;

    /// Mean pooling: 对 attention_mask 为 1 的 token 取平均
    static void mean_pool(const float* hidden_state,
                          const int64_t* attention_mask,
                          int seq_len, int hidden_dim,
                          std::vector<float>& pooled);

    /// L2 归一化
    static void l2_normalize(std::vector<float>& vec);
};
