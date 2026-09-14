"""Convert CVAT image XML to YOLO labels paired with existing raw screenshots."""

import argparse
from collections import Counter
import filecmp
import math
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

from PIL import Image
import yaml


ML_DIR = Path(__file__).resolve().parent


def convert(xml_path, split, dry_run=False):
    with (ML_DIR / "dataset.yaml").open(encoding="utf-8") as stream:
        config = yaml.safe_load(stream)
    names = config["names"]
    if isinstance(names, list):
        names = dict(enumerate(names))
    if sorted(names) != list(range(len(names))) or len(set(names.values())) != len(names):
        raise ValueError("dataset.yaml must have unique names with consecutive IDs starting at 0.")
    class_ids = {name: class_id for class_id, name in names.items()}
    dataset_root = (ML_DIR / config["path"]).resolve()
    raw_dir = dataset_root / "raw"
    image_dir = dataset_root / "images" / split
    label_dir = dataset_root / "labels" / split

    root = ET.parse(xml_path).getroot()
    images = root.findall("image")
    if root.tag != "annotations" or not images or root.find("track") is not None:
        raise ValueError("Expected a CVAT for images export with <image> entries and rectangle boxes.")

    planned = []
    seen = set()
    counts = Counter()
    for entry in images:
        relative = Path(entry.attrib["name"])
        if relative.is_absolute() or relative.drive or ".." in relative.parts:
            raise ValueError(f"Expected an image name relative to dataset/raw: {relative}")
        source = (raw_dir / relative).resolve()
        source.relative_to(raw_dir.resolve())
        label_relative = relative.with_suffix(".txt")
        label_key = str(label_relative).casefold()
        if label_key in seen:
            raise ValueError(f"Multiple images would write the same label: {label_relative}")
        seen.add(label_key)

        width, height = int(entry.attrib["width"]), int(entry.attrib["height"])
        if width <= 0 or height <= 0:
            raise ValueError(f"Invalid image dimensions: {relative}")
        with Image.open(source) as image:
            if image.size != (width, height):
                raise ValueError(
                    f"{relative}: XML size {(width, height)} differs from original {image.size}."
                )
            image.verify()

        rows = []
        for box in entry:
            if box.tag != "box":
                raise ValueError(f"{relative}: unsupported annotation <{box.tag}>; use rectangles.")
            if float(box.get("rotation", "0")) != 0:
                raise ValueError(f"{relative}: rotated rectangles require a different YOLO format.")
            label = box.attrib["label"]
            if label not in class_ids:
                raise ValueError(f"{relative}: unknown label {label!r}; check ml/dataset.yaml.")
            x1, y1, x2, y2 = (float(box.attrib[key]) for key in ("xtl", "ytl", "xbr", "ybr"))
            if not all(math.isfinite(value) for value in (x1, y1, x2, y2)):
                raise ValueError(f"{relative}: non-finite box coordinates for {label}.")
            if not (0 <= x1 < x2 <= width and 0 <= y1 < y2 <= height):
                raise ValueError(f"{relative}: invalid or out-of-bounds box for {label}.")

            # CVAT uses pixel corners; YOLO uses normalized center and size.
            values = ((x1 + x2) / (2 * width), (y1 + y2) / (2 * height),
                      (x2 - x1) / width, (y2 - y1) / height)
            rows.append(f"{class_ids[label]} " + " ".join(f"{value:.8f}" for value in values))
            counts[label] += 1

        destination = image_dir / relative
        label_path = label_dir / label_relative
        text = "\n".join(rows) + ("\n" if rows else "")
        other_split = "val" if split == "train" else "train"
        if (dataset_root / "images" / other_split / relative).exists():
            raise ValueError(f"{relative} already exists in {other_split}; keep splits separate.")
        if destination.exists() and not filecmp.cmp(source, destination, shallow=False):
            raise ValueError(f"Refusing to replace a different image: {destination}")
        if label_path.exists() and label_path.read_text(encoding="utf-8") != text:
            raise ValueError(f"Refusing to replace different annotations: {label_path}")
        planned.append((source, destination, label_path, text))

    # Validate the whole export before creating files. Originals stay in raw/.
    if not dry_run:
        for source, destination, label_path, text in planned:
            destination.parent.mkdir(parents=True, exist_ok=True)
            label_path.parent.mkdir(parents=True, exist_ok=True)
            if not destination.exists():
                shutil.copy2(source, destination)
            if not label_path.exists():
                label_path.write_text(text, encoding="utf-8")

    action = "Checked" if dry_run else "Converted"
    print(f"{action} {len(planned)} images and {sum(counts.values())} boxes for {split}.")
    print(f"Images: {image_dir}")
    print(f"Labels: {label_dir}")
    for class_id, name in sorted(names.items()):
        print(f"  {class_id}: {name}: {counts[name]} boxes")
    if split == "train" and not any((dataset_root / "labels" / "val").rglob("*.txt")):
        print("Use a separately annotated match for validation before training.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "xml", nargs="?", type=Path,
        default=ML_DIR.parent / "dataset" / "annotations.xml",
    )
    parser.add_argument("--split", choices=("train", "val"), default="train")
    parser.add_argument("--dry-run", action="store_true", help="Validate without creating files.")
    args = parser.parse_args()
    try:
        convert(args.xml, args.split, args.dry_run)
    except (OSError, ValueError, KeyError, ET.ParseError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
