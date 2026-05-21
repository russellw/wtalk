# wtalk
Windows voice input using a local Whisper model running on CPU.

## What it does
wtalk is a simple Windows program with a text box and two buttons. Record your speech, stop when done, and the transcription appears in the text box. Cut the text to the clipboard to paste it wherever you need it.

## Dependencies
- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) cloned alongside this repo at `C:\whisper.cpp`
- The `base.en` model at `C:\whisper.cpp\ggml-base.en.bin`
- Visual Studio (for the MSVC compiler)

## Building
From a VS developer command prompt in the repo directory:
```
build.bat
```

whisper.cpp must be built first as a static library with the static CRT:
```
cd C:\whisper.cpp
rmdir /s /q build
cmake -B build -DWHISPER_BUILD_EXAMPLES=ON -DBUILD_SHARED_LIBS=OFF "-DCMAKE_CXX_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG" "-DCMAKE_C_FLAGS_RELEASE=/MT /O2 /Ob2 /DNDEBUG"
cmake --build build --config Release
```

## Usage
| Action | Button | Shortcut |
|---|---|---|
| Raise window to foreground | — | Numpad + (global) |
| Start / stop recording | Record / Stop | F10 |
| Copy text to clipboard and clear, then minimize | Cut to Clipboard | F12 |

Transcription runs on a background thread so the UI stays responsive while the model processes audio.

Each transcription is appended to `wtalk.log` in the same directory as the executable, with a timestamp on each entry.
