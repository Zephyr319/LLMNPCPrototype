@echo off
title Local Whisper STT Server

echo ============================================
echo Starting Local Whisper STT Server
echo ============================================
echo.

set "ROOT=%~dp0"
set "WHISPER_DIR=%ROOT%LocalWhisperServer"

echo Project root:
echo %ROOT%
echo.
echo Whisper folder:
echo %WHISPER_DIR%
echo.

if not exist "%WHISPER_DIR%\server.py" (
    echo ERROR: server.py was not found.
    echo Expected:
    echo %WHISPER_DIR%\server.py
    echo.
    echo Please make sure the LocalWhisperServer folder is placed in the UE project root.
    pause
    exit /b 1
)

if not exist "%WHISPER_DIR%\venv\Scripts\activate.bat" (
    echo ERROR: venv activate.bat was not found.
    echo Expected:
    echo %WHISPER_DIR%\venv\Scripts\activate.bat
    echo.
    echo Please make sure the Python virtual environment exists.
    pause
    exit /b 1
)

pushd "%WHISPER_DIR%"

call ".\venv\Scripts\activate.bat"

echo.
echo Checking CUDA...
python -c "import torch; print('Torch:', torch.__version__); print('CUDA available:', torch.cuda.is_available()); print('GPU:', torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CUDA not available')"
echo.

echo Launching Whisper FastAPI server...
echo URL: http://127.0.0.1:8001/docs
echo Please keep this window open while using UE5.
echo.

uvicorn server:app --host 127.0.0.1 --port 8001

echo.
echo Whisper server stopped.
pause
