#!/usr/bin/env python3
"""
导出 BAAI/bge-small-zh-v1.5 嵌入模型为 ONNX 格式

使用 ModelScope 下载模型（国内网络可用）。

输出到 models/embedding/:
  - model.onnx     (~95MB, float32)
  - vocab.txt      (~100KB)
  - config.json    (max_length, dim, pooling 等)

用法:
  python scripts/export_embedding_model.py [--output models/embedding]
"""

import os
import sys
import json
import argparse
import shutil

MODEL_NAME = "BAAI/bge-small-zh-v1.5"


def _make_wrapper(model):
    """创建 ONNX 导出包装器"""
    import torch

    class BertWrapper(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.model = model

        def forward(self, input_ids, attention_mask, token_type_ids):
            outputs = self.model(
                input_ids=input_ids,
                attention_mask=attention_mask,
                token_type_ids=token_type_ids,
            )
            return outputs.last_hidden_state

    return BertWrapper(model)


def main():
    parser = argparse.ArgumentParser(description="Export BGE embedding model to ONNX")
    parser.add_argument("--output", default="models/embedding",
                        help="Output directory (default: models/embedding)")
    parser.add_argument("--model", default=MODEL_NAME,
                        help=f"Model name (default: {MODEL_NAME})")
    args = parser.parse_args()

    # 切换到项目根目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    os.chdir(project_dir)

    out_dir = args.output
    os.makedirs(out_dir, exist_ok=True)

    model_path = os.path.join(out_dir, "model.onnx")
    cfg_path = os.path.join(out_dir, "config.json")

    # ── 1. 下载模型并加载 ─────────────────────────────
    print(f"[1/4] 下载并加载模型: {args.model}")

    from modelscope import snapshot_download

    local_model_dir = snapshot_download(args.model, cache_dir=".cache/modelscope")
    print(f"      模型目录: {local_model_dir}")

    from transformers import AutoTokenizer, AutoModel
    import torch

    tokenizer = AutoTokenizer.from_pretrained(local_model_dir, trust_remote_code=True)
    bert = AutoModel.from_pretrained(local_model_dir, trust_remote_code=True)
    bert.eval()

    print(f"      vocab 大小: {len(tokenizer)}")
    print(f"      特殊 token: {tokenizer.all_special_tokens}")

    hidden_size = bert.config.hidden_size
    max_len = bert.config.max_position_embeddings
    print(f"      hidden_size: {hidden_size}")
    print(f"      max_position_embeddings: {max_len}")

    # ── 2. 保存 tokenizer 到输出目录 ───────────────────
    print(f"[2/4] 保存 tokenizer → {out_dir}/")
    tokenizer.save_pretrained(out_dir)

    # ── 3. 导出 ONNX ─────────────────────────────────
    print(f"[3/4] 导出 ONNX → {model_path}")

    wrapper = _make_wrapper(bert)
    wrapper.eval()

    test_text = "这是一个测试句子"
    encoded = tokenizer(
        test_text,
        padding="max_length",
        truncation=True,
        max_length=512,
        return_tensors="pt",
    )

    input_ids = encoded["input_ids"]
    attention_mask = encoded["attention_mask"]
    token_type_ids = encoded["token_type_ids"]
    print(f"      input_ids shape: {input_ids.shape}")

    torch.onnx.export(
        wrapper,
        (input_ids, attention_mask, token_type_ids),
        model_path,
        input_names=["input_ids", "attention_mask", "token_type_ids"],
        output_names=["last_hidden_state"],
        dynamic_axes={
            "input_ids": {0: "batch", 1: "sequence"},
            "attention_mask": {0: "batch", 1: "sequence"},
            "token_type_ids": {0: "batch", 1: "sequence"},
            "last_hidden_state": {0: "batch", 1: "sequence"},
        },
        opset_version=14,
        do_constant_folding=True,
    )
    size_mb = os.path.getsize(model_path) / 1024 / 1024
    print(f"      ONNX 模型大小: {size_mb:.1f} MB")

    # ── 4. 验证 ONNX 推理 ────────────────────────────
    print(f"[4/4] 验证 ONNX 推理")

    try:
        import onnxruntime as ort

        session = ort.InferenceSession(model_path)
        actual_inputs = [inp.name for inp in session.get_inputs()]
        actual_outputs = [out.name for out in session.get_outputs()]
        print(f"      输入: {actual_inputs}")
        print(f"      输出: {actual_outputs}")

        outputs = session.run(
            None,
            {
                "input_ids": input_ids.numpy(),
                "attention_mask": attention_mask.numpy(),
                "token_type_ids": token_type_ids.numpy(),
            },
        )
        last_hidden = outputs[0]
        print(f"      last_hidden_state shape: {last_hidden.shape}")

        # Mean pooling + normalize
        mask = attention_mask.numpy().astype("float32")
        mask_expanded = mask[:, :, None]
        masked = last_hidden * mask_expanded
        summed = masked.sum(axis=1)
        counts = mask_expanded.sum(axis=1)
        mean_pooled = summed / counts

        # L2 normalize
        norm = (mean_pooled ** 2).sum(axis=1, keepdims=True) ** 0.5
        normalized = mean_pooled / (norm + 1e-9)

        print(f"      mean_pooled shape: {mean_pooled.shape}")
        print(f"      normalized norm: {(normalized ** 2).sum():.4f}")
        print("      ✅ 推理验证通过")

    except ImportError:
        print("      ⚠️ onnxruntime 未安装，跳过验证")

    # ── 保存配置文件 ─────────────────────────────────
    cfg = {
        "model_name": args.model,
        "max_length": 512,
        "dim": hidden_size,
        "pooling": "mean",
        "normalize": True,
        "special_tokens": {
            "cls": "[CLS]",
            "sep": "[SEP]",
            "pad": "[PAD]",
            "unk": "[UNK]",
            "mask": "[MASK]",
            "cls_id": tokenizer.cls_token_id,
            "sep_id": tokenizer.sep_token_id,
            "pad_id": tokenizer.pad_token_id,
            "unk_id": tokenizer.unk_token_id,
            "mask_id": tokenizer.mask_token_id,
        },
    }
    with open(cfg_path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=2, ensure_ascii=False)

    print(f"\n✅ 完成！模型已导出到: {out_dir}/")
    print(f"   - {model_path}  ({size_mb:.1f} MB)")
    print(f"   - {out_dir}/vocab.txt")
    print(f"   - {cfg_path}")


if __name__ == "__main__":
    main()
