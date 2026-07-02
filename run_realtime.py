#!/usr/bin/env python3
"""
ASR-LLM-TTS 实时语音交互 — Ubuntu Ollama 版
用法:
    conda activate chatAudio
    python run_realtime.py
"""
import pyaudio
import wave
import threading
import numpy as np
import time
import webrtcvad
import os
import ollama
from funasr import AutoModel
import pygame
import edge_tts
import asyncio
import langid
import langdetect

# ============================================
# 配置参数 — 修改这里即可
# ============================================
OLLAMA_MODEL = "qwen2.5:1.5b"       # Ollama 模型名
MODEL_DIR = "/home/lixin/pretrained_models/SenseVoiceSmall"  # SenseVoice 路径
OUTPUT_DIR = "./output"
AUDIO_RATE = 16000
CHUNK = 1024
VAD_MODE = 3
NO_SPEECH_THRESHOLD = 1.0
# ============================================

os.makedirs(OUTPUT_DIR, exist_ok=True)

# WebRTC VAD
vad = webrtcvad.Vad()
vad.set_mode(VAD_MODE)

last_active_time = time.time()
recording_active = True
segments_to_save = []
saved_intervals = []
last_vad_end_time = 0
audio_file_count = 0

# 语音识别模型 — 加载 SenseVoice
print("正在加载 SenseVoice...")
model_senceVoice = AutoModel(model=MODEL_DIR, trust_remote_code=True)
print("SenseVoice 加载完成！")
print(f"Ollama 模型: {OLLAMA_MODEL}")
print("=" * 40)
print("开始监听... 说话即可交互！Ctrl+C 退出")
print("=" * 40)

def check_vad_activity(audio_data):
    num, rate = 0, 0.4
    step = int(AUDIO_RATE * 0.02)
    flag_rate = round(rate * len(audio_data) // step)
    for i in range(0, len(audio_data), step):
        chunk = audio_data[i:i + step]
        if len(chunk) == step and vad.is_speech(chunk, sample_rate=AUDIO_RATE):
            num += 1
    return num > flag_rate

def audio_recorder():
    global recording_active, last_active_time, segments_to_save, last_vad_end_time
    p = pyaudio.PyAudio()
    stream = p.open(format=pyaudio.paInt16, channels=1, rate=AUDIO_RATE,
                    input=True, frames_per_buffer=CHUNK)
    audio_buffer = []

    while recording_active:
        data = stream.read(CHUNK, exception_on_overflow=False)
        audio_buffer.append(data)

        if len(audio_buffer) * CHUNK / AUDIO_RATE >= 0.5:
            raw_audio = b"".join(audio_buffer)
            if check_vad_activity(raw_audio):
                print("[VAD] 🎤 检测到语音...")
                last_active_time = time.time()
                segments_to_save.append((raw_audio, time.time()))
            audio_buffer = []

        if time.time() - last_active_time > NO_SPEECH_THRESHOLD:
            if segments_to_save and segments_to_save[-1][1] > last_vad_end_time:
                save_and_process()
                last_active_time = time.time()

    stream.stop_stream()
    stream.close()
    p.terminate()

async def speak_edge_tts(text, voice, output_file):
    communicate = edge_tts.Communicate(text, voice)
    await communicate.save(output_file)

def play_audio(file_path):
    try:
        pygame.mixer.init()
        pygame.mixer.music.load(file_path)
        pygame.mixer.music.play()
        while pygame.mixer.music.get_busy():
            time.sleep(0.1)
    finally:
        pygame.mixer.quit()

def save_and_process():
    global segments_to_save, last_vad_end_time, saved_intervals, audio_file_count
    audio_file_count += 1
    audio_path = f"{OUTPUT_DIR}/audio_{audio_file_count}.wav"

    if not segments_to_save:
        return
    start_time = segments_to_save[0][1]
    end_time = segments_to_save[-1][1]

    if saved_intervals and saved_intervals[-1][1] >= start_time:
        segments_to_save.clear()
        return

    # 停止当前播放
    if pygame.mixer.get_init() and pygame.mixer.music.get_busy():
        pygame.mixer.music.stop()

    # 保存 WAV
    with wave.open(audio_path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(AUDIO_RATE)
        wf.writeframes(b"".join(seg[0] for seg in segments_to_save))
    print(f"[REC] 已保存: {audio_path}")

    saved_intervals.append((start_time, end_time))
    segments_to_save.clear()

    # 在新线程中做推理
    threading.Thread(target=inference_pipeline, args=(audio_path,)).start()

def inference_pipeline(audio_path):
    global audio_file_count

    # 1. ASR — SenseVoice 语音识别
    res = model_senceVoice.generate(input=audio_path, cache={},
                                    language="auto", use_itn=False)
    prompt = res[0]["text"].split(">")[-1] + "，回答简短一些，保持50字以内！"
    print(f"[ASR] {prompt}")

    # 2. LLM — Ollama 大模型推理
    try:
        response = ollama.chat(model=OLLAMA_MODEL, messages=[
            {"role": "system", "content": "你叫千问，是一个18岁的女大学生，性格活泼开朗，说话俏皮"},
            {"role": "user", "content": prompt},
        ])
        answer = response["message"]["content"]
    except Exception as e:
        print(f"[LLM] Ollama 错误: {e}")
        return
    print(f"[LLM] {answer}")

    # 3. TTS — 语种检测 + EdgeTTS 合成
    lang, _ = langid.classify(answer)
    speaker_map = {"ja": "ja-JP-NanamiNeural", "fr": "fr-FR-DeniseNeural",
                   "es": "ca-ES-JoanaNeural", "de": "de-DE-KatjaNeural",
                   "zh": "zh-CN-XiaoyiNeural", "en": "en-US-AnaNeural"}
    speaker = speaker_map.get(lang, "zh-CN-XiaoyiNeural")
    print(f"[TTS] 语种={lang}, 音色={speaker}")

    mp3_path = f"{OUTPUT_DIR}/reply_{audio_file_count}.mp3"
    asyncio.run(speak_edge_tts(answer, speaker, mp3_path))
    play_audio(mp3_path)

if __name__ == "__main__":
    try:
        audio_thread = threading.Thread(target=audio_recorder, daemon=True)
        audio_thread.start()
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("
正在退出...")
        recording_active = False
        print("已退出")
