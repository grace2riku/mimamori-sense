#!/usr/bin/env python3
import argparse, os, sys, math

def inspect_tflite(model_path):
    try:
        import tensorflow as tf
    except ImportError:
        print('ERROR: pip install tensorflow')
        sys.exit(1)
    interp = tf.lite.Interpreter(model_path=model_path)
    interp.allocate_tensors()
    inp = interp.get_input_details()
    out = interp.get_output_details()
    fsize = os.path.getsize(model_path)
    fkb = fsize / 1024.0
    print(f'Model: {model_path} ({fkb:.1f}KB)')
    _show('Input', inp)
    _show('Output', out)
    print(f'Arena limit=432KB, fits={fkb < 432}')
    return interp

def _show(label, details):
    print(f'--- {label} Tensors ---')
    for i, t in enumerate(details):
        nm = t.get('name','?')
        sh = t.get('shape',[])
        if hasattr(sh,'tolist'): sh=sh.tolist()
        dt = t.get('dtype')
        print(f'  [{i}] {nm} shape={sh} dtype={dt}')
        qp = t.get('quantization_parameters',{})
        sc = qp.get('scales')
        if sc is not None and hasattr(sc,'size') and sc.size>0:
            zp=qp.get('zero_points')[0]
            print(f'      quant: scale={sc[0]}, zp={zp}')

def compare_models(pa, pb):
    ka=os.path.getsize(pa)/1024
    kb=os.path.getsize(pb)/1024
    print(f'A:{pa}={ka:.1f}KB B:{pb}={kb:.1f}KB')
    if ka>0: print(f'Reduction:{(1-kb/ka)*100:.1f}%')

def evaluate_yolo_map(mp,dd,isz=192,ct=0.5,it=0.45):
    try:
        import tensorflow as tf, numpy as np
        from PIL import Image
    except ImportError as e:
        print(f'Missing:{e}'); sys.exit(1)
    interp=tf.lite.Interpreter(model_path=mp)
    interp.allocate_tensors()
    id0=interp.get_input_details()
    od0=interp.get_output_details()
    ishp=id0[0].get('shape')
    idt=id0[0].get('dtype')
    idir=os.path.join(dd,'images')
    ldir=os.path.join(dd,'labels')
    assert os.path.isdir(idir)
    assert os.path.isdir(ldir)
    exts=('.jpg','.jpeg','.png','.bmp')
    imgs=sorted([f for f in os.listdir(idir) if f.lower().endswith(exts)])
    tgt=tdet=tp=fp=fn=0
    print(f'Evaluating {len(imgs)} images')
    for idx,imf in enumerate(imgs):
        ip=os.path.join(idir,imf)
        lp=os.path.join(ldir,os.path.splitext(imf)[0]+'.txt')
        im=Image.open(ip)
        if len(ishp)==4 and ishp[3]==1: im=im.convert('L')
        else: im=im.convert('RGB')
        im=im.resize((isz,isz))
        arr=np.array(im)
        if len(ishp)==4 and ishp[3]==1: arr=np.expand_dims(arr,-1)
        arr=np.expand_dims(arr,0)
        if idt==np.int8: arr=arr.astype(np.int8)
        elif idt==np.uint8: arr=arr.astype(np.uint8)
        else: arr=arr.astype(np.float32)/255.0
        interp.set_tensor(id0[0].get('index'),arr)
        interp.invoke()
        gb=0
        if os.path.exists(lp):
            with open(lp) as f: gb=len([l for l in f if l.strip()])
        tgt+=gb; dc=0
        for od in od0:
            o=interp.get_tensor(od.get('index'))
            qp=od.get('quantization_parameters',{})
            sc=qp.get('scales')
            if sc is not None and hasattr(sc,'size') and sc.size>0:
                zp=qp.get('zero_points')[0]
                of=(o.astype(np.float32)-zp)*sc[0]
            else: of=o.astype(np.float32)
            fl=of.flatten(); nv=6
            for j in range(0,len(fl)-nv+1,nv):
                if 1/(1+math.exp(-float(fl[j+4])))>ct: dc+=1
        tdet+=dc; m=min(dc,gb); tp+=m
        fp+=max(0,dc-gb); fn+=max(0,gb-dc)
        if (idx+1)%100==0: print(f'  {idx+1}/{len(imgs)}')
    prec=tp/(tp+fp) if tp+fp>0 else 0
    rec=tp/(tp+fn) if tp+fn>0 else 0
    f1=2*prec*rec/(prec+rec) if prec+rec>0 else 0
    print(f'P={prec:.4f} R={rec:.4f} F1={f1:.4f}')
    pct='%'
    rok='PASS' if rec>=0.9 else 'FAIL'
    fpr=fp/max(tdet,1)
    fok='PASS' if fpr<=0.05 else 'FAIL'
    print(f'KPI: Det={rec*100:.1f}{pct}[{rok}] FP={fpr*100:.1f}{pct}[{fok}]')

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--inspect',type=str)
    p.add_argument('--compare',nargs=2)
    p.add_argument('--eval',action='store_true')
    p.add_argument('--model',type=str)
    p.add_argument('--data',type=str)
    p.add_argument('--img-size',type=int,default=192)
    p.add_argument('--conf',type=float,default=0.5)
    p.add_argument('--iou',type=float,default=0.45)
    args=p.parse_args()
    if args.inspect: inspect_tflite(args.inspect)
    elif args.compare: compare_models(*args.compare)
    elif args.eval:
        assert args.model and args.data
        evaluate_yolo_map(args.model,args.data,args.img_size,args.conf,args.iou)
    else: p.print_help()

if __name__=='__main__': main()
