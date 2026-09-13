"""Fine-tune a pretrained YOLO detector on labeled Clash Royale screenshots."""

import argparse
from pathlib import Path


ML_DIR = Path(__file__).resolve().parent
IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".webp"}


def load_dataset():
    import yaml

    config_path = ML_DIR / "dataset.yaml"
    with config_path.open(encoding="utf-8") as stream:
        data = yaml.safe_load(stream)

    # Resolve relative to this config, not the shell or Ultralytics settings.
    dataset_root = (config_path.parent / data["path"]).resolve()
    data["path"] = str(dataset_root)

    for split in ("train", "val"):
        image_dir = dataset_root / data[split]
        images = sorted(
            path for path in image_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS
        )
        if not images:
            raise ValueError(
                f"No {split} images found in {image_dir}. "
                "Label and split your screenshots first; see ml/README.md."
            )

        labeled_images = 0
        for image_path in images:
            relative_path = image_path.relative_to(dataset_root / "images")
            label_path = (dataset_root / "labels" / relative_path).with_suffix(".txt")
            if not label_path.is_file():
                raise ValueError(
                    f"Missing label: {label_path}. Export YOLO annotations first. "
                    "For a reviewed background image, use an empty .txt file."
                )
            if label_path.read_text(encoding="utf-8").strip():
                labeled_images += 1

        if not labeled_images:
            raise ValueError(f"The {split} split has no nonempty annotations.")
        print(f"{split}: {len(images)} images, {labeled_images} with annotations")

    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch", type=int, default=8)
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument(
        "--device", default=None,
        help="Use '0' for the first CUDA GPU or 'cpu'; default: automatic.",
    )
    parser.add_argument(
        "--check-data", action="store_true",
        help="Check image/label presence without downloading a model or training.",
    )
    args = parser.parse_args()
    if min(args.epochs, args.batch, args.imgsz) <= 0:
        parser.error("--epochs, --batch, and --imgsz must be positive integers.")

    try:
        data = load_dataset()
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.check_data:
        print("Dataset layout checked. YOLO will validate annotation contents during training.")
        return

    import yaml
    from ultralytics import YOLO

    runs_dir = ML_DIR / "runs"
    runs_dir.mkdir(exist_ok=True)
    resolved_config = runs_dir / "dataset.resolved.yaml"
    with resolved_config.open("w", encoding="utf-8") as stream:
        yaml.safe_dump(data, stream, sort_keys=False)

    weights_dir = ML_DIR / "weights"
    weights_dir.mkdir(exist_ok=True)
    model = YOLO(str(weights_dir / "yolo26n.pt"))
    model.train(
        data=str(resolved_config),
        epochs=args.epochs,
        batch=args.batch,
        imgsz=args.imgsz,
        device=args.device,
        workers=0,
        project=str(runs_dir),
        name="clashroyale",
        seed=42,
    )
    print(f"Best trained weights: {model.trainer.best}")


# Required on Windows when a training library starts child processes.
if __name__ == "__main__":
    main()
