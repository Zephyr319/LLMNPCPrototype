from fastapi import FastAPI, UploadFile, File
from faster_whisper import WhisperModel
import tempfile
import os
import torch
import librosa
import soundfile as sf
import numpy as np

app = FastAPI()

device = "cuda" if torch.cuda.is_available() else "cpu"
print(f"Using device: {device}")

MODEL_SIZE = "medium"

if device == "cuda":
    COMPUTE_TYPE = "int8_float16"
else:
    COMPUTE_TYPE = "int8"

model = WhisperModel(
    MODEL_SIZE,
    device=device,
    compute_type=COMPUTE_TYPE
)


@app.get("/status")
def status():
    return {
        "status": "running",
        "server": "LocalWhisperServer",
        "engine": "faster-whisper",
        "device": device,
        "model": MODEL_SIZE,
        "compute_type": COMPUTE_TYPE
    }


@app.post("/transcribe")
async def transcribe_audio(file: UploadFile = File(...)):
    suffix = os.path.splitext(file.filename)[1]
    if suffix == "":
        suffix = ".wav"

    tmp_path = None
    normalized_path = None

    try:
        with tempfile.NamedTemporaryFile(delete=False, suffix=suffix) as tmp:
            tmp.write(await file.read())
            tmp_path = tmp.name

        normalized_path = tmp_path.replace(suffix, "_normalized.wav")

        y, sr = librosa.load(tmp_path, sr=16000, mono=True)

        if y is None or len(y) == 0:
            return {
                "text": "",
                "device": device,
                "model": MODEL_SIZE,
                "reason": "empty_audio"
            }

        y, _ = librosa.effects.trim(y, top_db=25)

        if len(y) == 0:
            return {
                "text": "",
                "device": device,
                "model": MODEL_SIZE,
                "reason": "silent_audio"
            }

        peak = np.max(np.abs(y))
        if peak > 0:
            y = y / peak * 0.9

        duration = len(y) / 16000.0

        if duration < 0.3:
            return {
                "text": "",
                "device": device,
                "model": MODEL_SIZE,
                "reason": "too_short",
                "duration": duration
            }

        sf.write(normalized_path, y, 16000)

        japanese_prompt = (
            "\u3053\u308c\u306f\u65e5\u672c\u8a9e\u306e\u4f1a\u8a71\u3067\u3059\u3002"
            "\u8a71\u8005\u306fVR\u30b2\u30fc\u30e0\u5185\u3067NPC\u3068"
            "\u81ea\u7136\u306b\u4f1a\u8a71\u3057\u3066\u3044\u307e\u3059\u3002"
        )

        segments, info = model.transcribe(
            normalized_path,
            language="ja",
            task="transcribe",
            beam_size=5,
            vad_filter=True,
            condition_on_previous_text=False,
            initial_prompt=japanese_prompt
        )

        text = "".join(segment.text for segment in segments).strip()

        return {
            "text": text,
            "device": device,
            "model": MODEL_SIZE,
            "compute_type": COMPUTE_TYPE,
            "duration": duration,
            "language": info.language,
            "language_probability": info.language_probability
        }

    except Exception as e:
        return {
            "text": "",
            "device": device,
            "model": MODEL_SIZE,
            "compute_type": COMPUTE_TYPE,
            "error": str(e)
        }

    finally:
        if tmp_path and os.path.exists(tmp_path):
            os.remove(tmp_path)

        if normalized_path and os.path.exists(normalized_path):
            os.remove(normalized_path)