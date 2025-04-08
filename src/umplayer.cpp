// compile as umplayer
// g++ ../src/umplayer.cpp -o umplayer --static -mwindows
// this was mostly generated with ChatGPT
// use this with the discord bot to auto play an episode (so taiga wouldn't search torrent again)
// mostly are hardcoded

#include <windows.h>
#include <string>

const wchar_t g_szClassName[] = L"QWidget";

// Step 4: The Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_CREATE:
            // Set a timer with ID 1 and 3000ms (3 seconds) interval
            SetTimer(hwnd, 1, 5000, NULL);
            break;
        case WM_TIMER:
            if (wParam == 1)
            {
                KillTimer(hwnd, 1); // Kill the timer
                PostMessage(hwnd, WM_CLOSE, 0, 0); // Close the window
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpCmdLine, int nCmdShow)
{
    LPWSTR *szArgList;
    int nArgs;
    szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArgList == 0) return 0;
    if (nArgs < 2) return 0;

    std::wstring filePath_ = std::wstring(szArgList[1]);
    std::wstring win_title = filePath_ + std::wstring(L" - UMPlayer");

    LocalFree(szArgList);

    WNDCLASSEXW wc;
    HWND hwnd;
    MSG Msg;

    // Step 1: Registering the Window Class
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = 0;
    wc.lpfnWndProc   = WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = g_szClassName;
    wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    if(!RegisterClassExW(&wc))
    {
        //MessageBox(NULL, "Window Registration Failed!", "Error!",
        //    MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Step 2: Creating the Window
    hwnd = CreateWindowExW(
        WS_EX_NOACTIVATE,
        g_szClassName,
        win_title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 240, 120,
        NULL, NULL, hInstance, NULL);

    if(hwnd == NULL)
    {
        //MessageBox(NULL, "Window Creation Failed!", "Error!",
        //    MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    ShowWindow(hwnd, SW_MINIMIZE);
    UpdateWindow(hwnd);

    // Step 3: The Message Loop
    while(GetMessage(&Msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
    return Msg.wParam;
}

