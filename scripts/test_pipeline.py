#!/usr/bin/env python3
"""
离线 ASR + LLM 测试脚本（不需要网络）
适用于无 GPU 环境

使用方法：
    conda activate chatAudio
    python test_pipeline.py
"""
import os
import sys
import time

# 配置国内镜像（解决网络问题）
os.environ['HF_ENDPOINT'] = 'https://hf-mirror.com'

MODEL_DIR = os.path.expanduser("~/pretrained_models")

# ── 1. ASR: SenseVoice ──────────────────────────────────
print("=" * 50)
print("[1/3] 加载 ASR 模型: SenseVoice Small")
print("=" * 50)

from funasr import AutoModel

asr_model = AutoModel(
    model=os.path.join(MODEL_DIR, "SenseVoiceSmall/iic/SenseVoiceSmall"),
    trust_remote_code=True,
    disable_update=True,  # 跳过版本检查
)
print("✅ SenseVoice 加载完成\n")

# ── 2. LLM: Qwen2.5-0.5B ────────────────────────────────
print("=" * 50)
print("[2/3] 加载 LLM 模型: Qwen2.5-0.5B-Instruct")
print("=" * 50)

import torch
from transformers import AutoModelForCausalLM, AutoTokenizer

llm_model = AutoModelForCausalLM.from_pretrained(
    "Qwen/Qwen2.5-0.5B-Instruct",
    torch_dtype=torch.float32,
    device_map="cpu",
)
tokenizer = AutoTokenizer.from_pretrained("Qwen/Qwen2.5-0.5B-Instruct")
print("✅ Qwen2.5-0.5B 加载完成\n")


# ── 3. 测试函数 ──────────────────────────────────────────
def run_asr(wav_path: str) -> str:
    """语音识别"""
    t0 = time.time()
    res = asr_model.generate(input=wav_path, language="auto", use_itn=False)
    text = res[0]["text"].split(">")[-1]  # 去掉语言标签
    print(f"   ASR 结果: {text}")
    print(f"   耗时: {time.time() - t0:.2f}s")
    return text


def run_llm(prompt: str, max_tokens: int = 128) -> str:
    """大模型对话"""
    t0 = time.time()
    messages = [
        {"role": "system", "content": "你叫小千，是一个18岁的女大学生，性格活泼开朗，回答简洁有趣，不超过50字。"},
        {"role": "user", "content": prompt},
    ]
    text = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
    inputs = tokenizer([text], return_tensors="pt")

    outputs = llm_model.generate(**inputs, max_new_tokens=max_tokens, do_sample=True, temperature=0.7)
    reply = tokenizer.batch_decode(outputs[:, inputs.input_ids.shape[1]:], skip_special_tokens=True)[0]

    print(f"   LLM 回复: {reply}")
    print(f"   耗时: {time.time() - t0:.2f}s")
    return reply


# ── 4. 交互循环 ──────────────────────────────────────────
def main():
    print("=" * 50)
    print("  离线 ASR+LLM 测试就绪！")
    print("  输入 wav 文件路径进行识别+对话")
    print("  直接输入文字跳过 ASR，直接问 LLM")
    print("  输入 quit 退出")
    print("=" * 50)

    while True:
        try:
            user_input = input("\n🎤 请输入 wav 文件路径 / 文字 / quit: ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not user_input:
            continue

        if user_input.lower() == "quit":
            print("👋 再见！")
            break

        if user_input.endswith(".wav"):
            # ASR → LLM 完整管线
            if not os.path.exists(user_input):
                print(f"❌ 文件不存在: {user_input}")
                continue
            prompt = run_asr(user_input)
            if prompt.strip():
                run_llm(prompt)
        else:
            # 纯文字 LLM
            run_llm(user_input)


if __name__ == "__main__":
    main()
