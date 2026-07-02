#!/usr/bin/env python3
"""
离线 ASR + Ollama LLM 测试脚本
- ASR: SenseVoice Small (本地)
- LLM: Ollama (本地 qwen2.5:1.5b / qwen3:4b)
- 不依赖网络

对应 C++ 集成的架构：
  PCM → SenseVoice(ASR) → Ollama(LLM) → 文本输出
   ↓                          ↓
  sherpa-onnx C API        llama.cpp C API (Ollama 底层就是它)

使用方法:
    python test_ollama_pipeline.py
"""
import os
import sys
import time

# == 1. 导入模块 ==
from funasr import AutoModel   # ASR
import ollama                   # LLM (通过 Ollama API)

# == 2. 配置 ==
SENSEVOICE_PATH = os.path.expanduser("~/pretrained_models/SenseVoiceSmall/iic/SenseVoiceSmall")
OLLAMA_MODEL = "qwen2.5:1.5b"           # 你的 986MB 模型，推理快
# OLLAMA_MODEL = "qwen3:4b"             # 你的 2.5GB 模型，更强但慢

SYSTEM_PROMPT = "你叫小千，是一个18岁的女大学生，性格活泼开朗。回答简洁有趣，不超过50字。"


def load_asr():
    """加载 ASR 模型（等价于 C++ 中加载 sherpa-onnx 模型）"""
    print("[1/2] 加载 ASR: SenseVoice Small ...", end=" ", flush=True)
    model = AutoModel(
        model=SENSEVOICE_PATH,
        trust_remote_code=True,
        disable_update=True,
    )
    print("✅")
    return model


def run_asr(model, wav_path: str) -> str:
    """语音识别（等价于 C++ 中调用 sherpa-onnx recognize()）"""
    t0 = time.time()
    res = model.generate(input=wav_path, language="auto", use_itn=False)
    text = res[0]["text"].split(">")[-1]  # 去掉 <|zh|><|NEUTRAL|> 等标签
    print(f"   [ASR] \"{text}\"  ({time.time() - t0:.2f}s)")
    return text


def run_ollama(prompt: str, model_name: str = OLLAMA_MODEL) -> str:
    """调用 Ollama LLM（等价于 C++ 中调用 llama.cpp）"""
    t0 = time.time()
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": prompt},
    ]
    try:
        response = ollama.chat(model=model_name, messages=messages)
        reply = response["message"]["content"].strip()
    except Exception as e:
        reply = f"[Ollama 调用失败: {e}]"
    print(f"   [LLM] \"{reply}\"  ({time.time() - t0:.2f}s)")
    return reply


def print_arch():
    """打印架构对比"""
    print("""
┌─────────────────────────────────────────────────────────┐
│                    数据流架构                             │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Python（实验阶段）          C++（集成阶段）               │
│  ─────────────              ─────────────                │
│  SenseVoice(funasr)    →    sherpa-onnx SenseVoice      │
│       ↓                           ↓                     │
│  Ollama API            →    llama.cpp                   │
│       ↓                           ↓                     │
│  pyttsx3               →    sherpa-onnx Kokoro           │
│                                                         │
│  Ollama 底层就是 llama.cpp，所以 Python 和 C++ 用同一个   │
│  GGUF 模型文件，推理效果完全一致。                         │
└─────────────────────────────────────────────────────────┘
""")


# == 主循环 ==
def main():
    print_arch()

    # 加载 ASR（只做一次）
    asr = load_asr()

    # Ollama 不需要"加载"，服务已经在后台运行
    print(f"[2/2] Ollama 模型: {OLLAMA_MODEL} (已在后台运行)")
    print(f"     可用模型: qwen2.5:1.5b (986MB), qwen3:4b (2.5GB)")
    print()

    print("=" * 50)
    print("  输入 wav 文件 → ASR 识别 → Ollama LLM 回复")
    print("  直接输入文字 → 跳过 ASR，直接问 LLM")
    print("  输入 /qwen3 切换到 qwen3:4b")
    print("  输入 /qwen2 切换到 qwen2.5:1.5b")
    print("  输入 quit 退出")
    print("=" * 50)

    current_model = OLLAMA_MODEL

    while True:
        try:
            user_input = input("\n🎤 wav路径/文字/指令: ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not user_input:
            continue

        if user_input.lower() == "quit":
            print("👋 再见！")
            break

        # 切换模型
        if user_input == "/qwen3":
            current_model = "qwen3:4b"
            print(f"   🔄 切换到 qwen3:4b (2.5GB，推理较慢但更强)")
            continue
        if user_input == "/qwen2":
            current_model = "qwen2.5:1.5b"
            print(f"   🔄 切换到 qwen2.5:1.5b (986MB，推理快)")
            continue

        # ASR → LLM 完整管线
        if user_input.endswith(".wav"):
            if not os.path.exists(user_input):
                print(f"   ❌ 文件不存在: {user_input}")
                continue
            # Step 1: ASR
            prompt = run_asr(asr, user_input)
            if not prompt.strip():
                print("   ⚠️ 未识别到语音内容")
                continue
            # Step 2: LLM
            run_ollama(prompt, current_model)
        else:
            # 纯文字 → LLM
            run_ollama(user_input, current_model)


if __name__ == "__main__":
    main()
