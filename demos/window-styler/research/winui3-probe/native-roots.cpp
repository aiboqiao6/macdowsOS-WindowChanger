#include <windows.h>
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Content.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include "../../pe-identity.h"
namespace x=winrt::Microsoft::UI::Xaml;
UINT Message(){return RegisterWindowMessageW(L"Macdows.Research.WinUI3FixtureRead.v13");}
std::wstring Directory(){HMODULE m{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Directory),&m);wchar_t p[32768]{};GetModuleFileNameW(m,p,32768);std::wstring s(p);return s.substr(0,s.find_last_of(L'\\')+1);}
void Trace(std::ofstream& log,x::DependencyObject const& node,x::UIElement const& root,int depth=0){
 if(!node||depth>18)return;log<<std::string(depth,' ')<<winrt::to_string(winrt::get_class_name(node));
 if(auto e=node.try_as<x::FrameworkElement>()){auto p=e.TransformToVisual(root).TransformPoint({0,0});log<<" name="<<winrt::to_string(e.Name())<<" xy="<<p.X<<","<<p.Y<<" size="<<e.ActualWidth()<<","<<e.ActualHeight()<<" visible="<<(int)e.Visibility();if(p.Y>70){log<<"\n";return;}}
 log<<"\n";int n=x::Media::VisualTreeHelper::GetChildrenCount(node);for(int i=0;i<n;i++)Trace(log,x::Media::VisualTreeHelper::GetChild(node,i),root,depth+1);
}
LRESULT CALLBACK Hook(int code,WPARAM w,LPARAM l){
 if(code>=0&&l){auto event=reinterpret_cast<CWPSTRUCT*>(l);if(event->message==Message()&&GetPropW(event->hwnd,L"Macdows.WindowStyler.Sample")){
  std::ofstream log(Directory()+L"fixture-roots-"+std::to_wstring(GetCurrentProcessId())+L".txt");
  try{auto m=GetModuleHandleW(L"Microsoft.UI.Xaml.dll");constexpr GUID id{0x4acdbe85,0xd668,0x192e,{0xb8,0xb6,0xa9,0x33,0x4a,0xc9,0x1b,0x79}};if(!styler::MatchesPdb(m,id))return CallNextHookEx(nullptr,code,w,l);
   auto base=reinterpret_cast<BYTE*>(m);auto current=reinterpret_cast<void*(*)()>(base+0x3a24b8);log<<"current="<<current()<<"\n";
   std::vector<Microsoft::WRL::ComPtr<::IInspectable>> objects;auto get=reinterpret_cast<HRESULT(*)(decltype(objects)&)>(base+0x731804);auto hr=get(objects);log<<"hr="<<std::hex<<hr<<std::dec<<" count="<<objects.size()<<"\n";
   for(auto const& object:objects){winrt::Windows::Foundation::IInspectable i{nullptr};winrt::copy_from_abi(i,object.Get());log<<"object="<<winrt::to_string(winrt::get_class_name(i))<<"\n";
    if(auto source=i.try_as<x::Hosting::DesktopWindowXamlSource>())if(auto content=source.Content()){log<<"content="<<winrt::to_string(winrt::get_class_name(content))<<"\n";Trace(log,content,content);}
    if(auto element=i.try_as<x::FrameworkElement>())Trace(log,element,element);
   }
  }catch(...){log<<"hr="<<std::hex<<winrt::to_hresult();}
 }}return CallNextHookEx(nullptr,code,w,l);
}
extern "C" __declspec(dllexport) DWORD WINAPI StartReadOnly(void*){
 HWND target{};EnumWindows([](HWND h,LPARAM p)->BOOL{DWORD pid{};GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId()&&GetPropW(h,L"Macdows.WindowStyler.Sample")){*reinterpret_cast<HWND*>(p)=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&target));
 if(!target)return ERROR_NOT_SUPPORTED;HMODULE self{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&StartReadOnly),&self);auto hook=SetWindowsHookExW(WH_CALLWNDPROC,Hook,nullptr,GetWindowThreadProcessId(target,nullptr));if(!hook)return GetLastError();DWORD_PTR result{};auto ok=SendMessageTimeoutW(target,Message(),0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result);UnhookWindowsHookEx(hook);return ok?0:ERROR_TIMEOUT;
}
