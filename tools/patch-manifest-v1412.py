#!/usr/bin/env python3
"""Version 1.4.12, API 7 minimum; route browser intents through an API-safe Activity."""
import importlib.util,struct,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('axml',Path(__file__).with_name('patch-manifest-v146.py'))
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)

def patch(data):
 strings,ps,_=a.parse_string_pool(data,8); ids,rs=a.parse_resource_map(data,8+ps)
 strings[strings.index('1.4.11')]='1.4.12'
 def add(t):
  if t in strings:return strings.index(t)
  strings.append(t);ids.append(0);return len(strings)-1
 router=add('de.offlineflipnote.legacy.BrowserActivity')
 body=data[8+ps+rs:];chunks=[];p=0
 while p<len(body):
  n=a.u32(body,p+4);chunks.append(bytearray(body[p:p+n]));p+=n
 def attrs(c):
  pos=16+a.u16(c,24)
  for _ in range(a.u16(c,28)):
   yield strings[a.u32(c,pos+4)],pos
   pos+=a.u16(c,26)
 def val(c,name):
  for key,off in attrs(c):
   if key==name:
    v=a.u32(c,off+16);return strings[v] if c[off+15]==3 else v
  return None
 native_start=None; native_end=None;filter_start=None;filter_end=None;depth=0;inside=False;native_depth=0
 for i,c in enumerate(chunks):
  typ=a.u16(c,0)
  if typ==0x102:
   tag=strings[a.u32(c,20)]
   for key,off in attrs(c):
    if tag=='manifest' and key=='versionCode':a.put_u32(c,off+16,28)
    if tag=='uses-sdk' and key=='minSdkVersion':a.put_u32(c,off+16,7)
   if tag=='activity' and val(c,'name')=='android.app.NativeActivity':
    native_start=i;inside=True;native_depth=depth
    for key,off in attrs(c):
     if key=='exported':a.put_u32(c,off+16,0)
   if inside and tag=='intent-filter':filter_start=i
   depth+=1
  elif typ==0x103:
   depth-=1;tag=strings[a.u32(c,20)]
   if inside and tag=='intent-filter':filter_end=i
   if inside and tag=='activity' and depth==native_depth:native_end=i;inside=False
 assert None not in (native_start,native_end,filter_start,filter_end)
 start=bytearray(chunks[native_start])
 for key,off in attrs(start):
  if key=='name':a.put_u32(start,off+8,router);a.put_u32(start,off+16,router)
  if key=='exported':a.put_u32(start,off+16,0xffffffff)
 # Keep the router a standard Activity with no NativeActivity metadata.
 added=[start]+chunks[filter_start:filter_end+1]+[chunks[native_end]]
 result=[]
 for i,c in enumerate(chunks):
  if filter_start<=i<=filter_end:continue
  result.append(c)
  if i==native_end:result.extend(added)
 payload=a.build_string_pool(strings)+a.build_resource_map(ids)+b''.join(result)
 return struct.pack('<HHI',3,8,8+len(payload))+payload
if __name__=='__main__':
 src,dst=map(Path,sys.argv[1:]);dst.write_bytes(patch(src.read_bytes()));print(dst)
