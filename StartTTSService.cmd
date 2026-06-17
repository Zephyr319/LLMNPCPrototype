@echo off
title Style-Bert-VITS2 TTS Server

echo Starting Style-Bert-VITS2 TTS Server...

pushd "%~dp0Style-Bert-VITS2"

if errorlevel 1 (
    echo Failed to enter Style-Bert-VITS2 folder.
    pause
    exit /b
)

call ".\venv\Scripts\activate.bat"

if errorlevel 1 (
    echo Failed to activate venv.
    pause
    exit /b
)

python server_fastapi.py

pause