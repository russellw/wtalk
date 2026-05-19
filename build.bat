@echo off
setlocal

set WHISPER=C:\whisper.cpp
set WLIB=%WHISPER%\build\src\Release
set GLIB=%WHISPER%\build\ggml\src\Release

cl /nologo /EHsc /std:c++17 /O2 /W3 /MT ^
   /I%WHISPER%\include /I%WHISPER%\ggml\include ^
   wtalk.cpp ^
   /link /SUBSYSTEM:WINDOWS ^
   /LIBPATH:%WLIB% /LIBPATH:%GLIB% ^
   whisper.lib ggml.lib ggml-base.lib ggml-cpu.lib ^
   winmm.lib user32.lib gdi32.lib kernel32.lib advapi32.lib ^
   /OUT:wtalk.exe
