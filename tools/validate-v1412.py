#!/usr/bin/env python3
"""APK-level checks for the API7 browser test; does not claim an Android runtime test."""
from pathlib import Path
import hashlib,importlib.util,struct,subprocess,sys,tempfile,zipfile
from dex_rewriter import DexModel,validate_dex,ACC_NATIVE,utf16_sort_key
spec=importlib.util.spec_from_file_location('prev',Path(__file__).with_name('validate-release.py'))
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)

def components(data):
 m=v.load_manifest_module();strings,ps,_=m.parse_string_pool(data,8);_,rs=m.parse_resource_map(data,8+ps)
 body=data[8+ps+rs:];i=0;stack=[];out={};active=None
 while i<len(body):
  t=m.u16(body,i);size=m.u32(body,i+4)
  if t==m.START_ELEMENT:
   tag=strings[m.u32(body,i+20)];attrs={};p=i+16+m.u16(body,i+24)
   for _ in range(m.u16(body,i+28)):
    n=strings[m.u32(body,p+4)];d=m.u32(body,p+16);attrs[n]=strings[d] if body[p+15]==3 else d;p+=m.u16(body,i+26)
   stack.append(tag)
   if tag=='activity':active=attrs['name'];out[active]={'attrs':attrs,'actions':[]}
   elif tag=='action' and active:out[active]['actions'].append(attrs['name'])
  elif t==m.END_ELEMENT:
   tag=strings[m.u32(body,i+20)];assert stack.pop()==tag
   if tag=='activity':active=None
  i+=size
 assert not stack
 return out

def validate(path):
 fp=v.verify_signature(path);assert fp=='88:DD:F1:34:7A:2A:6C:94:A7:3C:4C:56:AA:FA:DD:46:65:71:C5:F9:7F:02:DF:7D:D7:8E:D1:E2:56:C3:90:A6'
 v.verify_alignment(path)
 with zipfile.ZipFile(path) as z:
  assert z.testzip() is None
  root,sdk,perms=v.manifest_values(z.read('AndroidManifest.xml'))
  assert root=={'versionCode':28,'versionName':'1.4.12','package':'de.offlineflipnote.legacy'}
  assert sdk=={'minSdkVersion':7,'targetSdkVersion':28}
  assert 'android.permission.INTERNET' not in perms and 'android.permission.WAKE_LOCK' not in perms
  acts=components(z.read('AndroidManifest.xml'))
  assert acts['android.app.NativeActivity']['attrs']['exported']==0
  assert not acts['android.app.NativeActivity']['actions']
  router='de.offlineflipnote.legacy.BrowserActivity'
  assert acts[router]['actions']==['android.intent.action.GET_CONTENT']
  assert acts[router]['attrs']['exported']==0xffffffff
  assert 'android.intent.action.VIEW' in acts['.MainActivity']['actions']
  dex=z.read('classes.dex');validate_dex(dex);m=DexModel(dex)
  assert dex[:8]==b'dex\n035\0'
  assert m.strings==sorted(set(m.strings),key=utf16_sort_key)
  assert m.types==sorted(set(m.types),key=lambda x:m.strings.index(x))
  idx=m.indexes();assert m.methods==idx.methods and m.fields==idx.fields and m.protos==idx.protos
  cls=m.class_by_desc('Lde/offlineflipnote/legacy/BrowserActivity;');assert cls.superclass=='Landroid/app/Activity;'
  assert cls.instance_fields[0].key.type=='J'
  native_names=[method.key.name for method in cls.virtual_methods if method.access_flags&ACC_NATIVE]
  assert len(native_names)==8
  for abi in ('armeabi','armeabi-v7a','x86','arm64-v8a','x86_64'):
   blob=z.read(f'lib/{abi}/libflipbrowser.so')
   with tempfile.NamedTemporaryFile(suffix='.so') as f:
    f.write(blob);f.flush()
    dynamic=subprocess.check_output(['readelf','-dW',f.name],text=True)
    symbols=subprocess.check_output(['readelf','-Ws',f.name],text=True)
    assert '[libandroid.so]' not in dynamic
    assert '[libdl.so]' in dynamic and '[libc.so]' in dynamic
    for name in native_names:
     assert 'Java_de_offlineflipnote_legacy_BrowserActivity_'+name in symbols
    assert 'Java_de_offlineflipnote_legacy_MainActivity_galleryTick' in symbols
    assert 'ANativeActivity_onCreate' in symbols
    undefined={line.split()[-1] for line in symbols.splitlines() if ' UND ' in line and len(line.split())>=8}
    assert not any(s.startswith(('AInput','ANative','AMotion','AKey','__aeabi_')) for s in undefined),undefined
   for marker in (b'ANIMIERTE VORSCHAUEN',b'animate_previews',b'VORSCHAU: STATISCH',b'setPixels',b'layoutInDisplayCutoutMode'):
    assert marker in blob,(abi,marker)
 print('PASS manifest: version 1.4.12, code 28, min API 7, target API 28, permissions unchanged')
 print('PASS only API-safe BrowserActivity is in picker; NativeActivity is internal-only')
 print('PASS DEX035 sorted tables, SHA1/Adler32 and 8 native bridge method declarations')
 print('PASS 5 ABIs: libc/libdl only; no load-time libandroid/API9 requirement; exports complete')
 print('PASS classic APK V1 SHA1/RSA digests and signing certificate; ZIP alignment')
 print('APK bytes:',path.stat().st_size)
 print('APK SHA256:',hashlib.sha256(path.read_bytes()).hexdigest())
 print('DEX SHA256:',hashlib.sha256(dex).hexdigest())
if __name__=='__main__':validate(Path(sys.argv[1]))
