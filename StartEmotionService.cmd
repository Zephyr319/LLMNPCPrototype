@echo off
title Local Emotion Server

echo ============================================
echo Starting Local Emotion Server
echo ============================================
echo.

set "ROOT=%~dp0"
set "EMOTION_DIR=%ROOT%LocalEmotionServer"

echo Project root:
echo %ROOT%
echo.
echo Emotion folder:
echo %EMOTION_DIR%
echo.

if not exist "%EMOTION_DIR%\server.py" (
    echo ERROR: server.py was not found.
    echo Expected:
    echo %EMOTION_DIR%\server.py
    echo.
    echo Please make sure the LocalEmotionServer folder is placed in the UE project root.
    pause
    exit /b 1
)

if not exist "%EMOTION_DIR%\venv\Scripts\activate.bat" (
    echo ERROR: venv activate.bat was not found.
    echo Expected:
    echo %EMOTION_DIR%\venv\Scripts\activate.bat
    echo.
    echo Please make sure the Python virtual environment exists.
    pause
    exit /b 1
)

pushd "%EMOTION_DIR%"

call ".\venv\Scripts\activate.bat"

echo.
echo Launching Emotion FastAPI server...
echo URL: http://127.0.0.1:8002/docs
echo Please keep this window open while using UE5.
echo.

uvicorn server:app --host 127.0.0.1 --port 8002

echo.
echo Emotion server stopped.
pause
