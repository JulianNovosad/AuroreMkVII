#!/usr/bin/env python3
"""
Export YOLO26n from Ultralytics PyTorch format to ONNX (end-to-end, NMS-free).
The exported model is used by Yolo26Detector (ONNX Runtime C++ inference).

Usage:
    pip install ultralytics
    python3 scripts/export_yolo26n.py

Output: models/yolo26n.onnx
"""

import os
import sys
from pathlib import Path

REPO_DIR = Path(__file__).parent.parent
MODELS_DIR = REPO_DIR / "models"
OUTPUT_PATH = MODELS_DIR / "yolo26n.onnx"

def main():
    try:
        from ultralytics import YOLO
    except ImportError:
        print("ERROR: ultralytics not installed. Run: pip install ultralytics")
        sys.exit(1)

    MODELS_DIR.mkdir(exist_ok=True)

    print("Loading YOLO26n model (will download weights if needed)...")
    model = YOLO("yolo26n.pt")

    print(f"Exporting to ONNX (end-to-end, NMS-free)...")
    # end2end=True (default): output shape [1, 300, 6] — no NMS needed
    model.export(
        format="onnx",
        imgsz=640,
        simplify=True,
        opset=17,
    )

    # Ultralytics saves alongside the .pt file; move to models/
    pt_dir = Path(model.ckpt_path).parent if hasattr(model, "ckpt_path") else Path(".")
    candidates = list(pt_dir.glob("yolo26n.onnx")) + list(Path(".").glob("yolo26n.onnx"))
    if candidates:
        src = candidates[0]
        src.rename(OUTPUT_PATH)
        print(f"Saved: {OUTPUT_PATH}")
    else:
        print(f"WARNING: Could not locate exported ONNX file — check current directory.")
        print(f"Expected output: {OUTPUT_PATH}")
        sys.exit(1)

    print(f"\nDone. Place {OUTPUT_PATH} on the RPi5 at:")
    print(f"  /home/pi/AuroreMkVII/models/yolo26n.onnx")
    print(f"\nOr update config/config.json:")
    print(f'  "vision": {{ "yolo_model_path": "{OUTPUT_PATH}" }}')

if __name__ == "__main__":
    main()
