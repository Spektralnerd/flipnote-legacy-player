#!/usr/bin/env python3
"""Version 1.4.13 (code 29): the v1.4.12 manifest with only the version bumped.
No new permissions, activities or intent filters; ZIP browsing is native-only."""
import importlib.util,struct,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('axml',Path(__file__).with_name('patch-manifest-v146.py'))
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
spec12=importlib.util.spec_from_file_location('v1412',Path(__file__).with_name('patch-manifest-v1412.py'))
v1412=importlib.util.module_from_spec(spec12);spec12.loader.exec_module(v1412)

VERSION_NAME='1.4.13'
VERSION_CODE=29

def patch(data):
 data=v1412.patch(data)
 strings,ps,_=a.parse_string_pool(data,8); ids,rs=a.parse_resource_map(data,8+ps)
 strings[strings.index('1.4.12')]=VERSION_NAME
 body=bytearray(data[8+ps+rs:]);p=0
 while p<len(body):
  n=a.u32(body,p+4)
  if a.u16(body,p)==0x102 and strings[a.u32(body,p+20)]=='manifest':
   pos=p+16+a.u16(body,p+24)
   for _ in range(a.u16(body,p+28)):
    if strings[a.u32(body,pos+4)]=='versionCode':a.put_u32(body,pos+16,VERSION_CODE)
    pos+=a.u16(body,p+26)
  p+=n
 payload=a.build_string_pool(strings)+a.build_resource_map(ids)+bytes(body)
 return struct.pack('<HHI',3,8,8+len(payload))+payload
if __name__=='__main__':
 src,dst=map(Path,sys.argv[1:]);dst.write_bytes(patch(src.read_bytes()));print(dst)
