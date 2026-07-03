#!/usr/bin/env python3
"""
Piper neural TTS wrapper — called by C++ TTSEngine via subprocess.

Usage:
  piper_tts.py <text> <output.wav> [model_path]     # output WAV file
  piper_tts.py <text> - [model_path]                # stream raw PCM to stdout

When output is '-' (stdout), first line printed to stderr is JSON metadata:
  {"sample_rate": 22050, "sample_width": 2, "channels": 1}

Requires: piper-tts, HF_ENDPOINT set to hf-mirror.com (for first download only)
"""

import os
import sys
import wave
import json

# Use hf-mirror for any HuggingFace downloads
os.environ.setdefault("HF_ENDPOINT", "https://hf-mirror.com")


def synthesize_stream(text: str, model_path: str):
    """Synthesize text, yielding audio chunks as int16 bytes."""
    import piper

    voice = piper.PiperVoice.load(model_path)

    for chunk in voice.synthesize(text):
        yield chunk.audio_int16_bytes, voice.config.sample_rate


def synthesize_to_wav(text: str, model_path: str, output_path: str) -> bool:
    """Synthesize text to WAV file."""
    import piper

    voice = piper.PiperVoice.load(model_path)

    with wave.open(output_path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(voice.config.sample_rate)
        for chunk in voice.synthesize(text):
            wf.writeframes(chunk.audio_int16_bytes)

    return True


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: piper_tts.py <text> <output.wav|-> [model_path]", file=sys.stderr)
        sys.exit(1)

    text = sys.argv[1]
    output = sys.argv[2]
    model_path = sys.argv[3] if len(sys.argv) > 3 else os.path.expanduser(
        "~/pretrained_models/piper/zh_CN/zh_CN-xiao_ya-medium.onnx"
    )

    if not os.path.exists(model_path):
        print(f"[piper_tts] Model not found: {model_path}", file=sys.stderr)
        sys.exit(1)

    if output == "-":
        # Stream raw PCM to stdout
        first_chunk = True
        sample_rate = 0
        for chunk_bytes, sr in synthesize_stream(text, model_path):
            if first_chunk:
                sample_rate = sr
                # Print metadata to stderr so C++ can read sample rate
                meta = {"sample_rate": sr, "sample_width": 2, "channels": 1}
                print(json.dumps(meta), file=sys.stderr, flush=True)
                first_chunk = False
            sys.stdout.buffer.write(chunk_bytes)
            sys.stdout.buffer.flush()
    else:
        synthesize_to_wav(text, model_path, output)
