// UAS keyboard command prototype for Windows.
// Start reading in keyboard_logic.hpp: that file contains the control logic.
// This file only connects Windows key events to that logic and displays results.
// It does not connect to a flight controller or send motor/arming commands.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <iomanip>
#include <sstream>
#include <string>

#include "keyboard_logic.hpp"

// These libraries are included with the Windows SDK used by Visual Studio.
// The pragmas also allow this source to be added to a normal Console project.
#pragma comment(lib, "user32.lib")

namespace
{
    // One AppState lives for the entire time the window is open.
    // Its Keys object remembers simultaneous keys between Windows messages.
    struct AppState
    {
        uas::Keys keys;
        bool focused = false;
        HWND command_label = nullptr;
        HWND focus_label = nullptr;
    };

    // Format the current COMMANDS for display. No sensor values are available.
    void refresh_display(const AppState & app)
    {
        const uas::PyrCommand command = uas::make_command(app.keys, app.focused);
        std::wostringstream text;
        text << std::fixed << std::setprecision(1) << std::showpos
             << L"Roll target:       " << command.roll_deg << L" deg\r\n\r\n"
             << L"Pitch target:      " << command.pitch_deg << L" deg\r\n\r\n"
             << L"Yaw rate target:   " << command.yaw_rate_deg_s << L" deg/s";
        SetWindowTextW(app.command_label, text.str().c_str());
        SetWindowTextW(app.focus_label, app.focused
            ? L"ACTIVE - hold control keys; release them to return to zero."
            : L"INACTIVE - all commands are zero. Click this window to use it.");
    }

    void clear_input(AppState & app)
    {
        uas::release_all(app.keys);
        refresh_display(app);
    }

    HWND add_label(HWND window, const wchar_t * text, int x, int y,
                   int width, int height)
    {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            x, y, width, height, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    // Windows calls this function whenever something happens to our window.
    // message identifies the event; wparam/lparam contain the event's details.
    LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        // GWLP_USERDATA stores the address of our AppState for this window.
        auto * app = reinterpret_cast<AppState *>(GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto * creation = reinterpret_cast<const CREATESTRUCTW *>(lparam);
            app = static_cast<AppState *>(creation->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        if (app == nullptr)
        {
            return DefWindowProcW(window, message, wparam, lparam);
        }

        switch (message)
        {
        case WM_CREATE:
            if (add_label(window, L"UAS - Keyboard Pitch / Roll / Yaw", 24, 20, 550, 26) == nullptr ||
                add_label(window,
                    L"W / S : pitch nose down / nose up\r\n"
                    L"A / D : roll left / right\r\n"
                    L"Q / E : yaw left / right\r\n\r\n"
                    L"Combine keys to command several axes. Opposite keys cancel.\r\n"
                    L"ESC closes the program.", 24, 62, 550, 122) == nullptr)
            {
                return -1;
            }
            app->command_label = add_label(window, L"", 24, 204, 550, 110);
            app->focus_label = add_label(window, L"", 24, 334, 550, 44);
            if (app->command_label == nullptr || app->focus_label == nullptr ||
                add_label(window,
                    L"COMMAND PREVIEW ONLY - no aircraft is connected.\r\n"
                    L"No throttle, arming, PID, motor output or telemetry is implemented.",
                    24, 394, 550, 52) == nullptr)
            {
                return -1;
            }
            refresh_display(*app);
            return 0;

        case WM_SETFOCUS:
            app->focused = true;
            clear_input(*app); // Do not reuse a key state from an earlier focus period.
            return 0;

        case WM_KILLFOCUS:
            // A key may be released in another app, so our window might never
            // receive its WM_KEYUP message. Clear every key immediately.
            app->focused = false;
            clear_input(*app);
            return 0;

        case WM_KEYDOWN:
        {
            if (wparam == VK_ESCAPE)
            {
                DestroyWindow(window);
                return 0;
            }

            // Bit 30 is 1 for an automatic repeat, 0 for a fresh key press.
            // Keeping a bool true already represents holding a key down.
            // Ignoring repeats also prevents a held key from reactivating when
            // the user returns from another app without first releasing it.
            const bool repeated = (lparam & (static_cast<LPARAM>(1) << 30)) != 0;
            if (app->focused && !repeated &&
                uas::set_key(app->keys, static_cast<unsigned>(wparam), true))
            {
                refresh_display(*app);
            }
            return 0;
        }

        case WM_KEYUP:
            if (uas::set_key(app->keys, static_cast<unsigned>(wparam), false))
            {
                refresh_display(*app);
            }
            return 0;

        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            // Clear commands while Alt/menu shortcuts are being used.
            // DefWindowProc still handles the shortcut, including Alt+F4.
            clear_input(*app);
            break;

        case WM_ENTERSIZEMOVE:
        case WM_ENTERMENULOOP:
            clear_input(*app);
            break;

        case WM_CLOSE:
            app->focused = false;
            clear_input(*app);
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            uas::release_all(app->keys);
            PostQuitMessage(0); // This tells the message loop below to finish.
            return 0;
        }

        return DefWindowProcW(window, message, wparam, lparam);
    }
}

int main()
{
    // main() works in the Windows Console project already present in UAS.
    // A separate control window opens; click that window before using keys.
    AppState app;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr wchar_t class_name[] = L"UasPyrKeyboardWindow";

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);

    if (RegisterClassW(&window_class) == 0)
    {
        MessageBoxW(nullptr, L"Could not register the keyboard window.", L"UAS", MB_ICONERROR);
        return 1;
    }

    // Use a fixed client area so every instruction remains visible.
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT bounds{0, 0, 600, 466};
    if (!AdjustWindowRect(&bounds, style, FALSE))
    {
        return 1;
    }
    const HWND window = CreateWindowExW(0, class_name, L"UAS Keyboard Commands",
        style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
        bounds.bottom - bounds.top, nullptr, nullptr, instance, &app);

    if (window == nullptr)
    {
        MessageBoxW(nullptr, L"Could not create the keyboard window.", L"UAS", MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    // Wait for events instead of repeatedly polling in a busy loop.
    // GetMessage returns >0 for an event, 0 to quit, or -1 for an error.
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return result == -1 ? 1 : 0;
}
