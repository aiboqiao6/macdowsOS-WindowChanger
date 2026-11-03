#include <windows.h>
static LRESULT CALLBACK Forward(int code,WPARAM w,LPARAM l){return CallNextHookEx(nullptr,code,w,l);}
// Research only: register/unregister from the target process without touching
// any HWND, styles, layout, input, or production adapter state.
extern "C" __declspec(dllexport) DWORD WINAPI ProbeThreadHook(void* thread){
    HHOOK hook=SetWindowsHookExW(WH_CALLWNDPROC,Forward,nullptr,static_cast<DWORD>(reinterpret_cast<UINT_PTR>(thread)));
    if(!hook)return GetLastError();
    return UnhookWindowsHookEx(hook)?ERROR_SUCCESS:GetLastError();
}
