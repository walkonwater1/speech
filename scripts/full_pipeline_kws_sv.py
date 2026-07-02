#!/usr/bin/env python3
"""
一条龙语音交互 + 唤醒词 + 声纹 + 对话记忆

数据流:
    麦克风 → VAD → ASR → 拼音匹配(唤醒词) → CAM++(声纹) → Ollama LLM → TTS → 播放
                                                ↓
                                          ChatMemory(对话历史)

用法:
    python scripts/full_pipeline_kws_sv.py

首次运行会自动下载 CAM++ 声纹模型 (~10MB)，需要网络。

C++ 集成对应:
    唤醒词  → ASR 结果转拼音 → 字符串匹配 (不需要额外模型)
    声纹    → sherpa-onnx speaker verification C API
    记忆    → std::vector<std::pair<string,string>> 存对话历史
"""

import os
import re
import time
import json

import sounddevice as sd
import numpy as np
from scipy.io.wavfile import write

import ollama
from funasr import AutoModel

import pyttsx3
import pygame
from pypinyin import pinyin, Style

# ══════════════════════════════════════════════════════════════
# 配置
# ══════════════════════════════════════════════════════════════

SENSEVOICE_PATH = os.path.expanduser(
    "~/pretrained_models/SenseVoiceSmall/iic/SenseVoiceSmall"
)
OLLAMA_MODEL = "qwen2.5:1.5b"
SYSTEM_PROMPT = "你叫小千，是一个18岁的女大学生，性格活泼开朗。回答简洁有趣，不超过50字。"

SAMPLE_RATE = 44100

# 唤醒词（拼音），设为空字符串 "" 则关闭唤醒词功能
WAKE_WORD = "zhan qi lai"       # "站起来"
# WAKE_WORD = "ni hao xiao qian" # "你好小千"
# WAKE_WORD = ""                 # 关闭唤醒词

# 声纹模型
SV_MODEL = "damo/speech_campplus_sv_zh-cn_16k-common"
SV_THRESHOLD = 0.35             # 声纹相似度阈值，分数越低越严格
SV_ENROLL_DIR = os.path.expanduser("~/speaker_voice/")  # 声纹注册语音目录

# ══════════════════════════════════════════════════════════════
# 1. 唤醒词模块
# ══════════════════════════════════════════════════════════════

def chinese_to_pinyin(text: str) -> str:
    """提取中文汉字 → 转拼音 → 用于匹配唤醒词"""
    chinese = re.findall(r'[一-龥]', text)
    if not chinese:
        return ""
    result = pinyin(''.join(chinese), style=Style.NORMAL)
    return ' '.join(item[0] for item in result)


def check_wake_word(asr_text: str) -> bool:
    """检查 ASR 结果是否包含唤醒词"""
    if not WAKE_WORD:
        return True  # 没设唤醒词，直接通过

    pinyin_text = chinese_to_pinyin(asr_text)
    matched = WAKE_WORD in pinyin_text
    if not matched:
        print(f"   [KWS] 未检测到唤醒词 \"{WAKE_WORD}\" (识别: {pinyin_text})")
    return matched


# ══════════════════════════════════════════════════════════════
# 2. 声纹识别模块
# ══════════════════════════════════════════════════════════════

def load_sv_model():
    """加载 CAM++ 声纹模型（首次运行自动下载 ~10MB）"""
    from modelscope.pipelines import pipeline
    print("[SV] 加载声纹模型: CAM++ ...", end=" ", flush=True)
    sv = pipeline(
        task='speaker-verification',
        model=SV_MODEL,
        model_revision='v1.0.0'
    )
    print("✅")
    return sv


def has_enrolled_voice(enroll_dir: str) -> bool:
    """检查是否有声纹注册文件"""
    os.makedirs(enroll_dir, exist_ok=True)
    for f in os.listdir(enroll_dir):
        if f.endswith('.wav'):
            return True
    return False


def verify_speaker(sv_model, enroll_dir: str, test_wav: str) -> bool:
    """对比注册声纹 vs 当前语音，返回是否同一人"""
    enroll_wav = os.path.join(enroll_dir, "enroll_0.wav")
    if not os.path.exists(enroll_wav):
        print("   [SV] ⚠️ 声纹未注册，请先说一段话注册")
        return False

    try:
        result = sv_model([enroll_wav, test_wav], thr=SV_THRESHOLD)
        score = result.get('scores', ['N/A'])[0] if 'scores' in result else 'N/A'
        is_same = result['text'] == "yes"
        status = "✅ 通过" if is_same else "❌ 拒绝"
        print(f"   [SV] {status} (分数: {score})")
        return is_same
    except Exception as e:
        print(f"   [SV] 验证失败: {e}")
        return False


# ══════════════════════════════════════════════════════════════
# 3. 对话记忆模块
# ══════════════════════════════════════════════════════════════

class ChatMemory:
    """对话历史管理（C++ 等价: std::vector<pair<string,string>>）"""

    def __init__(self, max_tokens: int = 512):
        self.history = []        # [(user_msg, assistant_msg), ...]
        self.max_tokens = max_tokens

    def add(self, user_msg: str, assistant_msg: str):
        self.history.append((user_msg, assistant_msg))
        # 保持总长度不超限
        while len(str(self.history)) > self.max_tokens * 4:  # 粗略估算
            self.history.pop(0)

    def get_context(self) -> str:
        """拼成 prompt 用的对话上下文"""
        if not self.history:
            return ""
        lines = []
        for user, assistant in self.history[-10:]:  # 最近 10 轮
            lines.append(f"User: {user}")
            lines.append(f"Assistant: {assistant}")
        return "\n".join(lines)

    def clear(self):
        self.history = []


# ══════════════════════════════════════════════════════════════
# 4. 录音、ASR、LLM、TTS 模块（与 full_pipeline.py 一致）
# ══════════════════════════════════════════════════════════════

def record_audio(filename="temp_recording.wav"):
    """按 Enter 开始/结束录音"""
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


def load_asr():
    print("[1/4] 加载 ASR: SenseVoice Small ...", end=" ", flush=True)
    model = AutoModel(
        model=SENSEVOICE_PATH,
        trust_remote_code=True,
        disable_update=True,
    )
    print("✅")
    return model


def run_asr(model, wav_path: str) -> str:
    t0 = time.time()
    res = model.generate(input=wav_path, language="auto", use_itn=False)
    text = res[0]["text"].split(">")[-1]
    print(f"   [ASR] \"{text}\"  ({time.time() - t0:.2f}s)")
    return text.strip()


def run_llm(prompt: str, memory: ChatMemory = None) -> str:
    t0 = time.time()

    # 拼接对话历史
    full_prompt = prompt
    if memory:
        context = memory.get_context()
        if context:
            full_prompt = f"{context}\nUser: {prompt}\nAssistant:"

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user", "content": full_prompt},
    ]
    try:
        response = ollama.chat(model=OLLAMA_MODEL, messages=messages)
        reply = response["message"]["content"].strip()
    except Exception as e:
        reply = f"[Ollama 错误: {e}]"
    print(f"   [LLM] \"{reply}\"  ({time.time() - t0:.2f}s)")
    return reply


def load_tts():
    print("[3/4] 初始化 TTS: pyttsx3 ...", end=" ", flush=True)
    engine = pyttsx3.init()
    engine.setProperty("rate", 200)
    voices = engine.getProperty("voices")
    if voices:
        engine.setProperty("voice", voices[0].id)
    print("✅")
    return engine


def run_tts(engine, text: str, filename="temp_reply.wav"):
    t0 = time.time()
    engine.save_to_file(text, filename)
    engine.runAndWait()
    print(f"   [TTS] 合成完成 ({time.time() - t0:.2f}s)")


def play_audio(file_path: str):
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
    print("=" * 60)
    print("  语音交互: 唤醒词 + 声纹 + 记忆 + ASR → LLM → TTS")
    features = []
    if WAKE_WORD:
        features.append(f"唤醒词: \"{WAKE_WORD}\"")
    features.append("声纹: CAM++")
    features.append("记忆: 最近10轮对话")
    print(f"  特性: {', '.join(features)}")
    print("=" * 60)
    print("  输入文字 → 直接 LLM 对话（跳过唤醒词/声纹检查）")
    print("  输入 r   → 录音 → 唤醒词检查 → 声纹验证 → LLM")
    print("  输入 enroll → 注册声纹（说 3 秒以上）")
    print("  输入 clear  → 清空对话记忆")
    print("  输入 quit  → 退出")
    print("=" * 60)

    # ---- 初始化 ----
    asr = load_asr()
    sv = load_sv_model()  # 声纹独立于唤醒词，始终加载
    tts = load_tts()
    memory = ChatMemory(max_tokens=512)
    print(f"[4/4] Ollama: {OLLAMA_MODEL} (后台就绪)")

    # 检查声纹注册状态
    sv_enrolled = has_enrolled_voice(SV_ENROLL_DIR)
    if sv:
        if sv_enrolled:
            print(f"   [SV] 声纹已注册 ✅ ({SV_ENROLL_DIR})")
        else:
            print(f"   [SV] 声纹未注册 ⚠️ 请说 'enroll' 注册")
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

        if cmd.lower() == "clear":
            memory.clear()
            print("   🧹 对话记忆已清空")
            continue

        # ── 声纹注册 ──
        if cmd.lower() == "enroll":
            wav_file = "temp_enroll.wav"
            if not record_audio(wav_file):
                continue
            # 检查时长
            import wave
            with wave.open(wav_file, 'rb') as wf:
                duration = wf.getnframes() / wf.getframerate()
            if duration < 3:
                print(f"   ⚠️ 录音仅 {duration:.1f}s，声纹注册需要 3 秒以上")
                os.remove(wav_file)
                continue
            # 保存为注册语音
            os.makedirs(SV_ENROLL_DIR, exist_ok=True)
            enroll_path = os.path.join(SV_ENROLL_DIR, "enroll_0.wav")
            os.rename(wav_file, enroll_path)
            sv_enrolled = True
            tts_text = "声纹注册完成！现在只有你可以命令我啦！"
            print(f"   ✅ {tts_text}")
            run_tts(tts, tts_text)
            play_audio("temp_reply.wav")
            os.remove("temp_reply.wav")
            continue

        # ── 语音输入 ──
        if cmd == "r":
            wav_file = "temp_recording.wav"
            if not record_audio(wav_file):
                continue
            prompt = run_asr(asr, wav_file)
            if not prompt:
                print("   ⚠️ 未识别到语音")
                os.remove(wav_file)
                continue

            # 唤醒词检查
            if WAKE_WORD and not check_wake_word(prompt):
                tts_text = "请说出正确的唤醒词"
                print(f"   🔇 {tts_text}")
                run_tts(tts, tts_text)
                play_audio("temp_reply.wav")
                os.remove("temp_reply.wav")
                os.remove(wav_file)
                continue

            # 声纹验证
            if sv:
                if not sv_enrolled:
                    tts_text = "请先注册声纹，说 enroll 开始注册"
                    run_tts(tts, tts_text)
                    play_audio("temp_reply.wav")
                    os.remove("temp_reply.wav")
                    os.remove(wav_file)
                    continue
                if not verify_speaker(sv, SV_ENROLL_DIR, wav_file):
                    tts_text = "声纹验证失败，我无法为您服务"
                    run_tts(tts, tts_text)
                    play_audio("temp_reply.wav")
                    os.remove("temp_reply.wav")
                    os.remove(wav_file)
                    continue

            os.remove(wav_file)

        else:
            # ── 文字输入（跳过唤醒词/声纹）──
            prompt = cmd

        # LLM 推理（带记忆）
        reply = run_llm(prompt, memory)

        # 更新记忆
        memory.add(prompt, reply)

        # TTS + 播放
        tts_file = "temp_reply.wav"
        run_tts(tts, reply, tts_file)
        print("   🔊 播放中...")
        play_audio(tts_file)
        os.remove(tts_file)


if __name__ == "__main__":
    main()
