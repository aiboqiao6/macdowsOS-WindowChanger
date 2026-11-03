// Isolated experiment. Both the loader and this DLL require our fixture marker.
// This is deliberately not registered as a production adapter.
#include <windows.h>
#include <commctrl.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cmath>
#include <memory>
#include <algorithm>
#include "../../pe-identity.h"
namespace x=winrt::Microsoft::UI::Xaml;
namespace c=x::Controls;
namespace w=winrt::Microsoft::UI::Windowing;
namespace input=winrt::Microsoft::UI::Input;
using winrt::Windows::Graphics::RectInt32;
constexpr UINT_PTR SubclassId=0x4d575833;
UINT Message(){return RegisterWindowMessageW(L"Macdows.Research.WinUI3FixtureInline.v14");}
std::wstring Directory(){HMODULE m{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Directory),&m);wchar_t p[32768]{};GetModuleFileNameW(m,p,32768);std::wstring s(p);return s.substr(0,s.find_last_of(L'\\')+1);}
void Log(std::string const& line){std::ofstream f(Directory()+L"inline-"+std::to_wstring(GetCurrentProcessId())+L".log",std::ios::app);f<<line<<"\n";}
struct State{
 HWND hwnd{};x::UIElement root{nullptr};c::Grid title{nullptr},lights{nullptr};w::AppWindow window{nullptr};
 w::OverlappedPresenter presenter{nullptr};input::InputNonClientPointerSource pointer{nullptr};
 bool border{},caption{},changing{},event{};x::Thickness padding{};winrt::event_token layout{};
 std::vector<RectInt32> passthrough;
};
thread_local State* active{};
LRESULT CALLBACK Subclass(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
void Inspect(x::DependencyObject const& n,x::UIElement const& root,std::ofstream& f,int depth=0){
 if(!n||depth>14)return;f<<std::string(depth,' ')<<winrt::to_string(winrt::get_class_name(n));
 if(auto e=n.try_as<x::FrameworkElement>()){auto p=e.TransformToVisual(root).TransformPoint({0,0});f<<" xy="<<p.X<<","<<p.Y<<" size="<<e.ActualWidth()<<","<<e.ActualHeight();if(p.Y>80){f<<"\n";return;}}
 f<<"\n";for(int i=0;i<x::Media::VisualTreeHelper::GetChildrenCount(n);i++)Inspect(x::Media::VisualTreeHelper::GetChild(n,i),root,f,depth+1);
}
std::vector<x::Hosting::DesktopWindowXamlSource> Roots(){
 auto module=GetModuleHandleW(L"Microsoft.UI.Xaml.dll");
 constexpr GUID id{0x4acdbe85,0xd668,0x192e,{0xb8,0xb6,0xa9,0x33,0x4a,0xc9,0x1b,0x79}};
 if(!styler::MatchesPdb(module,id))return {};
 const BYTE bytes[]{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x56,0x48,0x8b,0xec,0x48,0x83,0xec,0x40};
 auto address=reinterpret_cast<BYTE*>(module)+0x731804;if(memcmp(address,bytes,sizeof(bytes)))return {};
 std::vector<Microsoft::WRL::ComPtr<::IInspectable>> values;
 winrt::check_hresult(reinterpret_cast<HRESULT(*)(decltype(values)&)>(address)(values));
 std::vector<x::Hosting::DesktopWindowXamlSource> result;
 for(auto const& value:values){winrt::Windows::Foundation::IInspectable object{nullptr};winrt::copy_from_abi(object,value.Get());if(auto source=object.try_as<x::Hosting::DesktopWindowXamlSource>())result.push_back(source);}
 return result;
}
void Candidates(x::DependencyObject const& n,x::UIElement const& root,std::vector<c::Grid>& list,int depth=0){
 if(!n||depth>10)return;
 if(auto grid=n.try_as<c::Grid>();grid&&grid.IsLoaded()&&grid.Visibility()==x::Visibility::Visible){
  auto p=grid.TransformToVisual(root).TransformPoint({0,0});auto body=root.as<x::FrameworkElement>();
  if(std::abs(p.X)<2&&std::abs(p.Y)<2&&grid.ActualHeight()>=24&&grid.ActualHeight()<=64&&grid.ActualWidth()>body.ActualWidth()*.70&&grid.Children().Size()>0){list.push_back(grid);return;}
 }
 for(int i=0;i<x::Media::VisualTreeHelper::GetChildrenCount(n);i++)Candidates(x::Media::VisualTreeHelper::GetChild(n,i),root,list,depth+1);
}
void Regions(State* s){
 if(s->changing)return;s->changing=true;
 try{
  auto point=s->lights.TransformToVisual(s->root).TransformPoint({0,0});double scale=s->root.XamlRoot().RasterizationScale();
  auto rects=s->passthrough;rects.push_back({int(std::lround(point.X*scale)),int(std::lround(point.Y*scale)),int(std::lround(s->lights.ActualWidth()*scale)),int(std::lround(s->lights.ActualHeight()*scale))});
  s->pointer.SetRegionRects(input::NonClientRegionKind::Passthrough,rects);
 }catch(...){Log("region exception "+std::to_string(winrt::to_hresult()));}s->changing=false;
}
void Detach(){
 auto s=active;if(!s)return;active=nullptr;RemoveWindowSubclass(s->hwnd,Subclass,SubclassId);
 try{
  if(s->event)s->title.LayoutUpdated(s->layout);
  uint32_t index{};if(s->title.Children().IndexOf(s->lights,index))s->title.Children().RemoveAt(index);
  s->title.Padding(s->padding);s->pointer.SetRegionRects(input::NonClientRegionKind::Passthrough,s->passthrough);
  s->presenter.SetBorderAndTitleBar(s->border,s->caption);s->root.UpdateLayout();Log("detached");
 }catch(...){Log("detach exception "+std::to_string(winrt::to_hresult()));}
 delete s;
}
LRESULT CALLBACK Subclass(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
 if(m==WM_NCDESTROY){RemoveWindowSubclass(h,Subclass,SubclassId);Detach();}
 return DefSubclassProc(h,m,wp,lp);
}
DWORD Apply(HWND h){
 if(active)return ERROR_ALREADY_EXISTS;
 auto s=std::make_unique<State>();s->hwnd=h;
 for(auto source:Roots())if(auto root=source.Content()){
  auto id=root.XamlRoot().ContentIslandEnvironment().AppWindowId();auto window=w::AppWindow::GetFromWindowId(id);
  if(!window)continue;auto presenter=window.Presenter().try_as<w::OverlappedPresenter>();if(!presenter||!window.TitleBar().ExtendsContentIntoTitleBar())continue;
  std::vector<c::Grid> candidates;Candidates(root,root,candidates);if(candidates.size()!=1)continue;
  s->title=candidates[0];s->root=root;s->window=window;s->presenter=presenter;s->pointer=input::InputNonClientPointerSource::GetForWindowId(id);break;
 }
 if(!s->title)return ERROR_NOT_SUPPORTED;
 s->padding=s->title.Padding();s->border=s->presenter.HasBorder();s->caption=s->presenter.HasTitleBar();
 for(auto r:s->pointer.GetRegionRects(input::NonClientRegionKind::Passthrough))s->passthrough.push_back(r);
 active=s.release();auto state=active;
 try{
  state->presenter.SetBorderAndTitleBar(state->border,false);
  auto padding=state->padding;padding.Left+=84;state->title.Padding(padding);
  state->lights=c::Grid();state->lights.Width(84);state->lights.HorizontalAlignment(x::HorizontalAlignment::Left);
  x::Media::TranslateTransform shift;shift.X(-padding.Left);state->lights.RenderTransform(shift);
  state->lights.Background(x::Media::SolidColorBrush({0,0,0,0}));c::Grid::SetColumnSpan(state->lights,std::max(1u,state->title.ColumnDefinitions().Size()));
  auto buttonTemplate=x::Markup::XamlReader::Load(L"<ControlTemplate xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" TargetType=\"Button\"><ContentPresenter Content=\"{TemplateBinding Content}\" HorizontalContentAlignment=\"Center\" VerticalContentAlignment=\"Center\" Background=\"Transparent\"/></ControlTemplate>").as<c::ControlTemplate>();
  const winrt::Windows::UI::Color colors[]{{255,255,95,87},{255,255,189,46},{255,40,200,64}};const wchar_t* names[]{L"Close window",L"Minimize window",L"Maximize or restore window"};
  for(int i=0;i<3;i++){
   c::Button button;button.Template(buttonTemplate);button.Width(20);button.Height(24);button.HorizontalAlignment(x::HorizontalAlignment::Left);button.VerticalAlignment(x::VerticalAlignment::Center);button.Margin({double(8+i*20),0,0,0});
   x::Shapes::Ellipse dot;dot.Width(12);dot.Height(12);dot.Fill(x::Media::SolidColorBrush(colors[i]));button.Content(dot);x::Automation::AutomationProperties::SetName(button,names[i]);
   button.Click([h,i](auto const&,auto const&){PostMessageW(h,WM_SYSCOMMAND,i==0?SC_CLOSE:i==1?SC_MINIMIZE:IsZoomed(h)?SC_RESTORE:SC_MAXIMIZE,0);});state->lights.Children().Append(button);
  }
  state->title.Children().Append(state->lights);state->root.UpdateLayout();Regions(state);
  state->layout=state->title.LayoutUpdated([state](auto const&,auto const&){Regions(state);});state->event=true;
  if(!SetWindowSubclass(h,Subclass,SubclassId,0))winrt::throw_last_error();Log("attached: original row, presenter API, three XAML buttons");return 0;
 }catch(...){auto hr=winrt::to_hresult();Log("apply exception "+std::to_string(hr));Detach();return DWORD(hr);}
}
LRESULT CALLBACK Hook(int code,WPARAM wp,LPARAM lp){
 if(code>=0&&lp){auto e=reinterpret_cast<CWPSTRUCT*>(lp);if(e->message==Message()&&GetPropW(e->hwnd,L"Macdows.WindowStyler.Sample")){
  DWORD result{};try{
   if(e->wParam==1)result=Apply(e->hwnd);else if(e->wParam==2)Detach();
   std::ofstream f(Directory()+L"inline-tree-"+std::to_wstring(GetCurrentProcessId())+L".txt");
   f<<"command="<<e->wParam<<" result="<<result<<" active="<<(active!=nullptr)<<"\n";
   for(auto source:Roots())if(auto root=source.Content()){auto window=w::AppWindow::GetFromWindowId(root.XamlRoot().ContentIslandEnvironment().AppWindowId());auto p=window.Presenter().try_as<w::OverlappedPresenter>();f<<"appWindow="<<window.Id().Value<<" border="<<(p?p.HasBorder():false)<<" caption="<<(p?p.HasTitleBar():false)<<" extend="<<window.TitleBar().ExtendsContentIntoTitleBar()<<"\n";Inspect(root,root,f);}
  }catch(...){result=DWORD(winrt::to_hresult());Log("hook exception "+std::to_string(result));}
  SetPropW(e->hwnd,L"Macdows.Research.Result",reinterpret_cast<HANDLE>(UINT_PTR(result)+1));
 }}return CallNextHookEx(nullptr,code,wp,lp);
}
DWORD Start(DWORD command){
 HWND target{};EnumWindows([](HWND h,LPARAM p)->BOOL{DWORD pid{};GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId()&&GetPropW(h,L"Macdows.WindowStyler.Sample")){*reinterpret_cast<HWND*>(p)=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&target));
 if(!target)return ERROR_NOT_SUPPORTED;HMODULE self{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&self);
 auto hook=SetWindowsHookExW(WH_CALLWNDPROC,Hook,nullptr,GetWindowThreadProcessId(target,nullptr));if(!hook)return GetLastError();DWORD_PTR reply{};
 auto ok=SendMessageTimeoutW(target,Message(),command,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&reply);UnhookWindowsHookEx(hook);
 if(!ok)return ERROR_TIMEOUT;return DWORD(reinterpret_cast<UINT_PTR>(GetPropW(target,L"Macdows.Research.Result"))-1);
}
extern "C" __declspec(dllexport) DWORD WINAPI StartReadOnly(void*){return Start(0);}
extern "C" __declspec(dllexport) DWORD WINAPI StartExperiment(void* command){return command?Start(*reinterpret_cast<DWORD*>(command)):ERROR_INVALID_PARAMETER;}
