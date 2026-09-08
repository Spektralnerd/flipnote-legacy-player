#!/usr/bin/env python3
"""Add an API-7 Activity/Runnable bridge; preserve all existing player methods."""
from pathlib import Path
import sys
from dex_rewriter import *

def patch(data):
    model=DexModel(data)
    desc='Lde/offlineflipnote/legacy/BrowserActivity;'
    base='Landroid/app/Activity;'
    listener='Landroid/view/View$OnTouchListener;'
    runnable='Ljava/lang/Runnable;'
    for d in (desc,base,listener,runnable): model.add_type(d)
    field=FieldKey(desc,'nativeHandle','J')
    model.add_field_ref(field)
    cls=ClassDef(desc,ACC_PUBLIC,base,[listener,runnable],None,[],[EncodedField(field,ACC_PRIVATE)],[],[],[])
    model.classes.append(cls)
    ctor=MethodKey(desc,'<init>',Proto('V',()))
    super_ctor=MethodKey(base,'<init>',Proto('V',()))
    clinit=MethodKey(desc,'<clinit>',Proto('V',()))
    load=MethodKey('Ljava/lang/System;','loadLibrary',Proto('V',('Ljava/lang/String;',)))
    model.new_strings.add('flipbrowser')
    for m in (ctor,super_ctor,clinit,load): model.add_method_ref(m)
    c=CodeItem(1,1,1,[],[])
    c.builder=lambda idx,m,c:[0x1070,idx.method_idx[super_ctor],0x0000,0x000e]
    cls.direct_methods.append(EncodedMethod(ctor,ACC_PUBLIC|ACC_CONSTRUCTOR,c))
    c=CodeItem(1,0,1,[],[])
    c.builder=lambda idx,m,c:[0x001a,idx.string_idx['flipbrowser'],0x1071,idx.method_idx[load],0x0000,0x000e]
    cls.direct_methods.append(EncodedMethod(clinit,ACC_STATIC|ACC_CONSTRUCTOR,c))
    for name,ret,args,flags in [
        ('onCreate','V',('Landroid/os/Bundle;',),ACC_PROTECTED),
        ('onResume','V',(),ACC_PROTECTED),('onPause','V',(),ACC_PROTECTED),
        ('onDestroy','V',(),ACC_PROTECTED),('onLowMemory','V',(),ACC_PUBLIC),
        ('run','V',(),ACC_PUBLIC),
        ('onTouch','Z',('Landroid/view/View;','Landroid/view/MotionEvent;'),ACC_PUBLIC),
        ('onKeyDown','Z',('I','Landroid/view/KeyEvent;'),ACC_PUBLIC),
    ]:
        method=MethodKey(desc,name,Proto(ret,args)); model.add_method_ref(method)
        cls.virtual_methods.append(EncodedMethod(method,flags|ACC_NATIVE,None))
    result=model.build(); validate_dex(result)
    # Compare all old instructions after index remapping (including their API calls).
    rebuilt=DexModel(result); indexes=model.indexes()
    checked=0
    for oldcls in DexModel(data).classes:
        for old in oldcls.direct_methods+oldcls.virtual_methods:
            new=rebuilt.method_by_key(old.key)
            assert new.access_flags==old.access_flags
            if old.code:
                assert new.code is not None
                assert model._remap_insns(old.code,indexes)==new.code.insns,old.key
                assert old.code.registers_size==new.code.registers_size
                assert old.code.ins_size==new.code.ins_size
                assert old.code.tries==new.code.tries
                checked+=1
            else: assert new.code is None
    print(f'Original player: {checked} method bodies preserved (references remapped).')
    return result
if __name__=='__main__':
    source,dest=map(Path,sys.argv[1:]); dest.write_bytes(patch(source.read_bytes())); print(dest)
