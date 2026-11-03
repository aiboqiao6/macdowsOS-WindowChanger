#include "../adapter-api.h"
#include "../chrome.h"
// Small independent plugin demonstrating the public ABI. This example only
// accepts the research Win32 target; it is not registered in the default app.
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterProbeV1(const styler::AdapterContextV1 *c) {
    if (!styler::ValidContext(c))
        return ERROR_INVALID_PARAMETER;
    wchar_t cls[256]{};
    GetClassNameW(c->window, cls, 256);
    return wcscmp(cls, styler::TargetClass) == 0 && (GetWindowLongPtrW(c->window, GWL_STYLE) & WS_CAPTION) == WS_CAPTION
               ? ERROR_SUCCESS
               : ERROR_NOT_SUPPORTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI MacdowsAdapterAttachV1(const styler::AdapterContextV1 *c) {
    DWORD result = MacdowsAdapterProbeV1(c);
    if (result)
        return result;
    result = styler::chrome::Attach(c->window, c->owner, styler::Adapter::Native);
    if (!result)
        SetPropW(c->window, styler::AdapterProperty, reinterpret_cast<HANDLE>(c->adapterId));
    return result;
}
