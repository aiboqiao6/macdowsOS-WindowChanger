"""Extract a library's CodeView, named ABI exports and matching type strings."""
import argparse,json,pathlib,re,struct
from pe_inspect import PE

def exports(pe):
 address,size=pe.directories[0]
 if not address:return []
 flags,stamp,major,minor,name,base,count,names,functions,ids,ordinals=struct.unpack_from('<IIHH7I',pe.image,address)
 result=[]
 for i in range(names):
  pointer=struct.unpack_from('<I',pe.image,ids+i*4)[0]
  end=pe.image.find(b'\0',pointer)
  label=pe.image[pointer:end].decode('ascii','replace')
  ordinal=struct.unpack_from('<H',pe.image,ordinals+i*2)[0]
  function=struct.unpack_from('<I',pe.image,functions+ordinal*4)[0]
  result.append(dict(name=label,ordinal=base+ordinal,rva=hex(function),entry=pe.image[function:function+32].hex()))
 return result

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('path');p.add_argument('--out',required=True);p.add_argument('--pattern',default='NetUI|BCG|GFSpy|GFFrame|GFPopup|Caption|TitleBar');a=p.parse_args()
 pe=PE(a.path);pattern=re.compile(a.pattern,re.I)
 result=pe.identity();all_exports=exports(pe)
 result['namedExportCount']=len(all_exports);result['exports']=[e for e in all_exports if pattern.search(e['name'])]
 result['types']=sorted(set(s.decode('ascii','replace') for s in re.findall(rb'\.\?A[UV][^\x00]{3,180}\x00',pe.raw) if pattern.search(s.decode('ascii','replace'))))
 result['hasUnbackedExecutableSections']=any(vs and not raw and name!='.rsrc' for name,rva,vs,raw,pointer in pe.sections)
 pathlib.Path(a.out).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
