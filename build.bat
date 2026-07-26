@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set "CPPWINRT=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\cppwinrt"
set SRCS=main.cpp app_state.cpp process_util.cpp audio_devices.cpp audio_router.cpp audio_render.cpp audio_capture.cpp audio_fx.cpp stream_job.cpp media_info.cpp nodes.cpp ui.cpp
cl /nologo /EHsc /std:c++17 /utf-8 /DUNICODE /D_UNICODE /I"%CPPWINRT%" /Fe:ToneGraph.exe %SRCS% /link /SUBSYSTEM:WINDOWS windowsapp.lib windowscodecs.lib
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo Build succeeded: ToneGraph.exe
