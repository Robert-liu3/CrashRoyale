# YOLO Training

Fine-tune the small pretrained YOLO26n detector on your labeled screenshots.
The pretrained weights do not already recognize Clash Royale troops.

## Setup

Run these PowerShell commands from the repository root. Using the environment's
Python directly avoids needing to activate a PowerShell script.

```powershell
python -m venv ml/.venv
./ml/.venv/Scripts/python.exe -m pip install --upgrade pip
./ml/.venv/Scripts/python.exe -m pip install -r ml/requirements.txt
```

For NVIDIA GPU training, install a CUDA-enabled PyTorch build in this same
environment using the [official PyTorch instructions](https://pytorch.org/get-started/locally/).
Choose a build compatible with your Python version and driver. Check availability:

```powershell
./ml/.venv/Scripts/python.exe -c "import torch; print(torch.cuda.is_available())"
```

## Prepare the Dataset

Keep the original captures in `dataset/raw`. Annotate copies with bounding boxes
and export YOLO detection labels into this layout:

```text
dataset/
  raw/
  images/
    train/
      match01_001.png
    val/
      match02_001.png
  labels/
    train/
      match01_001.txt
    val/
      match02_001.txt
```

Match the classes in `ml/dataset.yaml` to the exact class IDs and
names used by your annotation export. Keep entire matches in one split to avoid
near-duplicate frames appearing in both training and validation. Reserve separate
matches for a later test; this starter only uses training and validation data.

Each label row describes one visible object:

```text
class_id x_center y_center width height
```

Coordinates are normalized to 0-1 relative to the image width and height. For
example, `0 0.5 0.5 0.1 0.2` describes class 0 centered in the image, with a box
10% of the image width and 20% of its height. Annotate all visible instances of
your chosen classes. Do not use this example as an actual screenshot annotation.

The starter requires a matching `.txt` file for every image to catch unfinished
labeling. Use an empty file only for a reviewed image containing none of your
chosen classes. Each split also needs some nonempty annotations. YOLO itself
permits missing files for background images, but this starter checks more strictly.

See the [official YOLO dataset format](https://docs.ultralytics.com/datasets/detect/).

## Convert a CVAT XML Export

Export annotations as **CVAT for images**. You can reuse the original screenshots
in `dataset/raw`; downloading the images from CVAT is unnecessary.

Put the XML at `dataset/annotations.xml`, then run:

```powershell
./ml/.venv/Scripts/python.exe ml/convert_cvat.py --dry-run
./ml/.venv/Scripts/python.exe ml/convert_cvat.py
```

The converter checks filenames, original image dimensions, classes, and box
bounds, then copies only XML-listed images into `dataset/images/train` and writes
normalized YOLO labels into `dataset/labels/train`. The raw screenshots and XML
are preserved. Matching output files are left alone on reruns; conflicting files
cause an error. Only rectangles from image tasks are supported.

An XML image entry without boxes produces an empty label file, so export only
reviewed images. The converter does not decide whether a box is visually correct.

Use a separate match for validation, keep its screenshot filenames unique, and
convert its XML with:

```powershell
./ml/.venv/Scripts/python.exe ml/convert_cvat.py dataset/validation.xml --split val
```

No random train/validation split is made: consecutive captures of the same match
would give misleading validation results. Training still needs both splits.

## Check and Train

Check that images and matching label files exist. This does not check box accuracy
or annotation syntax; YOLO performs its own annotation validation during training.

```powershell
./ml/.venv/Scripts/python.exe ml/train.py --check-data
```

Run a short trial first, then a longer training run:

```powershell
./ml/.venv/Scripts/python.exe ml/train.py --epochs 1
./ml/.venv/Scripts/python.exe ml/train.py --epochs 50 --batch 8 --imgsz 640
```

The default device is automatic. Add `--device 0` for CUDA or `--device cpu` for
CPU training. Reduce `--batch` if GPU memory runs out. `workers=0` keeps the first
Windows setup simple. An epoch is one pass through the training dataset.

On first training, Ultralytics downloads pretrained weights into `ml/weights`.
Training output goes into `ml/runs/clashroyale`, with numbered directories for
subsequent runs. The script prints the best model's path, typically
`ml/runs/clashroyale/weights/best.pt`.

Paths are anchored to `train.py` and `dataset.yaml`, so the script can also be
launched from `ml/`, `build/`, or another working directory. A generated YAML with
an absolute dataset path is written to `ml/runs/dataset.resolved.yaml` for YOLO.

This script trains the detector only. Tracking deployments, estimating elixir,
and exporting/integrating the trained model into C++ are later steps.
