#include <windows.h>
// Retired: Windows.UI.Xaml diagnostics against Windows App SDK 2 caused target
// failures (Photos / Notepad, 2026-11-01). Keep the old export as a disabled stub
// so archived loader commands cannot invoke the incompatible diagnostics path.
extern "C" __declspec(dllexport) DWORD WINAPI StartReadOnly(void*) {
    return ERROR_NOT_SUPPORTED;
}
STDAPI DllGetClassObject(REFCLSID,REFIID,void** result) { if(result)*result=nullptr; return CLASS_E_CLASSNOTAVAILABLE; }
