"""学習済み重みから TFLite INT8 + Vela を作り直すだけのスクリプト (Issue #148)

実行場所: Colab VM
  colab --auth=adc exec -s <name> -f dataset/scripts/convert_only_colab.py --timeout 3600

--------------------------------------------------------------------------
なぜ必要か
--------------------------------------------------------------------------
Round C (224px) では darknet の学習・評価は 224px で正しく行われたが、
学習ノートブックの変換セルに `IMG_SIZE = 192` がハードコードされていたため、
**出力された TFLite が 192px のまま**だった (方針書 6.1.8)。
出力テンソルが 12x12 / 6x6 (=192px 相当) で、224px なら 14x14 / 7x7 に
なるはずのところが変わっていないことで発覚した。

精度の結論 (mAP 54.97% / Precision 62%) は darknet 側の評価なので有効。
**変換成果物だけを作り直せばよく、5 時間の再学習は不要**である。

--------------------------------------------------------------------------
設計方針
--------------------------------------------------------------------------
- **学習ノートブックは実行しない**。誤って再学習が走る事故を構造的に防ぐ
- **cfg は Drive に保存された学習時のものをそのまま使う**。
  再生成するとアンカーが変わり、#137 6.1.3 と同じ
  「学習時と評価時でアンカーが違う」事故 (mAP 21% まで低下) になる
- 入力解像度は **cfg の width/height から読む**。ここを別途指定すると
  今回と同じ不整合を再発させるため、単一の情報源にする

--------------------------------------------------------------------------
前提 (Drive に置かれていること)
--------------------------------------------------------------------------
  <RUN_DIR>/yolo-fastest-person-192.cfg          学習時の cfg (アンカー込み)
  <RUN_DIR>/yolo-fastest-person-192_final.weights 学習済み重み
  /content/drive/MyDrive/fall_detection_dataset.zip  キャリブレーション用画像
"""
import glob
import json
import os
import subprocess
import sys

# ---------------------------------------------------------------------------
# 設定
# ---------------------------------------------------------------------------
GDRIVE_ROOT = '/content/drive/MyDrive/yolo_fastest_darknet_person'
RUN_NAME = 'issue148_roundC'          # 変換したいラウンド
WEIGHTS_NAME = 'yolo-fastest-person-192_final.weights'
CFG_NAME = 'yolo-fastest-person-192.cfg'
DATASET_ZIP = '/content/drive/MyDrive/fall_detection_dataset.zip'

RUN_DIR = os.path.join(GDRIVE_ROOT, RUN_NAME)
OUT_DIR = os.path.join(RUN_DIR, 'model_fixed')   # 作り直した成果物の置き場
WORK = '/content'
CONVERTER_DIR = os.path.join(WORK, 'keras-YOLOv3-model-set')
CFG_PATH = os.path.join(WORK, CFG_NAME)
WEIGHTS_PATH = os.path.join(WORK, WEIGHTS_NAME)
H5_PATH = os.path.join(WORK, 'yolo_fastest_person.h5')
FP32_PATH = os.path.join(WORK, 'yolo_fastest_person_fp32.tflite')
INT8_PATH = os.path.join(WORK, 'yolo_fastest_person_darknet_int8.tflite')
VELA_DIR = os.path.join(WORK, 'vela_output')
CALIB_MAX = 200


def run(cmd, **kw):
    """コマンドを実行し、出力をこのセルの出力に残す。"""
    p = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if p.stdout:
        print(p.stdout, end='' if p.stdout.endswith('\n') else '\n')
    if p.stderr and p.stderr.strip():
        print('--- stderr ---')
        print(p.stderr[-4000:], end='' if p.stderr.endswith('\n') else '\n')
    return p.returncode


print('=' * 70)
print('TFLite / Vela 作り直し (学習は行わない)')
print('=' * 70)
print(f'  対象ラウンド : {RUN_NAME}')
print(f'  出力先       : {OUT_DIR}')

# ---------------------------------------------------------------------------
# 1. Drive から cfg と weights を取得
# ---------------------------------------------------------------------------
print('\n[1/6] Drive から cfg / weights を取得')
import shutil

for src_name, dst in ((CFG_NAME, CFG_PATH), (WEIGHTS_NAME, WEIGHTS_PATH)):
    src = os.path.join(RUN_DIR, src_name)
    if not os.path.isfile(src):
        raise RuntimeError(
            f'{src} がありません。RUN_NAME ({RUN_NAME}) が正しいか、'
            ' 学習が完了しているか確認してください。')
    shutil.copy2(src, dst)
    print(f'  {src} -> {dst} ({os.path.getsize(dst) / 1024:.1f} KB)')

# cfg から入力解像度を読む (単一の情報源にする)
cfg_text = open(CFG_PATH, encoding='utf-8', errors='replace').read()
import re

m_w = re.search(r'^width\s*=\s*(\d+)', cfg_text, re.M)
m_h = re.search(r'^height\s*=\s*(\d+)', cfg_text, re.M)
if not m_w or not m_h:
    raise RuntimeError('cfg から width/height を読めませんでした')
IMG_W, IMG_H = int(m_w.group(1)), int(m_h.group(1))
if IMG_W != IMG_H:
    raise RuntimeError(f'正方形の入力を前提にしています (width={IMG_W}, height={IMG_H})')
IMG_SIZE = IMG_W

m_anchors = re.search(r'^anchors\s*=\s*(.+)$', cfg_text, re.M)
print(f'  入力解像度   : {IMG_SIZE}x{IMG_SIZE}  (cfg から取得)')
print(f'  期待グリッド : {IMG_SIZE // 16}x{IMG_SIZE // 16} と {IMG_SIZE // 32}x{IMG_SIZE // 32}')
if m_anchors:
    print(f'  アンカー     : {m_anchors.group(1).strip()}')

# ---------------------------------------------------------------------------
# 2. キャリブレーション用の画像を用意
# ---------------------------------------------------------------------------
print('\n[2/6] キャリブレーション用画像')
DATASET_DIR = os.path.join(WORK, 'dataset')
if not os.path.isdir(os.path.join(DATASET_DIR, 'images', 'val')):
    if not os.path.isfile(DATASET_ZIP):
        raise RuntimeError(f'{DATASET_ZIP} がありません')
    print('  データセットを展開中 (val のみで十分だが zip 全体を展開する) ...')
    os.makedirs(DATASET_DIR, exist_ok=True)
    rc = run(['unzip', '-q', '-o', DATASET_ZIP, '-d', DATASET_DIR])
    # unzip は警告時に 1 を返すため 1 は許容する
    if rc not in (0, 1):
        raise RuntimeError(f'unzip が異常終了しました (exit={rc})')

cal_images = sorted(glob.glob(os.path.join(DATASET_DIR, 'images', 'val', '*.jpg')))[:CALIB_MAX]
if not cal_images:
    cal_images = sorted(glob.glob(os.path.join(DATASET_DIR, 'images', 'val', '*.png')))[:CALIB_MAX]
if not cal_images:
    raise RuntimeError('キャリブレーション画像が見つかりません')
print(f'  {len(cal_images)} 枚')

# ---------------------------------------------------------------------------
# 3. darknet -> Keras
# ---------------------------------------------------------------------------
print('\n[3/6] darknet -> Keras (.h5)')
if not os.path.isdir(CONVERTER_DIR):
    rc = run(['git', 'clone', '--depth', '1',
              'https://github.com/david8862/keras-YOLOv3-model-set.git',
              CONVERTER_DIR])
    if rc != 0:
        raise RuntimeError('変換ツールの clone に失敗しました')

if os.path.exists(H5_PATH):
    os.remove(H5_PATH)
rc = run([sys.executable, 'tools/model_converter/convert.py',
          CFG_PATH, WEIGHTS_PATH, H5_PATH], cwd=CONVERTER_DIR)
if rc != 0 or not os.path.exists(H5_PATH):
    raise RuntimeError(f'Keras 変換に失敗しました (exit={rc})')
print(f'  {H5_PATH} ({os.path.getsize(H5_PATH) / 1024:.1f} KB)')

# ---------------------------------------------------------------------------
# 4. Keras -> TFLite (FP32 -> INT8)
# ---------------------------------------------------------------------------
print('\n[4/6] Keras -> TFLite INT8')
import numpy as np
import tensorflow as tf
import tf_keras
from PIL import Image

model = tf_keras.models.load_model(H5_PATH, compile=False)

# 入力サイズを固定する。ここは必ず cfg 由来の IMG_SIZE を使う
# (Round C ではここが 192 固定だったため 224 学習 / 192 変換の不整合が起きた)
fixed_input = tf_keras.layers.Input(shape=(IMG_SIZE, IMG_SIZE, 3), name='image_input')
outputs = model(fixed_input)
fixed_model = tf_keras.Model(inputs=fixed_input, outputs=outputs)

converter = tf.lite.TFLiteConverter.from_keras_model(fixed_model)
open(FP32_PATH, 'wb').write(converter.convert())
print(f'  FP32: {os.path.getsize(FP32_PATH) / 1024:.1f} KB')


def representative_dataset():
    for img_path in cal_images:
        img = Image.open(img_path).convert('RGB').resize((IMG_SIZE, IMG_SIZE))
        arr = np.asarray(img, dtype=np.float32) / 255.0
        yield [arr.reshape(1, IMG_SIZE, IMG_SIZE, 3)]


converter = tf.lite.TFLiteConverter.from_keras_model(fixed_model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8
open(INT8_PATH, 'wb').write(converter.convert())
print(f'  INT8: {os.path.getsize(INT8_PATH) / 1024:.1f} KB')

# ---------------------------------------------------------------------------
# 5. 形状の検証 (今回の不整合を二度と見逃さないための要)
# ---------------------------------------------------------------------------
print('\n[5/6] 形状の検証')
interp = tf.lite.Interpreter(model_path=INT8_PATH)
interp.allocate_tensors()

quant = {'input': [], 'output': []}
for tag, details in (('input', interp.get_input_details()),
                     ('output', interp.get_output_details())):
    for d in details:
        qp = d.get('quantization_parameters', {})
        sc = qp.get('scales', np.array([]))
        zp = qp.get('zero_points', np.array([]))
        info = {'name': d['name'], 'shape': d['shape'].tolist(),
                'dtype': str(d['dtype']),
                'scale': float(sc[0]) if len(sc) else None,
                'zero_point': int(zp[0]) if len(zp) else None}
        quant[tag].append(info)
        print(f"  {tag}: {d['name']} shape={info['shape']} "
              f"scale={info['scale']} zp={info['zero_point']}")

exp_in = [1, IMG_SIZE, IMG_SIZE, 3]
act_in = quant['input'][0]['shape']
if act_in != exp_in:
    raise RuntimeError(f'入力形状が期待と違います: {act_in} != {exp_in}')

exp_grids = {IMG_SIZE // 16, IMG_SIZE // 32}
act_grids = {o['shape'][1] for o in quant['output']}
if act_grids != exp_grids:
    raise RuntimeError(
        f'出力グリッドが期待と違います: {sorted(act_grids)} != {sorted(exp_grids)}\n'
        ' 入力解像度と噛み合っていません (Round C と同じ不整合)。')
print(f'  OK: 入力 {act_in} / グリッド {sorted(act_grids)}')

# ---------------------------------------------------------------------------
# 6. Vela + 成果物の保存
# ---------------------------------------------------------------------------
print('\n[6/6] Vela と保存')
rc = run([sys.executable, '-m', 'pip', 'install', '-q', 'ethos-u-vela'])
os.makedirs(VELA_DIR, exist_ok=True)
rc = run(['vela', '--accelerator-config', 'ethos-u55-256',
          '--optimise', 'Performance', '--output-dir', VELA_DIR, INT8_PATH])
if rc != 0:
    raise RuntimeError(f'vela が異常終了しました (exit={rc})')

os.makedirs(OUT_DIR, exist_ok=True)
saved = []
for p in ([CFG_PATH, H5_PATH, FP32_PATH, INT8_PATH]
          + sorted(glob.glob(os.path.join(VELA_DIR, '*')))):
    if os.path.isfile(p):
        dst = os.path.join(OUT_DIR, os.path.basename(p))
        shutil.copy2(p, dst)
        saved.append((os.path.basename(p), os.path.getsize(p)))

quant_json = os.path.join(OUT_DIR, 'quantization_params.json')
with open(quant_json, 'w', encoding='utf-8') as f:
    json.dump({'input_size': IMG_SIZE,
               'anchors': m_anchors.group(1).strip() if m_anchors else None,
               **quant}, f, ensure_ascii=False, indent=2)

for name, size in saved:
    print(f'  {name}: {size / 1024:.1f} KB')
print(f'  quantization_params.json')
print(f'\n保存先: {OUT_DIR}')

# ---------------------------------------------------------------------------
# MCU 側へ反映する値
# ---------------------------------------------------------------------------
print('\n' + '=' * 70)
print('MCU 側コード更新用パラメータ')
print('=' * 70)
print(f'\n入力解像度: {IMG_SIZE}x{IMG_SIZE}x3'
      f'  (入力バッファ {IMG_SIZE * IMG_SIZE * 3:,} バイト)')
if m_anchors:
    print(f'アンカー  : {m_anchors.group(1).strip()}')
print('\n--- 量子化パラメータ (fall_detection_postprocess.h) ---')
for o in quant['output']:
    g = o['shape'][1]
    branch = 1 if g == IMG_SIZE // 16 else 0
    print(f'  {o["name"]}: {g}x{g}  -> Branch {branch}')
    print(f'    #define POSTPROC_BRANCH{branch}_SCALE       ({o["scale"]:.8f}f)')
    print(f'    #define POSTPROC_BRANCH{branch}_ZERO_POINT  ({o["zero_point"]})')
print('\n完了。')
