 I'll search for the specific technical requirements from Coral Edge TPU and LiteRT documentation to create an accurate specification for your noise-robust MobileNetSSD model.  
 Based on the Coral Edge TPU documentation and your specific requirements, here is the comprehensive technical specification for your noise-robust MobileNetSSD model:

---

# Technical Specification: Noise-Robust MobileNetSSD for Edge TPU

## 1. Model Architecture

| Parameter | Specification | Rationale |
|-----------|--------------|-----------|
| **Backbone** | MobileNet SSD (Single Shot Detector) | Proven Edge TPU compatibility, efficient depthwise separable convolutions |
| **Feature Pyramid Network** | **Disabled** | Explicitly excluded per requirements; uses single-scale feature maps from final MobileNet layer |
| **Input Resolution** | 224×224 or 300×300 (fixed at compile-time) | No dynamic input sizes allowed |
| **Input Format** | **RGB888** (uint8, [0, 255]) | Per requirement; compiler will insert quantize op if needed |
| **Output Format** | Detection boxes + class scores + confidence | Standard SSD output tensors |

## 2. Edge TPU Compatibility Requirements

### 2.1 Quantization (Mandatory)
- **Type**: Full integer quantization (uint8/int8)
- **Method**: Post-training quantization with representative dataset OR Quantization-aware training (TF1 recommended for object detection)
- **Constraint**: All tensor parameters must be 8-bit fixed-point; no float32 ops in compiled graph

### 2.2 Tensor Constraints
| Constraint | Implementation |
|------------|---------------|
| **Static shapes only** | All tensor sizes constant at compile-time |
| **Dimensionality** | Max 3 dimensions with size >1 (innermost 3 only) |
| **No dynamic tensors** | Batch size fixed at 1; no dynamic batching |
| **Parameters constant** | All weights/biases frozen at compile-time |

### 2.3 Supported Operations (MobileNetSSD-compatible)
From Coral docs :
- `Conv2D` (same dilation x/y)
- `DepthwiseConv2D` (dilation constraints per runtime version)
- `Add`, `Mul`, `Sub` (residual connections)
- `ReLU`, `ReLU6` (activation functions)
- `MaxPool2D`, `AveragePool2D` (no fused activation)
- `Concatenation` (no fused activation)
- `Reshape`, `Squeeze`, `ExpandDims` (static shapes only)
- `Softmax` (1D input, max 16,000 elements)
- `FullyConnected` (default format only)

**Avoid**: FPN ops, dynamic shapes, unsupported activations (Swish, Mish), 5D tensors

## 3. Noise Augmentation Strategy (Training)

### 3.1 Random Pixel Noise (Your Specification)
```python
class EdgeTPUNoiseAugmentation:
    """
    Per-epoch randomized noise for robust training
    Compatible with uint8 quantization range [0, 255]
    """
    
    def __call__(self, image):
        # Coverage: 0-30% of total pixels (randomized per epoch)
        coverage = random.uniform(0.0, 0.30)
        num_pixels = int(224 * 224 * coverage)
        
        # Pixel selection: random locations, different every epoch
        h_indices = random.sample(range(224), num_pixels)
        w_indices = random.sample(range(224), num_pixels)
        
        # Channel perturbation: ±30% of RGB888 range (0-255)
        # 30% of 255 = ±76.5, rounded to int
        max_shift = 76
        
        for h, w in zip(h_indices, w_indices):
            # Independent R, G, B shifts per pixel
            r_shift = random.randint(-max_shift, max_shift)
            g_shift = random.randint(-max_shift, max_shift)  
            b_shift = random.randint(-max_shift, max_shift)
            
            image[h, w, 0] = np.clip(image[h, w, 0] + r_shift, 0, 255)
            image[h, w, 1] = np.clip(image[h, w, 1] + g_shift, 0, 255)
            image[h, w, 2] = np.clip(image[h, w, 2] + b_shift, 0, 255)
        
        return image
```

### 3.2 Additional Standard Augmentations
- **Geometric**: Random rotation (±15°), random crop, horizontal flip
- **Photometric**: Brightness (±40%), contrast (±40%), saturation
- **Order**: Geometric → Photometric → **Noise** → Quantization-aware scaling

## 4. Model Conversion Pipeline

### 4.1 TensorFlow to TFLite Conversion
Per LiteRT docs :

```python
import tensorflow as tf

converter = tf.lite.TFLiteConverter.from_saved_model(saved_model_dir)

# Mandatory: Full integer quantization for Edge TPU
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_data_gen
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.uint8   # RGB888 input
converter.inference_output_type = tf.uint8  # Or tf.float32 if needed

# CRITICAL: No dynamic shapes
converter.experimental_new_converter = True
converter.allow_custom_ops = False  # Only standard ops for Edge TPU

tflite_model = converter.convert()
```

### 4.2 Edge TPU Compilation
Per compiler docs :

```bash
# Single subgraph constraint: --num_segments=1 (default)
# No intermediate tensors (would create subgraphs)
edgetpu_compiler \
  --min_runtime_version 13 \
  --show_operations \
  -o ./compiled \
  model_quantized.tflite

# Output: model_quantized_edgetpu.tflite
```

**Compiler Constraints**:
- **No more than 1 subgraph per op**: Achieved by ensuring 100% op compatibility
- If unsupported op encountered, compiler stops and remainder runs on CPU (unacceptable per spec)
- Model must fit in ~8MB Edge TPU cache (parameter data)

## 5. On-Device Training Compatibility (Optional)

For future on-device retraining capability :

### 5.1 Weight Imprinting Setup
- Base model: MobileNet SSD feature extractor (compiled to Edge TPU)
- Final layer: Separated, runs on CPU
- L2-normalization layer required before classification head
- Architecture: `[Embedding Extractor] -> L2Norm -> Conv2D -> Mul -> Reshape -> Softmax`

### 5.2 Backpropagation Setup
- `SoftmaxRegressionModel` for final layer only
- Feature dimensions: Match MobileNet output (1024 for 1.0 width)
- Training: Last layer weights updateable via `Train()` method

## 6. Validation & Testing

### 6.1 Compilation Verification
```bash
# Check 100% Edge TPU mapping
edgetpu_compiler -s model.tflite | grep "Mapped to Edge TPU"

# Expected: All ops mapped, 0 CPU ops
```

### 6.2 Noise Robustness Testing
| Test Case | Description |
|-----------|-------------|
| Clean baseline | No noise, standard test set |
| Light noise | 5% pixel coverage, ±10% shift |
| Medium noise | 15% pixel coverage, ±20% shift |
| Heavy noise | 30% pixel coverage, ±30% shift |
| Adversarial | FGSM/PGD attacks (if security-critical) |

### 6.3 Latency Requirements
- Target: < 50ms inference on Edge TPU USB (Coral Dev Board)
- Memory: < 8MB parameter cache

## 7. File Deliverables

| File | Description |
|------|-------------|
| `mobilenetssd_noise_robot.pb` | Frozen TensorFlow graph (float) |
| `mobilenetssd_quant.tflite` | Quantized TFLite model (uint8) |
| `mobilenetssd_edgetpu.tflite` | **Final compiled model** |
| `labels.txt` | Class labels (COCO or custom) |
| `anchors.json` | SSD anchor box configurations |

## 8. Critical Constraints Checklist

- [ ] **No FPN**: Single-scale detection only
- [ ] **No dynamic tensors**: Fixed batch=1, fixed resolution
- [ ] **RGB888 input**: uint8 [0,255], mean/std scaling handled in preprocessing
- [ ] **Single subgraph**: All ops Edge TPU-compatible, no CPU fallback
- [ ] **Noise augmentation**: 0-30% coverage, ±30% RGB shift, per-epoch randomization
- [ ] **8-bit quantization**: All weights activations uint8/int8
- [ ] **Tensor dims ≤3**: No 4D+ operations with >1 in outer dims

---

**References**: Coral Edge TPU Model Requirements , LiteRT Conversion , Edge TPU Compiler , On-device Training APIs 
