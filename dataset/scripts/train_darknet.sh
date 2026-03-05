#!/usr/bin/env bash
# =============================================================================
# train_darknet.sh - YOLO-Fastest v1.1 Training Script
# Issue: F-003-3
# Description: Training launcher for Nota-NetsPresso (PyTorch) or Darknet
# =============================================================================
set -euo pipefail

FRAMEWORK="${1:-pytorch}"
IMG_SIZE="${2:-192}"
EPOCHS="${3:-300}"
BATCH_SIZE="${4:-64}"

echo "============================================"
echo "YOLO-Fastest v1.1 Training Script"
echo "============================================"
echo "Framework:  ${FRAMEWORK}"
echo "Image size: ${IMG_SIZE}"
echo "Epochs:     ${EPOCHS}"
echo "Batch size: ${BATCH_SIZE}"
echo "============================================"

if [ "${FRAMEWORK}" = "pytorch" ]; then
    echo ""
    echo "[INFO] PyTorch (Nota-NetsPresso) training"
    echo ""
    echo "[PREREQUISITE] Setup:"
    echo "  git clone https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85.git"
    echo "  cd ModelZoo-YOLOFastest-for-ARM-U55-M85"
    echo "  pip install -r requirements.txt"
    echo ""
    echo "[CMD] python train.py \\"
    echo "  --data ./data/person_fall.yaml \\"
    echo "  --cfg ./models/yolo-fastest.yaml \\"
    echo "  --img ${IMG_SIZE} \\"
    echo "  --batch-size ${BATCH_SIZE} \\"
    echo "  --epochs ${EPOCHS} \\"
    echo "  --weights '' \\"
    echo "  --name person_fall_${IMG_SIZE} \\"
    echo "  --hyp data/hyps/hyp.scratch-low.yaml"
    echo ""
    echo "After training, best model: runs/train/person_fall_${IMG_SIZE}/weights/best.pt"
    echo ""
    echo "Evaluate:"
    echo "  python val.py --weights runs/train/person_fall_${IMG_SIZE}/weights/best.pt \\"
    echo "    --data ./data/person_fall.yaml --img ${IMG_SIZE} --task test"
    echo ""
    echo "Export to ONNX:"
    echo "  python export.py --weights runs/train/person_fall_${IMG_SIZE}/weights/best.pt \\"
    echo "    --img ${IMG_SIZE} --include onnx --simplify --opset 12"

elif [ "${FRAMEWORK}" = "darknet" ]; then
    echo ""
    echo "[INFO] Darknet training"
    echo ""
    echo "[PREREQUISITE] Build Darknet with GPU:"
    echo "  git clone https://github.com/dog-qiuqiu/darknet.git"
    echo "  cd darknet && make"
    echo ""
    echo "[CMD] ./darknet detector train dataset/merged/obj.data \\"
    echo "  cfg/yolo-fastest-person-192.cfg -map"
    echo ""
    echo "Evaluate:"
    echo "  ./darknet detector map dataset/merged/obj.data \\"
    echo "    cfg/yolo-fastest-person-192.cfg backup/yolo-fastest-person-192_best.weights"

else
    echo "[ERROR] Unknown framework: ${FRAMEWORK}"
    echo "Supported: pytorch, darknet"
    exit 1
fi
