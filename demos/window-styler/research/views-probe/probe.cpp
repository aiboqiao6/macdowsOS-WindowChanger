// Restricted, timed framework experiment. No product class or object layout is
// used: candidates are existing Views grouped by standard WM_NCHITTEST actions.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#include <memory>
#include <algorithm>
#include <string>
#include <sstream>
#include <iostream>
#include <map>
#include "../../pe-identity.h"
#include "../../third_party/minhook/include/MinHook.h"

UINT Message(){return RegisterWindowMessageW(L"Macdows.Research.Views.v12");}
#ifdef VIEWS_PROBE_DLL
namespace {
using P=unsigned char*;
struct Rect {int x,y,w,h;};
struct Float4 {float x,y,z,w;};
struct RRect {Float4 bounds;float radii[8];int type;};
static_assert(sizeof(RRect)==52);
bool Readable(const void* p,size_t n){MEMORY_BASIC_INFORMATION i{};return p&&VirtualQuery(p,&i,sizeof(i))&&i.State==MEM_COMMIT&&!(i.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&uintptr_t(p)+n<=uintptr_t(i.BaseAddress)+i.RegionSize;}
template<class T>T Read(P p,size_t offset=0){T value{};if(Readable(p+offset,sizeof(value)))memcpy(&value,p+offset,sizeof(value));return value;}
template<class F>F Virtual(P p,size_t offset){return reinterpret_cast<F>(Read<P>(Read<P>(p),offset));}
std::wstring Directory(){HMODULE m{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Directory),&m);wchar_t path[32768]{};GetModuleFileNameW(m,path,32768);std::wstring s(path);return s.substr(0,s.find_last_of(L'\\')+1);}
void Log(std::string const& s){std::ofstream f(Directory()+L"views-"+std::to_wstring(GetCurrentProcessId())+L".txt",std::ios::app);f<<GetTickCount64()<<" "<<s<<"\n";}
using SetBounds=void(*)(P,const Rect&);
using Paint=void(*)(P,P);
SetBounds originalBounds{};
P module{};
struct Node {P view{},parent{};Rect bounds{};int rootX{},rootY{},depth{},hit{};bool visible{};P vtable{};};
struct Watched {P view{},vtable{};Rect original{},applied{};int role{};bool region{},container{};std::vector<P> clone;};
struct State {HWND hwnd{};P root{};DWORD thread{};bool changing{};std::vector<std::unique_ptr<Watched>> watched;ULONGLONG expires{};UINT_PTR timer{};};
thread_local State* active{};
std::vector<State*> retained; // Keep callback/vtable storage valid until process exit.
constexpr GUID identity{0x8de675e4,0xdc25,0x455f,{0x4c,0x4c,0x44,0x20,0x50,0x44,0x42,0x2e}};
bool InImage(P p){return p>=module+0x1000&&p<module+0x114c0000;}
bool Framework(HWND hwnd,P& root){
 P handler=reinterpret_cast<P>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(!Readable(handler,0x78)||Read<HWND>(handler,0x40)!=hwnd)return false;
 HMODULE m{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(Read<P>(handler)),&m)||!styler::MatchesPdb(m,identity))return false;
 module=reinterpret_cast<P>(m);
 if(Read<P>(handler)!=module+0x119b32b0)return false;
 P delegate=Read<P>(handler,0x70);if(!Readable(delegate,8)||Read<P>(Read<P>(delegate),0xb8)!=module+0xb55a720)return false;
 P host=delegate-0x1a8,widget=Read<P>(host,0x288);root=Read<P>(widget,0x1a0);
 return Readable(root,0x330)&&Read<P>(Read<P>(root),0x238)==module+0x33c0ea0;
}
bool Walk(P view,P parent,std::vector<Node>& nodes,int depth=0,int x=0,int y=0,bool visible=true){
 if(depth>24||nodes.size()>4000||!Readable(view,0x330)||Read<uint32_t>(view,0x17c)!=0x600d600d||Read<P>(view,0x198)!=parent)return false;
 if(std::any_of(nodes.begin(),nodes.end(),[&](Node const& n){return n.view==view;}))return false;
 Node n{};n.view=view;n.parent=parent;n.bounds=Read<Rect>(view,0x1c8);n.rootX=x+n.bounds.x;n.rootY=y+n.bounds.y;n.depth=depth;n.visible=visible&&Read<bool>(view,0x1d8);n.vtable=Read<P>(view);nodes.push_back(n);
 P children=Read<P>(view,0x1a0);size_t count=Read<size_t>(view,0x1a8),cap=Read<size_t>(view,0x1b0);
 if(count>4000||cap<count||(count&&!Readable(children,count*8)))return false;
 for(size_t i=0;i<count;i++)if(!Walk(Read<P>(children,i*8),view,nodes,depth+1,n.rootX,n.rootY,n.visible))return false;
 return true;
}
void Dump(HWND hwnd,std::vector<Node>& nodes){
 POINT origin{};ClientToScreen(hwnd,&origin);double scale=GetDpiForWindow(hwnd)/96.;
 for(auto& n:nodes){
  if(n.visible&&n.bounds.w>0&&n.bounds.h>0&&n.rootY>=0&&n.rootY<64&&n.bounds.w<160){
   int x=origin.x+int((n.rootX+n.bounds.w*.5)*scale),y=origin.y+int((n.rootY+n.bounds.h*.5)*scale);
   n.hit=int(SendMessageW(hwnd,WM_NCHITTEST,0,MAKELPARAM(x,y)));
  }
 }
 std::ostringstream out;out<<"tree count="<<nodes.size();Log(out.str());
 for(auto& n:nodes)if(n.hit==HTMINBUTTON||n.hit==HTMAXBUTTON||n.hit==HTCLOSE){std::ostringstream s;s<<"candidate "<<(void*)n.view<<" parent="<<(void*)n.parent<<" hit="<<n.hit<<" rect="<<n.bounds.x<<","<<n.bounds.y<<","<<n.bounds.w<<","<<n.bounds.h<<" root="<<n.rootX<<","<<n.rootY;Log(s.str());}
}
Watched* Find(P view){if(!active||active->changing)return nullptr;for(auto& w:active->watched)if(w->view==view&&Read<P>(view)==(w->clone.empty()?w->vtable:reinterpret_cast<P>(w->clone.data())))return w.get();return nullptr;}
Rect Transform(Watched& w,Rect r){
 if(w.container){r.x=6;r.w=60;}
 else if(w.role){r.x=w.role==HTCLOSE?0:w.role==HTMINBUTTON?20:40;r.w=20;}
 else if(w.region){r.x+=72;r.w=std::max(0,r.w-72);}
 return r;
}
void BoundsHook(P view,const Rect& bounds){
 auto w=Find(view);if(!w){originalBounds(view,bounds);return;}
 w->original=bounds;w->applied=Transform(*w,bounds);originalBounds(view,w->applied);
}
void PaintHook(P view,P canvas){
 auto w=Find(view);if(!w||!w->role)return;
 // Draw into the original View's clipped recording canvas. SkRRect is an
 // inline value (no owning pointers); the framework retains original input.
 P recording=Read<P>(canvas,0xc0);if(!Readable(recording,8))return;
 Rect bounds=Read<Rect>(view,0x1c8);float cx=bounds.w*.5f,cy=bounds.h*.5f;
 RRect oval{{cx-6,cy-6,cx+6,cy+6},{6,6,6,6,6,6,6,6},2};
 Float4 color=w->role==HTCLOSE?Float4{1,.373f,.341f,1}:w->role==HTMINBUTTON?Float4{1,.741f,.18f,1}:Float4{.157f,.784f,.251f,1};
 Virtual<int(*)(P)>(recording,0x20)(recording);
 Virtual<void(*)(P,const RRect&,int,bool)>(recording,0x98)(recording,oval,1,true);
 Virtual<void(*)(P,Float4,int)>(recording,0xb8)(recording,color,3);
 Virtual<void(*)(P)>(recording,0x50)(recording);
}
void Detach(){
 if(!active||active->changing)return;auto s=active;s->changing=true;KillTimer(s->hwnd,s->timer);
 std::vector<Node> nodes;P root{};bool valid=Framework(s->hwnd,root)&&root==s->root&&Walk(root,nullptr,nodes);
 if(valid)for(auto& w:s->watched){auto n=std::find_if(nodes.begin(),nodes.end(),[&](Node const& n){return n.view==w->view;});if(n==nodes.end())continue;
  if(!w->clone.empty()&&Read<P>(w->view)==reinterpret_cast<P>(w->clone.data()))memcpy(w->view,&w->vtable,sizeof(P));
  if(Read<P>(w->view)==w->vtable)originalBounds(w->view,w->original);
 }
 MH_DisableHook(module+0x106fa68);active=nullptr;InvalidateRect(s->hwnd,nullptr,FALSE);Log(valid?"detached: originals restored":"detached: no matching live tree");
}
void CALLBACK Timer(HWND,UINT,UINT_PTR,DWORD){if(active&&GetTickCount64()>=active->expires)Detach();}
bool Attach(HWND hwnd){
 if(active)return false;P root{};if(!Framework(hwnd,root))return false;
 std::vector<Node> nodes;if(!Walk(root,nullptr,nodes))return false;Dump(hwnd,nodes);
 Node *min{},*max{},*close{};
 for(auto& n:nodes)if(n.hit==HTCLOSE){for(auto& a:nodes)if(a.parent==n.parent&&a.hit==HTMINBUTTON){for(auto& b:nodes)if(b.parent==n.parent&&b.hit==HTMAXBUTTON){min=&a;max=&b;close=&n;}}}
 if(!min||!max||!close){Log("declined: no standard caption group");return false;}
 auto parent=std::find_if(nodes.begin(),nodes.end(),[&](Node const& n){return n.view==close->parent;});if(parent==nodes.end()||parent->bounds.h>64||parent->bounds.h<20||parent->rootY>16)return false;
 auto s=new State;s->hwnd=hwnd;s->root=root;s->thread=GetCurrentThreadId();s->expires=GetTickCount64()+45000;
 auto add=[&](Node const& n,int role,bool container,bool region){auto w=std::make_unique<Watched>();w->view=n.view;w->vtable=n.vtable;w->original=n.bounds;w->role=role;w->container=container;w->region=region;s->watched.push_back(std::move(w));};
 add(*parent,0,true,false);
 for(auto& n:nodes)if(n.parent==parent->view){int role=n.view==min->view?HTMINBUTTON:n.view==close->view?HTCLOSE:HTMAXBUTTON;if(n.vtable!=max->vtable){delete s;return false;}add(n,role,false,false);}
 for(auto& n:nodes)if(n.visible&&n.rootX<72&&n.rootY<parent->rootY+parent->bounds.h&&n.bounds.w>nodes[0].bounds.w/2&&n.bounds.h>=20&&n.bounds.h<=64&&n.view!=parent->view){add(n,0,false,true);}
 if(s->watched.size()<5){delete s;return false;}
 // The setter was recovered from the framework's X/Width metadata wrappers.
 const unsigned char signature[]={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x81,0xec,0xb8,0,0,0};
 if(memcmp(module+0x106fa68,signature,sizeof(signature))){delete s;return false;}
 auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED){delete s;return false;}
 if(!originalBounds&&MH_CreateHook(module+0x106fa68,reinterpret_cast<P>(&BoundsHook),reinterpret_cast<void**>(&originalBounds))!=MH_OK){delete s;return false;}
 for(auto& w:s->watched)if(w->role){
  for(size_t i=0;i<256;i++){auto f=Read<P>(w->vtable,i*8);if(!InImage(f))break;w->clone.push_back(f);}
  if(w->clone.size()<=0x328/8){delete s;return false;}
  w->clone[0x328/8]=reinterpret_cast<P>(&PaintHook);
 }
 HMODULE pinned{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&PaintHook),&pinned)){delete s;return false;}
 if(MH_EnableHook(module+0x106fa68)!=MH_OK){delete s;return false;}
 active=s;retained.push_back(s);s->changing=true;
 for(auto& w:s->watched){if(w->role){P vtable=reinterpret_cast<P>(w->clone.data());memcpy(w->view,&vtable,8);}w->applied=Transform(*w,w->original);originalBounds(w->view,w->applied);}
 s->changing=false;s->timer=SetTimer(hwnd,0x4d563132,100,Timer);if(!s->timer){Detach();return false;}
 InvalidateRect(hwnd,nullptr,FALSE);Log("attached timed experiment: original Views / original PaintCanvas");return true;
}
}
extern "C" __declspec(dllexport) LRESULT CALLBACK ViewsResearchHook(int code,WPARAM w,LPARAM l){
 if(code>=0&&l){auto event=reinterpret_cast<CWPSTRUCT*>(l);if(event->message==Message()){
  if(event->wParam==1){P root{};std::vector<Node> nodes;if(Framework(event->hwnd,root)&&Walk(root,nullptr,nodes))Dump(event->hwnd,nodes);else Log("read declined");}
  if(event->wParam==2){if(!Attach(event->hwnd))Log("attach declined");}
  if(event->wParam==3)Detach();
  if(event->wParam==4&&!active){P root{};std::vector<Node> nodes;if(Framework(event->hwnd,root)&&Walk(root,nullptr,nodes)){for(auto const& n:nodes)if(n.depth<=5)reinterpret_cast<void(*)(P)>(module+0x1092060)(n.view);Log("original framework layout requested");}}
 }}return CallNextHookEx(nullptr,code,w,l);
}
#else
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 HWND hwnd=reinterpret_cast<HWND>(wcstoull(argv[1],nullptr,0));if(!IsWindow(hwnd))return 3;
 wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring name(path);name=name.substr(0,name.find_last_of(L'\\')+1)+VIEWS_PROBE_NAME;
 HMODULE module=LoadLibraryW(name.c_str());if(!module){auto error=GetLastError();std::wcerr<<L"LoadLibrary "<<error<<L" "<<name<<L"\n";return error;}auto proc=reinterpret_cast<HOOKPROC>(GetProcAddress(module,"ViewsResearchHook"));
 HHOOK hook=SetWindowsHookExW(WH_CALLWNDPROC,proc,module,GetWindowThreadProcessId(hwnd,nullptr));if(!hook){auto error=GetLastError();std::wcerr<<L"SetWindowsHookEx "<<error<<L" "<<name<<L"\n";return error;}DWORD_PTR result{};
 auto sent=SendMessageTimeoutW(hwnd,Message(),wcstoul(argv[2],nullptr,0),0,SMTO_ABORTIFHUNG|SMTO_BLOCK,8000,&result);
 // Keep the image mapped in the target while timed callbacks are installed.
 if(wcstoul(argv[2],nullptr,0)==2)Sleep(47000);
 UnhookWindowsHookEx(hook);
 FreeLibrary(module);return sent?0:4;
}
#endif
