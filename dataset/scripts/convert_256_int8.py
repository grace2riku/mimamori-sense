"""
YOLO-Fastest 256x256 TFLite INT8 変換スクリプト
Colabで実行: !python /content/drive/MyDrive/convert_256_int8.py
または: このファイルの内容をColabのセルに貼り付けて実行
"""
import numpy as np
import glob
import os
from PIL import Image

WORK_DIR = '/content/yolo-fastest'
DATASET_DIR = os.path.join(WORK_DIR, 'dataset')
ONNX_PATH = '/content/drive/MyDrive/yolo_fastest_person/train/weights/best.onnx'
SAVED_MODEL_DIR = os.path.join(WORK_DIR, 'saved_model_256')
FP32_PATH = os.path.join(WORK_DIR, 'yolo_fastest_person_256_fp32.tflite')
INT8_PATH = os.path.join(WORK_DIR, 'yolo_fastest_person_256_int8.tflite')

print('=== ONNX -> SavedModel (onnx2tf) ===')
os.system(f'onnx2tf -i {ONNX_PATH} -o {SAVED_MODEL_DIR} -osd 2>&1 | tail -10')

if os.path.isdir(SAVED_MODEL_DIR):
    import tensorflow as tf

    print('\n=== FP32 TFLite 変換 ===')
    converter = tf.lite.TFLiteConverter.from_saved_model(SAVED_MODEL_DIR)
    tflite_fp32 = converter.convert()
    with open(FP32_PATH, 'wb') as f:
        f.write(tflite_fp32)
    print(f'FP32 TFLite: {os.path.getsize(FP32_PATH)/1024:.1f} KB')

    print('\n=== INT8 量子化 (RGB) ===')
    cal_dir = os.path.join(DATASET_DIR, 'images', 'val')
    cal_images = sorted(glob.glob(os.path.join(cal_dir, '*.jpg')))[:200]
    if not cal_images:
        cal_images = sorted(glob.glob(os.path.join(cal_dir, '*.png')))[:200]
    print(f'キャリブレーション画像: {len(cal_images)}枚')

    interp = tf.lite.Interpreter(model_path=FP32_PATH)
    interp.allocate_tensors()
    inp_shape = interp.get_input_details()[0]['shape']
    n, h, w, c = inp_shape
    print(f'入力形状: {inp_shape}')

    def representative_dataset():
        for img_path in cal_images:
            img = Image.open(img_path).convert('RGB').resize((w, h))
            arr = np.array(img, dtype=np.float32) / 255.0
            arr = arr.reshape(1, h, w, 3)
            yield [arr]

    converter = tf.lite.TFLiteConverter.from_saved_model(SAVED_MODEL_DIR)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8

    int8_model = converter.convert()
    with open(INT8_PATH, 'wb') as f:
        f.write(int8_model)
    print(f'INT8 TFLite: {os.path.getsize(INT8_PATH)/1024:.1f} KB')

    # モデル詳細
    print('\n=== INT8 モデル詳細 ===')
    interp = tf.lite.Interpreter(model_path=INT8_PATH)
    interp.allocate_tensors()
    for tag, details in [('Input', interp.get_input_details()),
                         ('Output', interp.get_output_details())]:
        print(f'\n--- {tag} ---')
        for i, d in enumerate(details):
            print(f'  [{i}] {d["name"]} shape={d["shape"]} dtype={d["dtype"]}')
            qp = d.get('quantization_parameters', {})
            sc = qp.get('scales', np.array([]))
            zp = qp.get('zero_points', np.array([]))
            if len(sc) > 0:
                print(f'      scale={sc[0]:.8f}, zero_point={zp[0]}')

    # Google Driveにもコピー
    import shutil
    output_dir = '/content/drive/MyDrive/yolo_fastest_person_model'
    os.makedirs(output_dir, exist_ok=True)
    shutil.copy2(INT8_PATH, os.path.join(output_dir, 'yolo_fastest_person_256_int8.tflite'))
    print(f'\nGoogle Drive に保存: {output_dir}/yolo_fastest_person_256_int8.tflite')
else:
    print('ERROR: SavedModel 生成失敗')
