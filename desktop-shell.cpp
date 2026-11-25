// 回声宅 · 3D —— Windows 桌面版外壳（Win32 + WebView2，无浏览器地址栏）
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include "WebView2.h"

static HWND  g_hwnd = nullptr;
static ICoreWebView2Controller* g_ctrl = nullptr;
static ICoreWebView2*           g_web  = nullptr;

/* ---- 极简 COM 回调基类（不依赖 WRL，避免额外头文件） ---- */
template <class I>
class Handler : public I {
    LONG m_ref = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(I)) {
            *ppv = static_cast<I*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) { delete this; return 0; }
        return (ULONG)r;
    }
};

static std::wstring ExeDir() {
    wchar_t p[MAX_PATH * 4] = { 0 };
    GetModuleFileNameW(nullptr, p, (DWORD)(sizeof(p) / sizeof(p[0])));
    wchar_t* s = wcsrchr(p, L'\\');
    if (s) *s = 0;
    return p;
}

static std::wstring UserDataDir() {
    wchar_t p[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p)))
        return ExeDir() + L"\\webview2-data";
    std::wstring d = std::wstring(p) + L"\\EchoManor\\WebView2";
    // 递归创建
    std::wstring cur;
    for (size_t i = 0; i < d.size(); ++i) {
        cur += d[i];
        if (d[i] == L'\\' || i + 1 == d.size()) CreateDirectoryW(cur.c_str(), nullptr);
    }
    return d;
}

static void Fatal(const wchar_t* msg) {
    MessageBoxW(g_hwnd, msg, L"回声宅", MB_OK | MB_ICONERROR);
    PostQuitMessage(1);
}

/* ---- 页面加载失败提示 ---- */
class NavCB : public Handler<ICoreWebView2NavigationCompletedEventHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*,
        ICoreWebView2NavigationCompletedEventArgs* args) override {
        BOOL ok = TRUE;
        if (args) args->get_IsSuccess(&ok);
        if (!ok) {
            MessageBoxW(g_hwnd,
                L"页面加载失败。\n\n请确认程序旁边的 app\\index.html 没有被移动或删除。",
                L"回声宅", MB_OK | MB_ICONWARNING);
        }
        return S_OK;
    }
};

/* ---- WebView2 控制器就绪 ---- */
class CtrlCB : public Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller* c) override {
        if (FAILED(hr) || !c) { Fatal(L"WebView2 控制器创建失败。\n请确认系统已安装 Edge WebView2 运行时。"); return S_OK; }
        g_ctrl = c; c->AddRef();
        if (FAILED(c->get_CoreWebView2(&g_web)) || !g_web) { Fatal(L"WebView2 初始化失败。"); return S_OK; }

        ICoreWebView2Settings* st = nullptr;
        if (SUCCEEDED(g_web->get_Settings(&st)) && st) {
            st->put_AreDefaultContextMenusEnabled(FALSE);   // 右键菜单：关
            st->put_AreDevToolsEnabled(FALSE);              // F12 调试：关
            st->put_IsStatusBarEnabled(FALSE);              // 左下角链接提示：关
            st->put_IsZoomControlEnabled(FALSE);            // Ctrl+滚轮缩放：关（滚轮留给游戏变焦）
            st->Release();
        }
        EventRegistrationToken tok{};
        g_web->add_NavigationCompleted(new NavCB(), &tok);

        /* 用虚拟主机映射 app 目录：等价于 http 环境，ES 模块 / 字体 / 贴图都不受 file:// 限制 */
        bool navigated = false;
        ICoreWebView2_3* w3 = nullptr;
        if (SUCCEEDED(g_web->QueryInterface(__uuidof(ICoreWebView2_3), (void**)&w3)) && w3) {
            std::wstring dir = ExeDir() + L"\\app";
            if (SUCCEEDED(w3->SetVirtualHostNameToFolderMapping(L"echo.local", dir.c_str(),
                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))) {
                g_web->Navigate(L"https://echo.local/index.html");
                navigated = true;
            }
            w3->Release();
        }
        if (!navigated) {                                   // 兜底：直接走 file://
            std::wstring url = L"file:///" + ExeDir() + L"/app/index.html";
            for (auto& ch : url) if (ch == L'\\') ch = L'/';
            g_web->Navigate(url.c_str());
        }

        RECT rc; GetClientRect(g_hwnd, &rc);
        c->put_Bounds(rc);
        c->put_IsVisible(TRUE);
        return S_OK;
    }
};

/* ---- 环境创建完成 ---- */
class EnvCB : public Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
public:
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment* env) override {
        if (FAILED(hr) || !env) {
            Fatal(L"WebView2 环境创建失败。\n\n请安装 “Microsoft Edge WebView2 运行时” 后重试。");
            return S_OK;
        }
        env->CreateCoreWebView2Controller(g_hwnd, new CtrlCB());
        return S_OK;
    }
};

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE:
        if (g_ctrl) { RECT rc; GetClientRect(h, &rc); g_ctrl->put_Bounds(rc); }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize.x = 900;
        mm->ptMinTrackSize.y = 600;
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    typedef BOOL (WINAPI* PFN_SetCtx)(HANDLE);
    if (HMODULE u = GetModuleHandleW(L"user32.dll"))
        if (auto f = (PFN_SetCtx)GetProcAddress(u, "SetProcessDpiAwarenessContext"))
            f((HANDLE)-4);                                  // PER_MONITOR_AWARE_V2

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"EchoManorWindow";
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    RegisterClassExW(&wc);

    RECT r{ 0, 0, 1280, 800 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"回声宅 · 3D", WS_OVERLAPPEDWINDOW,
                             x, y, w, h, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    std::wstring data = UserDataDir();
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, data.c_str(), nullptr, new EnvCB());
    if (FAILED(hr)) Fatal(L"无法启动 WebView2。请安装 “Microsoft Edge WebView2 运行时”。");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }

    if (g_ctrl) g_ctrl->Release();
    if (g_web)  g_web->Release();
    return 0;
}
