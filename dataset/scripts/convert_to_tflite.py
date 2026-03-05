#!/usr/bin/env python3
"""convert_to_tflite.py - Model Conversion Tool (F-003-3)"""
import argparse, os, sys, glob, numpy as np

def export_pt_to_onnx(w, sz, out, opset=12):
    print(f"[Step 1] PyTorch -> ONNX: {w} (img={sz})")
    print(f"  python export.py --weights {w} --img {sz} --include onnx --simplify --opset {opset}")

def convert_onnx_to_tflite_fp32(onnx_path, output_path, img_size):
    print(f"[Step 2] ONNX -> TFLite FP32: {onnx_path} -> {output_path}")
    try:
        import onnx; from onnx_tf.backend import prepare; import tensorflow as tf
        sd = output_path.replace(".tflite","_saved_model")
        m = onnx.load(onnx_path); r = prepare(m); r.export_graph(sd)
        c = tf.lite.TFLiteConverter.from_saved_model(sd)
        with open(output_path,"wb") as f: f.write(c.convert())
        print(f"  Saved: {output_path} ({os.path.getsize(output_path)/1024:.1f} KB)")
    except ImportError as e: print(f"  [ERROR] {e}. pip install onnx onnx-tf tensorflow")

def quantize_int8(inp, out, cal_dir, cal_n=100, sz=192):
    print(f"[Step 3] INT8 Quantization: {inp} -> {out}")
    try:
        import tensorflow as tf; from PIL import Image
        sd = inp.replace(".tflite","_saved_model")
        if not os.path.isdir(sd):
            print(f"  [ERROR] SavedModel not found. Use mcu_quantize.py instead."); return
        def rep():
            fs = sorted(glob.glob(os.path.join(cal_dir,"*.jpg")))[:cal_n]
            if not fs: fs = sorted(glob.glob(os.path.join(cal_dir,"*.png")))[:cal_n]
            for f in (fs if fs else [None]*cal_n):
                if f:
                    im=Image.open(f).convert("L").resize((sz,sz))
                    yield [np.array(im,dtype=np.float32).reshape(1,sz,sz,1)/255.0]
                else: yield [np.random.rand(1,sz,sz,1).astype(np.float32)]
        c=tf.lite.TFLiteConverter.from_saved_model(sd)
        c.optimizations=[tf.lite.Optimize.DEFAULT]
        c.representative_dataset=rep
        c.target_spec.supported_ops=[tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        c.inference_input_type=tf.int8; c.inference_output_type=tf.int8
        with open(out,"wb") as f: f.write(c.convert())
        kb=os.path.getsize(out)/1024
        print(f"  Saved: {out} ({kb:.1f} KB). Arena limit: 432KB. Fits: {kb<=432}")
    except Exception as e: print(f"  [ERROR] {e}")

def inspect(path):
    print(f"=== Inspect: {path} ===")
    print(f"Size: {os.path.getsize(path):,} bytes ({os.path.getsize(path)/1024:.1f} KB)")
    try:
        import tensorflow as tf
        interp = tf.lite.Interpreter(model_path=path); interp.allocate_tensors()
        for lbl,dets in [("Input",interp.get_input_details()),("Output",interp.get_output_details())]:
            print(f"  === {lbl} ===")
            for i,d in enumerate(dets):
                n=d["name"]; s=d["shape"]; t=d["dtype"]
                print(f"  [{i}] {n} shape={s} dtype={t}")
                q=d.get("quantization_parameters",{})
                sc=q.get("scales",np.array([])); zp=q.get("zero_points",np.array([]))
                if len(sc)>0: print(f"      scale={sc[0]} zero_point={zp[0]}")
        print("  === Code for MainLoop_obj.cc ===")
        for i,o in enumerate(interp.get_output_details()):
            q=o.get("quantization_parameters",{})
            sc=q.get("scales",np.array([0.0])); zp=q.get("zero_points",np.array([0]))
            print(f"  create_int8_tensor(&outputTensor{i}, output{i}, {float(sc[0])}, {int(zp[0])});")
    except ImportError: print("  [ERROR] tensorflow not installed")
    except Exception as e: print(f"  [ERROR] {e}")

def main():
    p=argparse.ArgumentParser(description="Model Conversion (F-003-3)")
    p.add_argument("--weights",help="PyTorch .pt"); p.add_argument("--onnx",help="ONNX .onnx")
    p.add_argument("--img-size",type=int,default=192); p.add_argument("--output",help="Output path")
    p.add_argument("--opset",type=int,default=12); p.add_argument("--quantize",action="store_true")
    p.add_argument("--input",help="Input TFLite"); p.add_argument("--cal-images",help="Cal images dir")
    p.add_argument("--cal-count",type=int,default=100); p.add_argument("--inspect",action="store_true")
    a=p.parse_args()
    if a.inspect: inspect(a.input) if a.input else print("--input required")
    elif a.quantize:
        if not a.input: print("--input required"); return
        quantize_int8(a.input, a.output or a.input.replace(".tflite","_int8.tflite"),
                      a.cal_images or "dataset/merged/images/val/", a.cal_count, a.img_size)
    elif a.weights: export_pt_to_onnx(a.weights, a.img_size, a.output or "model.onnx", a.opset)
    elif a.onnx: convert_onnx_to_tflite_fp32(a.onnx, a.output or a.onnx.replace(".onnx","_fp32.tflite"), a.img_size)
    else: p.print_help()

if __name__=="__main__": main()
