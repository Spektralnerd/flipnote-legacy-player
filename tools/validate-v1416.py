#!/usr/bin/env python3
"""APK-level checks for the 1.4.16 build (native mixer, device-class UI tick); not an Android runtime test.
usage: validate-v1416.py APK [--unsigned]"""
from pathlib import Path
import hashlib,importlib.util,subprocess,sys,tempfile,zipfile
from dex_rewriter import DexModel,validate_dex,ACC_NATIVE,ACC_STATIC,ACC_PUBLIC,utf16_sort_key,MethodKey,Proto
from dalvik_verify_check import check as dalvik_check
from dalvik_patch import instruction_bounds
try:
 import asn1crypto  # noqa: F401  (only needed to verify a signed APK)
 HAVE_ASN1=True
except ImportError:
 import types
 HAVE_ASN1=False
 for name in ('asn1crypto','asn1crypto.cms','asn1crypto.pem','asn1crypto.x509'):
  sys.modules.setdefault(name,types.ModuleType(name))
 sys.modules['asn1crypto'].cms=sys.modules['asn1crypto.cms'];sys.modules['asn1crypto'].pem=sys.modules['asn1crypto.pem'];sys.modules['asn1crypto'].x509=sys.modules['asn1crypto.x509']
spec=importlib.util.spec_from_file_location('prev',Path(__file__).with_name('validate-release.py'))
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
spec12=importlib.util.spec_from_file_location('v1412',Path(__file__).with_name('validate-v1412.py'))
v1412=importlib.util.module_from_spec(spec12);spec12.loader.exec_module(v1412)
CERT='88:DD:F1:34:7A:2A:6C:94:A7:3C:4C:56:AA:FA:DD:46:65:71:C5:F9:7F:02:DF:7D:D7:8E:D1:E2:56:C3:90:A6'
MAIN='Lde/offlineflipnote/legacy/MainActivity;'
TICK_DELAY=MethodKey(MAIN,'tickDelayMillis',Proto('J',()))


def validate(path,unsigned=False):
 if unsigned:
  with zipfile.ZipFile(path) as z:
   assert not any(n.upper().startswith('META-INF/') for n in z.namelist())
 else:
  assert HAVE_ASN1,'Signaturpruefung braucht die Pakete aus requirements-signing.txt'
  fp=v.verify_signature(path);assert fp==CERT
 v.verify_alignment(path)
 with zipfile.ZipFile(path) as z:
  assert z.testzip() is None
  root,sdk,perms=v.manifest_values(z.read('AndroidManifest.xml'))
  assert root=={'versionCode':32,'versionName':'1.4.16','package':'de.offlineflipnote.legacy'},root
  assert sdk=={'minSdkVersion':7,'targetSdkVersion':28},sdk
  assert 'android.permission.INTERNET' not in perms and 'android.permission.WAKE_LOCK' not in perms
  assert perms=={'android.permission.READ_EXTERNAL_STORAGE','android.permission.WRITE_EXTERNAL_STORAGE','android.permission.MANAGE_EXTERNAL_STORAGE'},perms
  acts=v1412.components(z.read('AndroidManifest.xml'))
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
  native_names=[method.key.name for method in cls.virtual_methods if method.access_flags&ACC_NATIVE]
  assert len(native_names)==8
  # 1.4.15: Dalvik narrowing rules, positionOf() guard, CrashLog.
  with tempfile.NamedTemporaryFile(suffix='.dex') as f:
   f.write(dex);f.flush();problems=dalvik_check(Path(f.name))
  assert problems==[],problems
  engine=m.class_by_desc('Lde/offlineflipnote/legacy/PpmAudioEngine;')
  pos=[x for x in engine.direct_methods+engine.virtual_methods if x.key.name=='positionOf'][0]
  assert len(pos.code.tries)==1 and pos.code.tries[0].start_addr==0 and pos.code.tries[0].handler.pairs[0][0]=='Ljava/lang/RuntimeException;'
  crash=m.class_by_desc('Lde/offlineflipnote/legacy/CrashLog;')
  assert crash.interfaces==['Ljava/lang/Thread$UncaughtExceptionHandler;']
  assert [x.key.name for x in crash.virtual_methods]==['uncaughtException'] and crash.virtual_methods[0].access_flags&ACC_NATIVE
  # 1.4.16: the mixer is native and has no bytecode; MainActivity owns a
  # public static native tickDelayMillis()J and its ticker calls it instead
  # of the former constant 16 ms.
  mixer=m.class_by_desc('Lde/offlineflipnote/legacy/PpmAudioMixer;')
  mix=[x for x in mixer.virtual_methods if x.key.name=='mix'][0]
  assert mix.code is None and mix.access_flags==(ACC_PUBLIC|ACC_NATIVE),hex(mix.access_flags)
  assert mix.key.proto==Proto('V',('[S','I','I','J'))
  main=m.class_by_desc(MAIN)
  tick=[x for x in main.direct_methods if x.key==TICK_DELAY]
  assert len(tick)==1 and tick[0].code is None and tick[0].access_flags==(ACC_PUBLIC|ACC_STATIC|ACC_NATIVE)
  ticker=m.class_by_desc('Lde/offlineflipnote/legacy/MainActivity$1;')
  run=[x for x in ticker.virtual_methods if x.key.name=='run'][0]
  words=run.code.insns
  ops=[(pc,words[pc]&0xff) for pc,_w,kind in instruction_bounds(words) if kind=='insn']
  assert [op for _pc,op in ops]==[0x54,0x71,0x54,0x71,0x0c,0x71,0x0b,0x6e,0x0e],ops
  call_pc=ops[5][0]
  assert words[call_pc]==0x0071 and m.methods[words[call_pc+1]]==TICK_DELAY and words[call_pc+2]==0
  assert words[call_pc+3]==0x010b,'move-result-wide v1 expected'
  assert not any(w==0x0116 for w in words),'constant 16 ms ticker still present'
  for abi in ('armeabi','armeabi-v7a','x86','arm64-v8a','x86_64'):
   blob=z.read(f'lib/{abi}/libflipbrowser.so')
   with tempfile.NamedTemporaryFile(suffix='.so') as f:
    f.write(blob);f.flush()
    dynamic=subprocess.check_output(['readelf','-dW',f.name],text=True)
    symbols=subprocess.check_output(['readelf','-Ws',f.name],text=True)
    assert '[libandroid.so]' not in dynamic and '[libz.so]' not in dynamic
    assert '[libdl.so]' in dynamic and '[libc.so]' in dynamic
    for name in native_names:
     assert 'Java_de_offlineflipnote_legacy_BrowserActivity_'+name in symbols
    assert 'Java_de_offlineflipnote_legacy_CrashLog_uncaughtException' in symbols
    assert 'JNI_OnLoad' in symbols
    assert 'Java_de_offlineflipnote_legacy_MainActivity_galleryTick' in symbols
    assert 'Java_de_offlineflipnote_legacy_MainActivity_tickDelayMillis' in symbols
    assert 'Java_de_offlineflipnote_legacy_PpmAudioMixer_mix' in symbols
    assert 'ANativeActivity_onCreate' in symbols
    undefined={line.split()[-1] for line in symbols.splitlines() if ' UND ' in line and len(line.split())>=8}
    assert not any(s.startswith(('AInput','ANative','AMotion','AKey','__aeabi_','__udiv','__div','__ashl','__lshr','__ashr','__float','__fix','inflate','deflate','crc32')) for s in undefined),undefined
    assert not any(s in undefined for s in ('floor','ceil','fmod','sqrt','pow')),undefined
    # ZIP browsing needs exactly these additional libc calls; no zlib. lseek64
    # (bionic since API 3) carries the 64-bit archive positions on 32-bit ABIs.
    assert {'fwrite','rename','unlink','rmdir','mkdir','open','close','read','lseek64'}<=undefined,undefined
    assert not ({'fseeko','ftello','fseeko64','ftello64','lseek','pread64','pread'}&undefined),undefined
   for marker in (b'ANIMIERTE VORSCHAUEN',b'animate_previews',b'VORSCHAU: STATISCH',b'setPixels',b'layoutInDisplayCutoutMode',
                  b'ZIP-ARCHIV',b'ZIP-INDEX VOLL',b'last_directory',b'getCacheDir',b'ZIP NICHT LESBAR',b'Flipnote-Absturz.txt',b'setDefaultUncaughtExceptionHandler',
                  b'bgmSourceRate',b'OUTPUT_SAMPLE_RATE',b'hasWindowFocus',b'noteName'):
    assert marker in blob,(abi,marker)
 print('PASS manifest: version 1.4.16, code 32, min API 7, target API 28, permissions unchanged')
 print('PASS only API-safe BrowserActivity is in picker; NativeActivity is internal-only')
 print('PASS DEX035 sorted tables, SHA1/Adler32, 8 native bridge methods, Dalvik narrowing rules clean, positionOf guard and CrashLog present')
 print('PASS DEX 1.4.16: PpmAudioMixer.mix() native without bytecode, MainActivity.tickDelayMillis() static native, ticker calls it (no constant 16 ms)')
 print('PASS 5 ABIs: libc/libdl only, no libz/libandroid/libm, no compiler-rt imports; JNI_OnLoad, CrashLog, mixer and tick exports present')
 print('PASS ZIP alignment' + ('; unsigned test build (no META-INF)' if unsigned else '; classic APK V1 SHA1/RSA digests and signing certificate'))
 print('APK bytes:',path.stat().st_size)
 print('APK SHA256:',hashlib.sha256(path.read_bytes()).hexdigest())
 print('DEX SHA256:',hashlib.sha256(dex).hexdigest())
if __name__=='__main__':validate(Path(sys.argv[1]),'--unsigned' in sys.argv[2:])
