from fastapi import FastAPI, UploadFile, File
import tempfile
import os
import numpy as np
import librosa

app = FastAPI()


@app.get("/status")
def status():
    return {
        "status": "running",
        "server": "LocalEmotionServer"
    }


@app.post("/analyze")
async def analyze_emotion(file: UploadFile = File(...)):
    suffix = os.path.splitext(file.filename)[1]
    if suffix == "":
        suffix = ".wav"

    with tempfile.NamedTemporaryFile(delete=False, suffix=suffix) as tmp:
        tmp.write(await file.read())
        tmp_path = tmp.name

    try:
        y, sr = librosa.load(tmp_path, sr=None, mono=True)

        duration = librosa.get_duration(y=y, sr=sr)

        # 音量エネルギー
        rms = librosa.feature.rms(y=y)[0]
        mean_energy = float(np.mean(rms))
        max_energy = float(np.max(rms))

        # ピッチ推定
        pitches, magnitudes = librosa.piptrack(y=y, sr=sr)
        pitch_values = pitches[magnitudes > np.median(magnitudes)]

        if len(pitch_values) > 0:
            mean_pitch = float(np.mean(pitch_values))
        else:
            mean_pitch = 0.0

        # とても簡易的なルールベース判定
        emotion = "neutral"

        if mean_energy > 0.08 and mean_pitch > 220:
            emotion = "excited"
        elif mean_energy > 0.08:
            emotion = "angry"
        elif mean_energy < 0.015:
            emotion = "calm"
        elif mean_pitch > 250:
            emotion = "nervous"

        return {
            "emotion": emotion,
            "duration": duration,
            "mean_energy": mean_energy,
            "max_energy": max_energy,
            "mean_pitch": mean_pitch,
            "sample_rate": sr
        }

    finally:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)