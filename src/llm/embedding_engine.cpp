/**
 * Embedding 引擎 — 实现
 */

#include "embedding_engine.h"
#include "http_client.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cmath>

using json = nlohmann::json;

EmbeddingEngine::EmbeddingEngine(const std::string& host, const std::string& model)
    : host_(host)
    , model_(model)
{}

std::vector<float> EmbeddingEngine::encode(const std::string& text) const
{
    auto batch = encode_batch({text});
    if (batch.empty()) {
        throw std::runtime_error("Embedding API 返回为空");
    }
    return batch[0];
}

std::vector<std::vector<float>> EmbeddingEngine::encode_batch(
    const std::vector<std::string>& texts) const
{
    if (texts.empty()) return {};

    // 构造请求
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

    // 解析响应: {"embeddings": [[0.1, 0.2, ...], [...], ...]}
    json resp = json::parse(response);

    if (!resp.contains("embeddings") || !resp["embeddings"].is_array()) {
        throw std::runtime_error("Embedding 响应格式异常");
    }

    std::vector<std::vector<float>> result;
    for (const auto& emb : resp["embeddings"]) {
        std::vector<float> vec;
        for (const auto& val : emb) {
            vec.push_back(val.get<float>());
        }

        // 归一化（便于后续计算余弦相似度）
        float norm = 0.0f;
        for (float v : vec) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (float& v : vec) v /= norm;
        }

        if (dimension_ == 0 && !vec.empty()) {
            dimension_ = (int)vec.size();
            std::cout << "   [Embedding] 维度: " << dimension_ << std::endl;
        }

        result.push_back(std::move(vec));
    }

    return result;
}
