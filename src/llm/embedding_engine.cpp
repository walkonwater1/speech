#include "embedding_engine.h"
#include "onnx_embedding.h"
#include "http_client.h"
#include "logger.h"
#include <nlohmann/json.hpp>
#include <cmath>

using json = nlohmann::json;

EmbeddingEngine::EmbeddingEngine(const std::string& host, const std::string& model)
    : host_(host), model_(model), backend_("ollama") {}

EmbeddingEngine::~EmbeddingEngine() = default;

std::shared_ptr<EmbeddingEngine> EmbeddingEngine::create_onnx(const std::string& model_dir)
{
    auto engine = std::shared_ptr<EmbeddingEngine>(new EmbeddingEngine("", ""));
    engine->backend_ = "onnx";
    engine->onnx_ = std::make_unique<OnnxEmbedding>();
    if (!engine->onnx_->initialize(model_dir)) {
        LOG_ERROR("[Embedding] ONNX 后端初始化失败");
        return nullptr;
    }
    engine->dimension_ = engine->onnx_->dimension();
    LOG_INFO("[Embedding] ONNX 本地后端就绪 (dim={})", engine->dimension_);
    return engine;
}

std::vector<float> EmbeddingEngine::encode(const std::string& text) const
{
    auto batch = encode_batch({text});
    if (batch.empty()) throw std::runtime_error("Embedding API 返回为空");
    return batch[0];
}

std::vector<std::vector<float>> EmbeddingEngine::encode_batch(
    const std::vector<std::string>& texts) const
{
    if (texts.empty()) return {};

    if (backend_ == "onnx") {
        if (!onnx_) throw std::runtime_error("ONNX 嵌入引擎未初始化");
        auto results = onnx_->encode_batch(texts);
        if (dimension_ == 0 && !results.empty() && !results[0].empty())
            dimension_ = (int)results[0].size();
        return results;
    }

    json body;
    body["model"] = model_;
    body["input"] = texts;
    std::string url = host_ + "/api/embed";
    std::string response;
    try {
        response = HttpClient::post(url, body.dump(), 30);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Embedding 请求失败: ") + e.what());
    }
    json resp = json::parse(response);
    if (!resp.contains("embeddings") || !resp["embeddings"].is_array())
        throw std::runtime_error("Embedding 响应格式异常");

    std::vector<std::vector<float>> result;
    for (const auto& emb : resp["embeddings"]) {
        std::vector<float> vec;
        for (const auto& val : emb) vec.push_back(val.get<float>());
        float norm = 0.0f;
        for (float v : vec) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0.0f) for (float& v : vec) v /= norm;
        if (dimension_ == 0 && !vec.empty()) {
            dimension_ = (int)vec.size();
            LOG_INFO("[Embedding] 维度: {}", dimension_);
        }
        result.push_back(std::move(vec));
    }
    return result;
}
