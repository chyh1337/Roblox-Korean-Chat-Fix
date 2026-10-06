#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <imm.h>

#include "resource1.h"

#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "imm32.lib")

#ifndef IMC_GETCONVERSIONMODE
#define IMC_GETCONVERSIONMODE 0x0001
#endif

// ============================================================
// 기준 해상도
// ============================================================

constexpr int BASE_WIDTH = 1920;
constexpr int BASE_HEIGHT = 1080;

// Roblox 채팅 토글 버튼
constexpr int CHAT_BUTTON_LEFT = 123;
constexpr int CHAT_BUTTON_TOP = 18;
constexpr int CHAT_BUTTON_RIGHT = 155;
constexpr int CHAT_BUTTON_BOTTOM = 51;

// Roblox 채팅 텍스트 박스
constexpr int CHAT_LEFT = 60;
constexpr int CHAT_TOP = 373;
constexpr int CHAT_RIGHT = 440;
constexpr int CHAT_BOTTOM = 410;


// ============================================================
// 전역 변수
// ============================================================

HINSTANCE g_instance = nullptr;

HWND g_mainWindow = nullptr;
HWND g_startButton = nullptr;
HWND g_statusText = nullptr;
HWND g_monitorCombo = nullptr;
HWND g_noticeText = nullptr;

HWND g_overlay = nullptr;

std::atomic<bool> g_running = false;
std::atomic<bool> g_exit = false;

// 실제 Roblox 채팅 UI가 켜져 있는지
std::atomic<bool> g_chatEnabled = false;

// 현재 텍스트 입력 중인지
std::atomic<bool> g_chatActive = false;

// Enter 처리
std::atomic<bool> g_enterPending = false;
bool g_enterWasKorean = false;

// 마우스 토글 버튼 중복 클릭 방지
std::atomic<bool> g_buttonClickPending = false;

// 현재 선택된 모니터
HMONITOR g_selectedMonitor = nullptr;


// ============================================================
// 모니터 정보
// ============================================================

struct MonitorInfo
{
    HMONITOR handle;
    RECT rect;
    RECT workRect;
    std::wstring name;
    int width;
    int height;
};

std::vector<MonitorInfo> g_monitors;


// ============================================================
// Right Alt
// ============================================================

void PressRightAlt()
{
    INPUT inputs[2]{};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0;
    inputs[0].ki.wScan = 0x38;
    inputs[0].ki.dwFlags =
        KEYEVENTF_SCANCODE |
        KEYEVENTF_EXTENDEDKEY;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0;
    inputs[1].ki.wScan = 0x38;
    inputs[1].ki.dwFlags =
        KEYEVENTF_SCANCODE |
        KEYEVENTF_EXTENDEDKEY |
        KEYEVENTF_KEYUP;

    SendInput(
        2,
        inputs,
        sizeof(INPUT)
    );
}


// ============================================================
// IME 상태 확인
// ============================================================

bool IsKoreanMode()
{
    HWND foreground =
        GetForegroundWindow();

    if (!foreground)
        return false;

    HWND imeWindow =
        ImmGetDefaultIMEWnd(
            foreground
        );

    if (!imeWindow)
        return false;

    LRESULT conversionMode =
        SendMessageW(
            imeWindow,
            WM_IME_CONTROL,
            IMC_GETCONVERSIONMODE,
            0
        );

    return
        (conversionMode & IME_CMODE_NATIVE) != 0;
}


// ============================================================
// 모니터 열거
// ============================================================

BOOL CALLBACK MonitorEnumProc(
    HMONITOR monitor,
    HDC,
    LPRECT,
    LPARAM)
{
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);

    if (!GetMonitorInfoW(
        monitor,
        &info))
    {
        return TRUE;
    }

    MonitorInfo item{};

    item.handle = monitor;
    item.rect = info.rcMonitor;
    item.workRect = info.rcWork;

    item.width =
        info.rcMonitor.right -
        info.rcMonitor.left;

    item.height =
        info.rcMonitor.bottom -
        info.rcMonitor.top;

    item.name =
        info.szDevice;

    g_monitors.push_back(item);

    return TRUE;
}

void EnumerateMonitors()
{
    g_monitors.clear();

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        MonitorEnumProc,
        0
    );
}


// ============================================================
// 현재 선택된 모니터
// ============================================================

MonitorInfo* GetSelectedMonitorInfo()
{
    for (auto& monitor : g_monitors)
    {
        if (monitor.handle ==
            g_selectedMonitor)
        {
            return &monitor;
        }
    }

    if (!g_monitors.empty())
        return &g_monitors[0];

    return nullptr;
}


// ============================================================
// 모니터 기준 좌표 변환
// ============================================================

int ScaleX(
    int x,
    const MonitorInfo& monitor)
{
    return
        (x * monitor.width) /
        BASE_WIDTH;
}

int ScaleY(
    int y,
    const MonitorInfo& monitor)
{
    return
        (y * monitor.height) /
        BASE_HEIGHT;
}


// ============================================================
// 채팅 토글 버튼 영역
// ============================================================

bool IsCursorInChatButton()
{
    POINT point{};

    if (!GetCursorPos(&point))
        return false;

    MonitorInfo* monitor =
        GetSelectedMonitorInfo();

    if (!monitor)
        return false;

    int left =
        monitor->rect.left +
        ScaleX(
            CHAT_BUTTON_LEFT,
            *monitor
        );

    int top =
        monitor->rect.top +
        ScaleY(
            CHAT_BUTTON_TOP,
            *monitor
        );

    int right =
        monitor->rect.left +
        ScaleX(
            CHAT_BUTTON_RIGHT,
            *monitor
        );

    int bottom =
        monitor->rect.top +
        ScaleY(
            CHAT_BUTTON_BOTTOM,
            *monitor
        );

    return
        point.x >= left &&
        point.x <= right &&
        point.y >= top &&
        point.y <= bottom;
}


// ============================================================
// 텍스트 박스 히트박스
// ============================================================

bool IsCursorInChatBox()
{
    POINT point{};

    if (!GetCursorPos(&point))
        return false;

    MonitorInfo* monitor =
        GetSelectedMonitorInfo();

    if (!monitor)
        return false;

    int left =
        monitor->rect.left +
        ScaleX(
            CHAT_LEFT,
            *monitor
        );

    int top =
        monitor->rect.top +
        ScaleY(
            CHAT_TOP,
            *monitor
        );

    int right =
        monitor->rect.left +
        ScaleX(
            CHAT_RIGHT,
            *monitor
        );

    int bottom =
        monitor->rect.top +
        ScaleY(
            CHAT_BOTTOM,
            *monitor
        );

    return
        point.x >= left &&
        point.x <= right &&
        point.y >= top &&
        point.y <= bottom;
}


// ============================================================
// 텍스트 박스 히트박스 Overlay
// ============================================================

LRESULT CALLBACK OverlayProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCHITTEST:
        // 마우스 클릭을 절대 가로채지 않음
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};

        BeginPaint(
            hwnd,
            &ps
        );

        EndPaint(
            hwnd,
            &ps
        );

        return 0;
    }
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam
    );
}


// ============================================================
// Overlay 위치 갱신
// ============================================================

void UpdateOverlayPosition()
{
    if (!g_overlay)
        return;

    if (!g_chatEnabled)
        return;

    MonitorInfo* monitor =
        GetSelectedMonitorInfo();

    if (!monitor)
        return;

    int x =
        monitor->rect.left +
        ScaleX(
            CHAT_LEFT,
            *monitor
        );

    int y =
        monitor->rect.top +
        ScaleY(
            CHAT_TOP,
            *monitor
        );

    int width =
        ScaleX(
            CHAT_RIGHT - CHAT_LEFT,
            *monitor
        );

    int height =
        ScaleY(
            CHAT_BOTTOM - CHAT_TOP,
            *monitor
        );

    SetWindowPos(
        g_overlay,
        HWND_TOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE |
        SWP_SHOWWINDOW
    );
}


// ============================================================
// Overlay 생성
// ============================================================

void ShowOverlay()
{
    if (!g_chatEnabled)
        return;

    if (g_overlay)
    {
        UpdateOverlayPosition();

        // 무조건 최상단
        SetWindowPos(
            g_overlay,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE |
            SWP_NOSIZE |
            SWP_NOACTIVATE |
            SWP_SHOWWINDOW
        );

        return;
    }

    WNDCLASSW wc{};

    wc.lpfnWndProc =
        OverlayProc;

    wc.hInstance =
        g_instance;

    wc.lpszClassName =
        L"RobloxChatFixOverlayClass";

    RegisterClassW(&wc);

    MonitorInfo* monitor =
        GetSelectedMonitorInfo();

    if (!monitor)
        return;

    int x =
        monitor->rect.left +
        ScaleX(
            CHAT_LEFT,
            *monitor
        );

    int y =
        monitor->rect.top +
        ScaleY(
            CHAT_TOP,
            *monitor
        );

    int width =
        ScaleX(
            CHAT_RIGHT - CHAT_LEFT,
            *monitor
        );

    int height =
        ScaleY(
            CHAT_BOTTOM - CHAT_TOP,
            *monitor
        );

    g_overlay =
        CreateWindowExW(
            WS_EX_LAYERED |
            WS_EX_TRANSPARENT |
            WS_EX_NOACTIVATE |
            WS_EX_TOOLWINDOW |
            WS_EX_TOPMOST,

            wc.lpszClassName,
            L"",

            WS_POPUP,

            x,
            y,
            width,
            height,

            nullptr,
            nullptr,
            g_instance,
            nullptr
        );

    if (!g_overlay)
        return;

    // 완전 투명
    SetLayeredWindowAttributes(
        g_overlay,
        0,
        0,
        LWA_ALPHA
    );

    SetWindowPos(
        g_overlay,
        HWND_TOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE |
        SWP_SHOWWINDOW
    );

    UpdateWindow(g_overlay);
}


// ============================================================
// Overlay 제거
// ============================================================

void HideOverlay()
{
    if (!g_overlay)
        return;

    DestroyWindow(
        g_overlay
    );

    g_overlay = nullptr;
}


// ============================================================
// UI 업데이트
// ============================================================

void UpdateUI()
{
    if (!g_startButton)
        return;

    if (g_running)
    {
        SetWindowTextW(
            g_startButton,
            L"중지"
        );

        SetWindowTextW(
            g_statusText,
            L"실행됨"
        );

        if (g_chatEnabled)
            ShowOverlay();
        else
            HideOverlay();
    }
    else
    {
        SetWindowTextW(
            g_startButton,
            L"시작"
        );

        SetWindowTextW(
            g_statusText,
            L"중지됨"
        );

        HideOverlay();
    }
}


// ============================================================
// 채팅 활성화
// ============================================================

void EnableChat()
{
    if (!g_running)
        return;

    if (g_chatEnabled)
        return;

    g_chatEnabled = true;

    UpdateUI();

    OutputDebugStringW(
        L"[RCF V2] CHAT ENABLED\n"
    );
}


// ============================================================
// 채팅 비활성화
// ============================================================

void DisableChat()
{
    g_chatEnabled = false;
    g_chatActive = false;
    g_enterPending = false;

    HideOverlay();
    UpdateUI();

    OutputDebugStringW(
        L"[RCF V2] CHAT DISABLED\n"
    );
}


// ============================================================
// 텍스트 입력 시작
// ============================================================

void StartChat()
{
    if (!g_running)
        return;

    if (!g_chatEnabled)
        return;

    if (g_chatActive)
        return;

    g_chatActive = true;

    OutputDebugStringW(
        L"[RCF V2] CHAT TEXT START\n"
    );

    std::thread([]()
        {
            Sleep(80);

            if (!g_running)
                return;

            if (!g_chatEnabled)
                return;

            if (!g_chatActive)
                return;

            PressRightAlt();

            OutputDebugStringW(
                L"[RCF V2] CHAT START -> RIGHT ALT\n"
            );

        }).detach();
}


// ============================================================
// Raw Input
// ============================================================

LRESULT CALLBACK RawInputWindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    if (msg != WM_INPUT)
    {
        return DefWindowProcW(
            hwnd,
            msg,
            wParam,
            lParam
        );
    }

    UINT size = 0;

    GetRawInputData(
        reinterpret_cast<HRAWINPUT>(lParam),
        RID_INPUT,
        nullptr,
        &size,
        sizeof(RAWINPUTHEADER)
    );

    if (size == 0)
        return 0;

    BYTE* buffer =
        new BYTE[size];

    UINT result =
        GetRawInputData(
            reinterpret_cast<HRAWINPUT>(lParam),
            RID_INPUT,
            buffer,
            &size,
            sizeof(RAWINPUTHEADER)
        );

    if (result == size)
    {
        RAWINPUT* raw =
            reinterpret_cast<RAWINPUT*>(
                buffer
                );

        if (raw->header.dwType ==
            RIM_TYPEKEYBOARD)
        {
            RAWKEYBOARD& keyboard =
                raw->data.keyboard;

            bool keyUp =
                (keyboard.Flags & RI_KEY_BREAK) != 0;

            USHORT vKey =
                keyboard.VKey;

            // --------------------------------------------
            // KEY DOWN
            // --------------------------------------------

            if (!keyUp)
            {
                // /
                if (vKey == VK_OEM_2)
                {
                    // /를 누르면 채팅을 영구 활성화
                    if (!g_chatEnabled)
                    {
                        EnableChat();
                    }

                    StartChat();
                }

                // Enter
                else if (vKey == VK_RETURN)
                {
                    if (g_chatActive)
                    {
                        // Roblox가 채팅을 닫기 전에 확인
                        g_enterWasKorean =
                            IsKoreanMode();

                        g_enterPending = true;

                        if (g_enterWasKorean)
                        {
                            OutputDebugStringW(
                                L"[RCF V2] "
                                L"ENTER DOWN = KOREAN\n"
                            );
                        }
                        else
                        {
                            OutputDebugStringW(
                                L"[RCF V2] "
                                L"ENTER DOWN = ENGLISH\n"
                            );
                        }
                    }
                }

                // ESC는 아무것도 하지 않음
            }

            // --------------------------------------------
            // KEY UP
            // --------------------------------------------

            else
            {
                if (vKey == VK_RETURN)
                {
                    if (g_enterPending)
                    {
                        g_enterPending = false;
                        g_chatActive = false;

                        if (g_enterWasKorean)
                        {
                            OutputDebugStringW(
                                L"[RCF V2] "
                                L"KOREAN CHAT END -> ENGLISH\n"
                            );

                            PressRightAlt();
                        }
                        else
                        {
                            OutputDebugStringW(
                                L"[RCF V2] "
                                L"ENGLISH CHAT END -> NOTHING\n"
                            );
                        }
                    }
                }
            }
        }
    }

    delete[] buffer;

    return 0;
}


// ============================================================
// Raw Input Thread
// ============================================================

void RawInputThread()
{
    WNDCLASSW wc{};

    wc.lpfnWndProc =
        RawInputWindowProc;

    wc.hInstance =
        g_instance;

    wc.lpszClassName =
        L"RobloxChatFixRawInput";

    RegisterClassW(&wc);

    HWND hwnd =
        CreateWindowExW(
            0,
            wc.lpszClassName,
            L"",
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            nullptr,
            g_instance,
            nullptr
        );

    if (!hwnd)
        return;

    RAWINPUTDEVICE device{};

    device.usUsagePage = 0x01;
    device.usUsage = 0x06;
    device.dwFlags =
        RIDEV_INPUTSINK;
    device.hwndTarget =
        hwnd;

    if (!RegisterRawInputDevices(
        &device,
        1,
        sizeof(device)))
    {
        MessageBoxW(
            nullptr,
            L"Raw Input 등록에 실패했습니다.",
            L"RobloxChatFix",
            MB_ICONERROR
        );

        DestroyWindow(hwnd);
        return;
    }

    MSG msg{};

    while (!g_exit)
    {
        BOOL result =
            GetMessageW(
                &msg,
                nullptr,
                0,
                0
            );

        if (result <= 0)
            break;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    RAWINPUTDEVICE removeDevice{};

    removeDevice.usUsagePage = 0x01;
    removeDevice.usUsage = 0x06;
    removeDevice.dwFlags =
        RIDEV_REMOVE;
    removeDevice.hwndTarget =
        nullptr;

    RegisterRawInputDevices(
        &removeDevice,
        1,
        sizeof(removeDevice)
    );

    DestroyWindow(hwnd);
}


// ============================================================
// 마우스 Thread
// ============================================================

void MouseThread()
{
    bool previousLeft = false;

    while (!g_exit)
    {
        if (g_running)
        {
            bool leftPressed =
                (GetAsyncKeyState(
                    VK_LBUTTON
                ) & 0x8000) != 0;

            if (leftPressed &&
                !previousLeft)
            {
                // ----------------------------------------
                // 1. 채팅 토글 버튼
                // ----------------------------------------

                if (IsCursorInChatButton())
                {
                    if (g_chatEnabled)
                    {
                        DisableChat();
                    }
                    else
                    {
                        EnableChat();
                    }
                }

                // ----------------------------------------
                // 2. 채팅 텍스트 박스
                // ----------------------------------------

                else if (g_chatEnabled &&
                    IsCursorInChatBox())
                {
                    StartChat();
                }
            }

            previousLeft =
                leftPressed;
        }
        else
        {
            previousLeft = false;
        }

        // Overlay는 항상 최상단 유지
        if (g_running &&
            g_chatEnabled &&
            g_overlay)
        {
            SetWindowPos(
                g_overlay,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE |
                SWP_NOSIZE |
                SWP_NOACTIVATE |
                SWP_SHOWWINDOW
            );
        }

        Sleep(5);
    }
}


// ============================================================
// 프로그램 시작
// ============================================================

void StartProgram()
{
    if (g_running)
        return;

    // 프로그램 시작 시 채팅은 OFF
    g_chatEnabled = false;
    g_chatActive = false;
    g_enterPending = false;

    g_running = true;

    UpdateUI();

    OutputDebugStringW(
        L"[RCF V2] PROGRAM STARTED\n"
    );
}


// ============================================================
// 프로그램 중지
// ============================================================

void StopProgram()
{
    if (!g_running)
        return;

    g_running = false;

    g_chatEnabled = false;
    g_chatActive = false;
    g_enterPending = false;

    HideOverlay();
    UpdateUI();

    OutputDebugStringW(
        L"[RCF V2] PROGRAM STOPPED\n"
    );
}


// ============================================================
// 안내문
// ============================================================

const wchar_t* NOTICE_TEXT =
L"1. 이 프로그램은 Roblox를 전체 화면으로 실행해야 합니다.\n"
L"2. 마우스 클릭은 히트박스 방식으로 감지합니다. "
L"Roblox를 실행한 후 프로그램을 실행해 주세요.\n"
L"3. 1920×1080 해상도가 아닌 환경에서는 "
L"마우스 클릭을 정확하게 감지하지 못할 수 있습니다.";


// ============================================================
// 메인 Window
// ============================================================

LRESULT CALLBACK MainWindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        // 제목
        CreateWindowW(
            L"STATIC",
            L"RobloxChatFix",
            WS_CHILD |
            WS_VISIBLE,
            20,
            15,
            250,
            25,
            hwnd,
            nullptr,
            g_instance,
            nullptr
        );

        // 시작 / 중지
        g_startButton =
            CreateWindowW(
                L"BUTTON",
                L"시작",
                WS_CHILD |
                WS_VISIBLE |
                BS_PUSHBUTTON,
                20,
                50,
                100,
                35,
                hwnd,
                reinterpret_cast<HMENU>(
                    1001
                    ),
                g_instance,
                nullptr
            );

        // 상태
        g_statusText =
            CreateWindowW(
                L"STATIC",
                L"중지됨",
                WS_CHILD |
                WS_VISIBLE,
                135,
                58,
                210,
                25,
                hwnd,
                nullptr,
                g_instance,
                nullptr
            );

        // 모니터 선택
        CreateWindowW(
            L"STATIC",
            L"모니터:",
            WS_CHILD |
            WS_VISIBLE,
            20,
            92,
            60,
            25,
            hwnd,
            nullptr,
            g_instance,
            nullptr
        );

        g_monitorCombo =
            CreateWindowW(
                L"COMBOBOX",
                L"",
                WS_CHILD |
                WS_VISIBLE |
                WS_VSCROLL |
                CBS_DROPDOWNLIST,
                80,
                88,
                260,
                180,
                hwnd,
                reinterpret_cast<HMENU>(
                    1002
                    ),
                g_instance,
                nullptr
            );

        // 모니터 목록
        EnumerateMonitors();

        HMONITOR primary =
            MonitorFromWindow(
                nullptr,
                MONITOR_DEFAULTTOPRIMARY
            );

        int primaryIndex = 0;

        for (size_t i = 0;
            i < g_monitors.size();
            ++i)
        {
            const auto& monitor =
                g_monitors[i];

            std::wstringstream text;

            text
                << L"모니터 "
                << (i + 1)
                << L" ("
                << monitor.width
                << L"×"
                << monitor.height
                << L")";

            SendMessageW(
                g_monitorCombo,
                CB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(
                    text.str().c_str()
                    )
            );

            if (monitor.handle ==
                primary)
            {
                primaryIndex =
                    static_cast<int>(i);
            }
        }

        if (!g_monitors.empty())
        {
            g_selectedMonitor =
                g_monitors[
                    primaryIndex
                ].handle;

            SendMessageW(
                g_monitorCombo,
                CB_SETCURSEL,
                primaryIndex,
                0
            );
        }

        // 안내문
        g_noticeText =
            CreateWindowW(
                L"STATIC",
                NOTICE_TEXT,
                WS_CHILD |
                WS_VISIBLE |
                SS_LEFT,
                20,
                125,
                340,
                90,
                hwnd,
                nullptr,
                g_instance,
                nullptr
            );

        // 안내문 빨간색
        SendMessageW(
            g_noticeText,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(
                GetStockObject(
                    DEFAULT_GUI_FONT
                )
                ),
            TRUE
        );

        return 0;
    }


    // --------------------------------------------------------
    // 버튼 / 콤보박스
    // --------------------------------------------------------

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc =
            reinterpret_cast<HDC>(wParam);

        HWND control =
            reinterpret_cast<HWND>(lParam);

        if (control == g_noticeText)
        {
            SetTextColor(
                hdc,
                RGB(220, 0, 0)
            );

            SetBkMode(
                hdc,
                TRANSPARENT
            );

            return reinterpret_cast<LRESULT>(
                GetStockObject(
                    NULL_BRUSH
                )
                );
        }

        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 1001)
        {
            if (g_running)
                StopProgram();
            else
                StartProgram();

            return 0;
        }

        if (LOWORD(wParam) == 1002 &&
            HIWORD(wParam) == CBN_SELCHANGE)
        {
            int index =
                static_cast<int>(
                    SendMessageW(
                        g_monitorCombo,
                        CB_GETCURSEL,
                        0,
                        0
                    )
                    );

            if (index >= 0 &&
                index <
                static_cast<int>(
                    g_monitors.size()
                    ))
            {
                g_selectedMonitor =
                    g_monitors[index].handle;

                if (g_running &&
                    g_chatEnabled)
                {
                    UpdateOverlayPosition();
                }
            }

            return 0;
        }

        break;
    }


    // --------------------------------------------------------
    // 창 닫기
    // --------------------------------------------------------

    case WM_CLOSE:
    {
        StopProgram();

        g_exit = true;

        DestroyWindow(hwnd);

        return 0;
    }


    case WM_DESTROY:
    {
        g_exit = true;

        HideOverlay();

        PostQuitMessage(0);

        return 0;
    }
    }

    return DefWindowProcW(
        hwnd,
        msg,
        wParam,
        lParam
    );
}


// ============================================================
// WinMain
// ============================================================

int WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE,
    _In_ PWSTR,
    _In_ int showCommand)
{
    g_instance = instance;

    WNDCLASSEXW wc{};

    wc.cbSize =
        sizeof(WNDCLASSEXW);

    wc.lpfnWndProc =
        MainWindowProc;

    wc.hInstance =
        instance;

    wc.hIcon =
        LoadIconW(
            instance,
            MAKEINTRESOURCEW(IDI_ICON1)
        );

    wc.hIconSm =
        LoadIconW(
            instance,
            MAKEINTRESOURCEW(IDI_ICON1)
        );

    wc.lpszClassName =
        L"RobloxChatFixMain";

    wc.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW
        );

    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(
            COLOR_WINDOW + 1
            );

    RegisterClassExW(&wc);


    // --------------------------------------------------------
    // 메인 창
    //
    // WS_MAXIMIZEBOX 없음
    // WS_MINIMIZEBOX 있음
    // --------------------------------------------------------

    g_mainWindow =
        CreateWindowExW(
            0,
            wc.lpszClassName,
            L"RobloxChatFix",

            WS_OVERLAPPED |
            WS_CAPTION |
            WS_SYSMENU |
            WS_MINIMIZEBOX,

            CW_USEDEFAULT,
            CW_USEDEFAULT,

            380,
            240,

            nullptr,
            nullptr,
            instance,
            nullptr
        );

    if (!g_mainWindow)
        return 1;


    ShowWindow(
        g_mainWindow,
        showCommand
    );

    UpdateWindow(
        g_mainWindow
    );


    // --------------------------------------------------------
    // Threads
    // --------------------------------------------------------

    std::thread rawInputThread(
        RawInputThread
    );

    std::thread mouseThread(
        MouseThread
    );


    // --------------------------------------------------------
    // Message Loop
    // --------------------------------------------------------

    MSG msg{};

    while (GetMessageW(
        &msg,
        nullptr,
        0,
        0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }


    // --------------------------------------------------------
    // 종료
    // --------------------------------------------------------

    g_exit = true;
    g_running = false;
    g_chatEnabled = false;
    g_chatActive = false;
    g_enterPending = false;

    HideOverlay();


    if (rawInputThread.joinable())
    {
        PostThreadMessageW(
            GetThreadId(
                rawInputThread.native_handle()
            ),
            WM_QUIT,
            0,
            0
        );

        rawInputThread.join();
    }


    if (mouseThread.joinable())
    {
        mouseThread.join();
    }

    return 0;
}