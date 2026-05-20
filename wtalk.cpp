#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <string>
#include <thread>
#include <cstdint>
#include "whisper.h"

#pragma comment(lib, "winmm.lib")

#define WM_DONE  (WM_USER + 1)
#define IDC_EDIT 1
#define IDC_BTN  2
#define IDC_CUT  3

static const char* MODEL_PATH = "C:\\whisper.cpp\\ggml-base.en.bin";
static const int SAMPLE_RATE = 16000;
static const int NUM_BUFS    = 8;
static const int BUF_SAMPLES = SAMPLE_RATE / 10;  // 100 ms
static const int BUF_BYTES   = BUF_SAMPLES * 2;

enum State { IDLE, RECORDING, STOPPING, TRANSCRIBING };

static HWND              g_hMain;
static HWND              g_hEdit;
static HWND              g_hBtn;
static whisper_context*  g_ctx;
static State             g_state = IDLE;
static HWAVEIN           g_hWaveIn;
static WAVEHDR           g_headers[NUM_BUFS];
static int16_t           g_bufs[NUM_BUFS][BUF_SAMPLES];
static std::vector<int16_t> g_captured;
static int               g_outstanding = 0;
static std::wstring      g_result;

static HWND   g_hCut;
static HACCEL g_hAccel;

static void layout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int bh = 36;
    int half = rc.right / 2;
    SetWindowPos(g_hEdit, nullptr, 0, 0, rc.right, rc.bottom - bh, SWP_NOZORDER);
    SetWindowPos(g_hBtn,  nullptr, 0, rc.bottom - bh, half, bh, SWP_NOZORDER);
    SetWindowPos(g_hCut,  nullptr, half, rc.bottom - bh, rc.right - half, bh, SWP_NOZORDER);
}

static void requeue(WAVEHDR* hdr) {
    hdr->dwFlags = 0;
    waveInPrepareHeader(g_hWaveIn, hdr, sizeof(WAVEHDR));
    waveInAddBuffer(g_hWaveIn, hdr, sizeof(WAVEHDR));
    g_outstanding++;
}

static void finish_recording() {
    waveInClose(g_hWaveIn);
    g_state = TRANSCRIBING;

    std::thread([captured = std::move(g_captured)]() {
        std::vector<float> samples(captured.size());
        for (size_t i = 0; i < captured.size(); i++)
            samples[i] = captured[i] / 32768.0f;

        whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        p.print_progress   = false;
        p.print_realtime   = false;
        p.print_timestamps = false;

        std::string text;
        if (!samples.empty() &&
            whisper_full(g_ctx, p, samples.data(), (int)samples.size()) == 0) {
            int n = whisper_full_n_segments(g_ctx);
            for (int i = 0; i < n; i++) {
                const char* s = whisper_full_get_segment_text(g_ctx, i);
                if (s) text += s;
            }
            if (!text.empty() && text[0] == ' ')
                text.erase(0, 1);
        }

        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring wtext(wlen > 1 ? wlen - 1 : 0, L'\0');
        if (wlen > 1)
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);

        g_result = std::move(wtext);
        PostMessageW(g_hMain, WM_DONE, 0, 0);
    }).detach();
}

static void start_recording() {
    WAVEFORMATEX wfx  = {};
    wfx.wFormatTag     = WAVE_FORMAT_PCM;
    wfx.nChannels      = 1;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign    = 2;
    wfx.nAvgBytesPerSec = SAMPLE_RATE * 2;

    if (waveInOpen(&g_hWaveIn, WAVE_MAPPER, &wfx,
                   (DWORD_PTR)g_hMain, 0, CALLBACK_WINDOW) != MMSYSERR_NOERROR) {
        MessageBoxW(g_hMain, L"Failed to open microphone", L"wtalk", MB_ICONERROR);
        return;
    }

    g_captured.clear();
    g_outstanding = 0;

    for (int i = 0; i < NUM_BUFS; i++) {
        g_headers[i]              = {};
        g_headers[i].lpData       = (LPSTR)g_bufs[i];
        g_headers[i].dwBufferLength = BUF_BYTES;
        waveInPrepareHeader(g_hWaveIn, &g_headers[i], sizeof(WAVEHDR));
        waveInAddBuffer(g_hWaveIn, &g_headers[i], sizeof(WAVEHDR));
        g_outstanding++;
    }

    waveInStart(g_hWaveIn);
    g_state = RECORDING;
    SetWindowTextW(g_hBtn, L"Stop (F10)");
}

static void stop_recording() {
    g_state = STOPPING;
    SetWindowTextW(g_hBtn, L"Transcribing...");
    EnableWindow(g_hBtn, FALSE);
    waveInReset(g_hWaveIn);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hEdit = CreateWindowExW(0, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 0, 0, hwnd, (HMENU)IDC_EDIT, nullptr, nullptr);
        g_hBtn = CreateWindowW(L"BUTTON", L"Record (F10)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_BTN, nullptr, nullptr);
        g_hCut = CreateWindowW(L"BUTTON", L"Cut to Clipboard (F12)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)IDC_CUT, nullptr, nullptr);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)hFont, FALSE);
        SendMessageW(g_hBtn,  WM_SETFONT, (WPARAM)hFont, FALSE);
        SendMessageW(g_hCut,  WM_SETFONT, (WPARAM)hFont, FALSE);

        ACCEL accels[] = {
            { FVIRTKEY, VK_F10, IDC_BTN },
            { FVIRTKEY, VK_F12, IDC_CUT },
        };
        g_hAccel = CreateAcceleratorTableW(accels, 2);

        g_ctx = whisper_init_from_file(MODEL_PATH);
        if (!g_ctx) {
            MessageBoxW(hwnd, L"Failed to load whisper model", L"wtalk", MB_ICONERROR);
            return -1;
        }
        return 0;
    }
    case WM_SIZE:
        layout(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BTN) {
            if      (g_state == IDLE)      start_recording();
            else if (g_state == RECORDING) stop_recording();
        } else if (LOWORD(wp) == IDC_CUT) {
            SendMessageW(g_hEdit, EM_SETSEL, 0, -1);
            SendMessageW(g_hEdit, WM_COPY, 0, 0);
            SendMessageW(g_hEdit, WM_SETTEXT, 0, (LPARAM)L"");
        }
        return 0;
    case MM_WIM_DATA: {
        WAVEHDR* hdr = (WAVEHDR*)lp;
        if (g_state == RECORDING) {
            int n = hdr->dwBytesRecorded / sizeof(int16_t);
            auto* p = (int16_t*)hdr->lpData;
            g_captured.insert(g_captured.end(), p, p + n);
            waveInUnprepareHeader(g_hWaveIn, hdr, sizeof(WAVEHDR));
            g_outstanding--;
            requeue(hdr);
        } else if (g_state == STOPPING) {
            waveInUnprepareHeader(g_hWaveIn, hdr, sizeof(WAVEHDR));
            if (--g_outstanding == 0)
                finish_recording();
        }
        return 0;
    }
    case WM_DONE: {
        if (!g_result.empty()) {
            int len = GetWindowTextLengthW(g_hEdit);
            SendMessageW(g_hEdit, EM_SETSEL, len, len);
            if (len > 0)
                SendMessageW(g_hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
            SendMessageW(g_hEdit, EM_REPLACESEL, FALSE, (LPARAM)g_result.c_str());
        }
        g_state = IDLE;
        SetWindowTextW(g_hBtn, L"Record (F10)");
        EnableWindow(g_hBtn, TRUE);
        return 0;
    }
    case WM_DESTROY:
        if (g_ctx) whisper_free(g_ctx);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSW wc    = {};
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"wtalk";
    RegisterClassW(&wc);

    g_hMain = CreateWindowW(L"wtalk", L"wtalk",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 400,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hMain, nCmdShow);
    UpdateWindow(g_hMain);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(g_hMain, g_hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
