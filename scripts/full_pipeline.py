#!/usr/bin/env python3
"""
一条龙语音交互：录音 → ASR → LLM → TTS → 播放
全离线运行，不依赖网络

架构（C++ 开发者视角）:

    init():   加载 ASR 模型 + 初始化 TTS 引擎
    loop():
        record()   → wav 文件      (sounddevice 录音)
        asr()      → 文本           (SenseVoice 识别)
        llm()      → 回复文本       (Ollama API)
        tts()      → wav 文件      (pyttsx3 合成)
        play()     → 扬声器播放    (pygame 播放)
    shutdown(): 清理资源

    对应 C++ 集成:
        record   → 腾讯硬件 SDK (不变)
        asr      → sherpa-onnx SenseVoice C API
        llm      → llama.cpp
        tts      → sherpa-onnx Kokoro C API
        play     → 腾讯硬件 SDK (不变)

用法:
    python full_pipeline.py
"""

import os
import time
import sys

import sounddevice as sd
import numpy as np
from scipy.io.wavfile import write

import ollama
from funasr import AutoModel

import pyttsx3
import pygame

# ══════════════════════════════════════════════════════════════
# 配置
# ══════════════════════════════════════════════════════════════

SENSEVOICE_PATH = os.path.expanduser(
    "~/pretrained_models/SenseVoiceSmall/iic/SenseVoiceSmall"
)
OLLAMA_MODEL = "qwen2.5:1.5b"  # 986MB，推理快
# OLLAMA_MODEL = "qwen3:4b"    # 2.5GB，更强

SYSTEM_PROMPT = "你叫小千，是一个18岁的女大学生，性格活泼开朗。回答简洁有趣，不超过50字。"

SAMPLE_RATE = 44100  # 录音采样率

# ══════════════════════════════════════════════════════════════
# 1. 录音模块  (C++ 等价: 腾讯硬件 SDK → PCM buffer)
# ══════════════════════════════════════════════════════════════

def record_audio(filename="temp_recording.wav"):
    """按 Enter 开始/结束录音，保存为 wav"""
    input("\n🔴 按下 Enter 开始录音...")
    print("   🎙️  录音中... 再次按下 Enter 结束")

    recording = []
    try:
        def callback(indata, frames, time_info, status):
            recording.append(indata.copy())

        with sd.InputStream(samplerate=SAMPLE_RATE, channels=1, callback=callback):
            input()
    except Exception as e:
        print(f"   ❌ 录音失败: {e}")
        return False

    audio_data = np.concatenate(recording, axis=0)
    write(filename, SAMPLE_RATE, (audio_data * 32767).astype(np.int16))
    duration = len(audio_data) / SAMPLE_RATE
    print(f"   ✅ 录音完成 ({duration:.1f}s) → {filename}")
    return True


# ══════════════════════════════════════════════════════════════
# 2. ASR 模块  (C++ 等价: sherpa-onnx SenseVoice)
# ══════════════════════════════════════════════════════════════

def load_asr():
    """加载 SenseVoice 模型（只调用一次）"""
    print("[1/3] 加载 ASR: SenseVoice Small ...", end=" ", flush=True)
    model = AutoModel(
        model=SENSEVOICE_PATH,
        trust_remote_code=True,
        disable_update=True,
    )
    print("✅")
    return model


def run_asr(model, wav_path: str) -> str:
    """语音→文本"""
    t0 = time.time()
    res = model.generate(input=wav_path, language="auto", use_itn=False)
    text = res[0]["text"].split(">")[-1]  # 去掉 <|zh|><|NEUTRAL|> 等标记
    print(f"   [ASR] \"{text}\"  ({time.time() - t0:.2f}s)")
    return text.strip()


# ══════════════════════════════════════════════════════════════
# 3. LLM 模块  (C++ 等价: llama.cpp)
# ══════════════════════════════════════════════════════════════

def run_llm(prompt: str) -> str:
    """文本→AI 回复（通过 Ollama API，底层是 llama.cpp）"""
    t0 = time.time()
    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": prompt},
    ]
    try:
        response = ollama.chat(model=OLLAMA_MODEL, messages=messages)
        reply = response["message"]["content"].strip()
    except Exception as e:
        reply = f"[Ollama 错误: {e}]"
    print(f"   [LLM] \"{reply}\"  ({time.time() - t0:.2f}s)")
    return reply


# ══════════════════════════════════════════════════════════════
# 4. TTS + 播放模块  (C++ 等价: sherpa-onnx Kokoro)
# ══════════════════════════════════════════════════════════════

def load_tts():
    """初始化 pyttsx3 TTS 引擎"""
    print("[2/3] 初始化 TTS: pyttsx3 ...", end=" ", flush=True)
    engine = pyttsx3.init()
    engine.setProperty("rate", 200)  # 语速
    voices = engine.getProperty("voices")
    if voices:
        engine.setProperty("voice", voices[0].id)
    print("✅")
    return engine


def run_tts(engine, text: str, filename="temp_reply.wav"):
    """文本→语音文件"""
    t0 = time.time()
    engine.save_to_file(text, filename)
    engine.runAndWait()
    print(f"   [TTS] 合成完成 ({time.time() - t0:.2f}s) → {filename}")


def play_audio(file_path: str):
    """播放音频文件"""
    try:
        pygame.mixer.init()
        pygame.mixer.music.load(file_path)
        pygame.mixer.music.play()
        while pygame.mixer.music.get_busy():
            time.sleep(0.1)
        pygame.mixer.quit()
    except Exception as e:
        print(f"   ❌ 播放失败: {e}")


# ══════════════════════════════════════════════════════════════
# 5. 主循环
# ══════════════════════════════════════════════════════════════

def main():
    print("=" * 55)
    print("  一条龙语音交互: 录音 → ASR → Ollama LLM → TTS → 播放")
    print("  输入文字跳过录音，直接和 LLM 对话")
    print("  输入 quit 退出")
    print("=" * 55)

    # ---- 初始化（只做一次）----
    asr = load_asr()
    tts = load_tts()
    print(f"[3/3] Ollama: {OLLAMA_MODEL} (后台服务已就绪)")
    print("\n🎉 就绪！\n")

    # ---- 主循环 ----
    while True:
        try:
            cmd = input(">>> ").strip()
        except (EOFError, KeyboardInterrupt):
            break

        if not cmd:
            continue

        if cmd.lower() == "quit":
            print("👋 再见！")
            break

        # 分支1: 直接输入文字 → 跳过录音，直接 LLM
        if not cmd.startswith("r"):
            prompt = cmd
        else:
            # 分支2: 输入 "r" → 录音 → ASR → LLM
            wav_file = "temp_recording.wav"
            if not record_audio(wav_file):
                continue
            prompt = run_asr(asr, wav_file)
            if not prompt:
                print("   ⚠️ 未识别到语音，跳过")
                continue
            # 清理临时录音
            os.remove(wav_file)

        # LLM 推理
        reply = run_llm(prompt)

        # TTS 合成 + 播放
        tts_file = "temp_reply.wav"
        run_tts(tts, reply, tts_file)
        print("   🔊 播放中...")
        play_audio(tts_file)
        os.remove(tts_file)


if __name__ == "__main__":
    main()
