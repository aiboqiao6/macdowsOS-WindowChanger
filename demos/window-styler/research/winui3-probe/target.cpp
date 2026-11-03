#include <windows.h>
#include <appmodel.h>
#include <unknwn.h>
#include <commctrl.h>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <fstream>
namespace x=winrt::Microsoft::UI::Xaml;
namespace c=x::Controls;
struct __declspec(uuid("eecdbf0e-bae9-4cb6-a68e-9598e1cb57bb")) IWindowNative: IUnknown {virtual HRESULT __stdcall get_WindowHandle(HWND*)=0;};
struct App:x::ApplicationT<App>{
 x::Window window{nullptr};
 c::Grid root{nullptr},title{nullptr};
 c::Border paddingSource{nullptr};
 void Title(bool alternate){
  if(title){window.SetTitleBar(nullptr);uint32_t index{};if(root.Children().IndexOf(title,index))root.Children().RemoveAt(index);}
  title=c::Grid();title.Background(x::Media::SolidColorBrush(alternate?winrt::Windows::UI::Color{255,55,45,65}:winrt::Windows::UI::Color{255,48,50,56}));title.Margin({0,0,144,0});
  if(wcsstr(GetCommandLineW(),L"--binding")){paddingSource=c::Border();x::Data::Binding binding;binding.Source(paddingSource);binding.Path(x::PropertyPath(L"Padding"));x::Data::BindingOperations::SetBinding(title,c::Grid::PaddingProperty(),binding);}
  if(alternate){c::ColumnDefinition first;first.Width({1,x::GridUnitType::Star});title.ColumnDefinitions().Append(first);c::ColumnDefinition second;second.Width({200,x::GridUnitType::Pixel});title.ColumnDefinitions().Append(second);}
  c::TextBlock search;search.Text(alternate?L"Rebuilt original title content":L"Original title content");search.Margin({12,4,12,4});
  if(wcsstr(GetCommandLineW(),L"--bound-column")){c::Border source;source.Tag(winrt::box_value(0));x::Data::Binding binding;binding.Source(source);binding.Path(x::PropertyPath(L"Tag"));x::Data::BindingOperations::SetBinding(search,c::Grid::ColumnProperty(),binding);}
  title.Children().Append(search);root.Children().InsertAt(0,title);if(window.Content())window.SetTitleBar(title);
 }
 static LRESULT CALLBACK TestMessages(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
  auto self=reinterpret_cast<App*>(data);
  if(m==WM_APP+40){
   try{
    if(wp==1)self->Title(true);
    if(wp==2){if(self->paddingSource)self->paddingSource.Padding({20,2,6,2});else self->title.Padding({20,2,6,2});}
    if(wp==3)self->root.RequestedTheme(x::ElementTheme::Light);
    if(wp==4)self->root.RequestedTheme(x::ElementTheme::Dark);
    if(wp==5){auto p=self->window.AppWindow().Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>();p.SetBorderAndTitleBar(true,true);}
    if(wp==6){self->root.RowDefinitions().GetAt(0).Height({32,x::GridUnitType::Pixel});}
    if(wp==7){if(self->paddingSource)self->paddingSource.Padding({40,2,6,2});else self->title.Padding({40,2,6,2});}
    if(wp==8)for(auto child:self->title.Children())if(auto text=child.try_as<c::TextBlock>())c::Grid::SetColumn(text,0);
    self->root.UpdateLayout();
   }catch(...){return winrt::to_hresult();}return 0;
  }
  return DefSubclassProc(h,m,wp,lp);
 }
 App(){UnhandledException([](auto const&,x::UnhandledExceptionEventArgs const& e){wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);std::wstring path(file);path.resize(path.find_last_of(L'\\')+1);std::ofstream log(path+L"fixture-unhandled.txt");log<<std::hex<<e.Exception().value<<" "<<winrt::to_string(e.Message());});}
 void OnLaunched(x::LaunchActivatedEventArgs const&){
  Resources().Insert(winrt::box_value(L"AcrylicBackgroundFillColorDefaultBrush"),x::Media::SolidColorBrush({255,32,34,38}));
  window=x::Window();window.Title(L"Ordinary WinUI 3 fixture");window.ExtendsContentIntoTitleBar(true);
  root=c::Grid();root.Background(x::Media::SolidColorBrush({255,32,34,38}));
  c::RowDefinition top;top.Height({48,x::GridUnitType::Pixel});root.RowDefinitions().Append(top);root.RowDefinitions().Append(c::RowDefinition());
  if(wcsstr(GetCommandLineW(),L"--no-title-row"))top.Height({120,x::GridUnitType::Pixel});
  Title(wcsstr(GetCommandLineW(),L"--columns")!=nullptr);
  c::TextBlock body;body.Text(L"WinUI 3 original layout fixture");body.FontSize(26);body.Foreground(x::Media::SolidColorBrush({255,230,230,235}));body.HorizontalAlignment(x::HorizontalAlignment::Center);body.VerticalAlignment(x::VerticalAlignment::Center);c::Grid::SetRow(body,1);root.Children().Append(body);
  window.Content(root);window.SetTitleBar(title);HWND h{};winrt::check_hresult(window.as<IWindowNative>()->get_WindowHandle(&h));if(!wcsstr(GetCommandLineW(),L"--unmarked"))SetPropW(h,L"Macdows.WindowStyler.Sample",reinterpret_cast<HANDLE>(1));
  SetWindowSubclass(h,TestMessages,0x4d545354,reinterpret_cast<DWORD_PTR>(this));
  window.Closed([](auto const&,auto const&){x::Application::Current().Exit();});window.Activate();
 }
};
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 try{
  SetProcessDpiAwarenessContext(wcsstr(GetCommandLineW(),L"--dpi-unaware")?DPI_AWARENESS_CONTEXT_UNAWARE:DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  PACKAGE_VERSION version{};version.Major=2;PWSTR id{};PACKAGEDEPENDENCY_CONTEXT context{};
  auto kernel=GetModuleHandleW(L"kernelbase.dll");auto create=reinterpret_cast<decltype(&TryCreatePackageDependency)>(GetProcAddress(kernel,"TryCreatePackageDependency"));auto add=reinterpret_cast<decltype(&AddPackageDependency)>(GetProcAddress(kernel,"AddPackageDependency"));if(!create||!add)winrt::throw_hresult(E_NOTIMPL);
  winrt::check_hresult(create(nullptr,L"Microsoft.WindowsAppRuntime.2_8wekyb3d8bbwe",version,PackageDependencyProcessorArchitectures_X64,PackageDependencyLifetimeKind_Process,nullptr,CreatePackageDependencyOptions_None,&id));
  winrt::check_hresult(add(id,0,AddPackageDependencyOptions_None,&context,nullptr));HeapFree(GetProcessHeap(),0,id);
  winrt::init_apartment(winrt::apartment_type::single_threaded);x::Application::Start([](auto const&){winrt::make<App>();});return 0;
 }catch(winrt::hresult_error const& e){wchar_t file[32768]{};GetModuleFileNameW(nullptr,file,32768);std::wstring path(file);path.resize(path.find_last_of(L'\\')+1);std::ofstream log(path+L"fixture-error.txt");log<<std::hex<<e.code().value<<" "<<winrt::to_string(e.message());return int(e.code());}
}
