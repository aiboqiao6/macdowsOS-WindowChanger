#include <windows.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <oleacc.h>
#include <wrl/client.h>
#include <CoreWindow.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Automation.h>

UINT Message(){return RegisterWindowMessageW(L"Macdows.Research.ReadOnlyTree.v11");}
#ifdef TREE_PROBE_DLL
namespace x=winrt::Windows::UI::Xaml;
std::wstring Directory(){HMODULE m{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Directory),&m);wchar_t p[32768]{};GetModuleFileNameW(m,p,32768);std::wstring s(p);return s.substr(0,s.find_last_of(L'\\')+1);}
void Trace(std::ofstream& log,x::DependencyObject const& node,x::UIElement const& root,int depth=0){
 if(!node||depth>16)return;std::ostringstream line;line<<std::string(depth,' ')<<winrt::to_string(winrt::get_class_name(node));
 if(auto e=node.try_as<x::FrameworkElement>()){
  auto p=e.TransformToVisual(root).TransformPoint({0,0});
  if(p.Y>70)return;
  line<<" name="<<winrt::to_string(e.Name())<<" automation="<<winrt::to_string(x::Automation::AutomationProperties::GetName(e))<<" xy="<<p.X<<","<<p.Y<<" size="<<e.ActualWidth()<<","<<e.ActualHeight()<<" visible="<<(int)e.Visibility();
 }
 log<<line.str()<<"\n";
 int count=x::Media::VisualTreeHelper::GetChildrenCount(node);
 for(int i=0;i<count;i++)Trace(log,x::Media::VisualTreeHelper::GetChild(node,i),root,depth+1);
}
BOOL CALLBACK Prop(HWND,LPWSTR key,HANDLE value,ULONG_PTR ctx){
 auto& log=*reinterpret_cast<std::ofstream*>(ctx);log<<"prop ";if(IS_INTRESOURCE(key))log<<"atom:"<<reinterpret_cast<ULONG_PTR>(key);else log<<winrt::to_string(key);log<<"="<<value<<"\n";return TRUE;
}
void Read(HWND target){
 std::ofstream log(Directory()+L"tree-"+std::to_wstring(GetCurrentProcessId())+L".txt");
 log<<"pid="<<GetCurrentProcessId()<<" thread="<<GetCurrentThreadId()<<" target="<<target<<" userdata="<<(void*)GetWindowLongPtrW(target,GWLP_USERDATA)<<"\n";
 EnumPropsExW(target,Prop,reinterpret_cast<LPARAM>(&log));
 try{
  auto window=x::Window::Current();log<<"Window::Current="<<winrt::get_abi(window)<<"\n";
  if(window){HWND core{};auto hr=window.CoreWindow().as<ICoreWindowInterop>()->get_WindowHandle(&core);log<<"core="<<core<<" hr="<<std::hex<<hr<<std::dec<<"\n";if(auto root=window.Content())Trace(log,root,root);}
 }catch(...){log<<"xaml HRESULT="<<std::hex<<winrt::to_hresult()<<"\n";}
 Microsoft::WRL::ComPtr<IAccessible> accessible;
 HRESULT hr=AccessibleObjectFromWindow(target,OBJID_CLIENT,IID_PPV_ARGS(&accessible));
 log<<"accessible="<<accessible.Get()<<" hr="<<std::hex<<hr;
 if(accessible){void* vtable=*reinterpret_cast<void***>(accessible.Get());HMODULE m{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(vtable),&m);wchar_t path[32768]{};GetModuleFileNameW(m,path,32768);log<<" module="<<winrt::to_string(path)<<" vtableRva="<<((uintptr_t)vtable-(uintptr_t)m);}
 log<<"\n";
}
extern "C" __declspec(dllexport) LRESULT CALLBACK ReadOnlyTreeHook(int code,WPARAM w,LPARAM l){
 if(code>=0&&l){auto event=reinterpret_cast<CWPSTRUCT*>(l);if(event->message==Message())Read(event->hwnd);}
 return CallNextHookEx(nullptr,code,w,l);
}
#else
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;HWND target=reinterpret_cast<HWND>(wcstoull(argv[1],nullptr,0));
 if(!IsWindow(target))return 3;
 auto module=LoadLibraryW(L"XamlTreeProbe.dll");if(!module)return GetLastError();
 auto proc=reinterpret_cast<HOOKPROC>(GetProcAddress(module,"ReadOnlyTreeHook"));
 auto hook=SetWindowsHookExW(WH_CALLWNDPROC,proc,module,GetWindowThreadProcessId(target,nullptr));
 if(!hook)return GetLastError();DWORD_PTR result{};
 auto sent=SendMessageTimeoutW(target,Message(),0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,5000,&result);
 UnhookWindowsHookEx(hook);FreeLibrary(module);return sent?0:4;
}
#endif
