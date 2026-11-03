"""Read HWND-owned object identities. Never executes or writes target code."""
import argparse, ctypes as C, json, pathlib, struct
from ctypes import wintypes as W

u=C.WinDLL('user32',use_last_error=True);k=C.WinDLL('kernel32',use_last_error=True)
u.GetWindowLongPtrW.argtypes=[W.HWND,C.c_int];u.GetWindowLongPtrW.restype=C.c_ssize_t
u.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
k.CloseHandle.argtypes=[W.HANDLE]
k.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.c_void_p]
k.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];k.CreateToolhelp32Snapshot.restype=W.HANDLE
class Module(C.Structure):
 _fields_=[('size',W.DWORD),('id',W.DWORD),('pid',W.DWORD),('global_',W.DWORD),('local',W.DWORD),('base',C.c_void_p),('length',W.DWORD),('handle',W.HMODULE),('name',W.WCHAR*256),('path',W.WCHAR*260)]
k.Module32FirstW.argtypes=[W.HANDLE,C.POINTER(Module)];k.Module32NextW.argtypes=[W.HANDLE,C.POINTER(Module)]
Enum=C.WINFUNCTYPE(W.BOOL,W.HWND,C.c_ssize_t)
u.EnumChildWindows.argtypes=[W.HWND,Enum,C.c_ssize_t]
PropEnum=C.WINFUNCTYPE(C.c_int,W.HWND,C.c_void_p,W.HANDLE,C.c_size_t)
u.EnumPropsExW.argtypes=[W.HWND,PropEnum,C.c_size_t]
class Reader:
 def __init__(self,hwnd):
  pid=W.DWORD();u.GetWindowThreadProcessId(hwnd,C.byref(pid));self.pid=pid.value
  self.h=k.OpenProcess(0x410,False,self.pid)
  if not self.h:raise C.WinError(C.get_last_error())
  self.modules=[];snapshot=k.CreateToolhelp32Snapshot(0x18,self.pid)
  if snapshot==C.c_void_p(-1).value:raise C.WinError(C.get_last_error())
  entry=Module();entry.size=C.sizeof(entry)
  if k.Module32FirstW(snapshot,C.byref(entry)):
   while True:
    self.modules.append(dict(base=entry.base,size=entry.length,name=entry.name,path=entry.path))
    if not k.Module32NextW(snapshot,C.byref(entry)):break
  k.CloseHandle(snapshot)
  self.ptr=4 if any(m['name'].lower()=='wow64.dll' for m in self.modules) or all(m['base']<0x100000000 for m in self.modules) else 8
 def read(self,address,size):
  if size>65536:raise ValueError('oversized read')
  b=C.create_string_buffer(size)
  if not k.ReadProcessMemory(self.h,address,b,size,None):raise C.WinError(C.get_last_error())
  return b.raw
 def integer(self,address,n=None):return int.from_bytes(self.read(address,n or self.ptr),'little')
 def module(self,address):return next((m for m in self.modules if m['base']<=address<m['base']+m['size']),None)
 def obj(self,address):
  result={'address':hex(address)}
  try:
   vt=self.integer(address);m=self.module(vt);result['vtable']=hex(vt)
   if not m:return result
   result.update(module=m['name'],vtableRva=hex(vt-m['base']))
   result['head']=self.read(address,128).hex()
   result['virtuals']=[hex(self.integer(vt+i*self.ptr)) for i in range(24)]
   col=self.integer(vt-self.ptr)
   if not self.module(col):result['rtti']=False;return result
   raw=self.read(col,24);signature,offset,cd,td,chd=struct.unpack_from('<5I',raw)
   name=m['base']+td+16 if self.ptr==8 and signature==1 else td+8
   result['type']=self.read(name,200).split(b'\0')[0].decode('ascii')
   result['offset']=offset
  except (OSError,UnicodeError,ValueError) as e:result['error']=str(e)
  return result
 def window(self,h):
  name=C.create_unicode_buffer(256);u.GetClassNameW(h,name,256)
  result={'hwnd':hex(h),'class':name.value,'slots':{},'properties':[]}
  def prop(hwnd,p,value,data):
   label=C.wstring_at(p) if p>65535 else '#'+str(p)
   result['properties'].append({'name':label,'value':hex(value or 0)})
   return 1
  callback=PropEnum(prop);u.EnumPropsExW(h,callback,0)
  for slot in [-21,0,self.ptr,self.ptr*2,self.ptr*3]:
   value=u.GetWindowLongPtrW(h,slot)&((1<<(self.ptr*8))-1)
   if value:result['slots'][str(slot)]=self.obj(value)
  return result
 def close(self):k.CloseHandle(self.h)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--hwnd',type=lambda s:int(s,0),required=True);p.add_argument('--out',required=True);a=p.parse_args()
 reader=Reader(a.hwnd)
 try:
  windows=[reader.window(a.hwnd)]
  def visit(h,l):
   name=C.create_unicode_buffer(256);u.GetClassNameW(h,name,256)
   if name.value=='NetUIHWND' or name.value.startswith('BCGPRibbon'):windows.append(reader.window(h))
   return True
  callback=Enum(visit);u.EnumChildWindows(a.hwnd,callback,0)
  result={'pid':reader.pid,'ptr':reader.ptr,'modules':reader.modules,'windows':windows}
  pathlib.Path(a.out).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
 finally:reader.close()
