"""Sample an already-running X11 preview for black/white viewport flashes."""

import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path


def metrics(rgb):
    if not rgb or len(rgb) % 3:
        raise ValueError("Expected nonempty RGB pixels")
    pixels = list(zip(rgb[0::3], rgb[1::3], rgb[2::3]))
    return {
        "black": sum(max(p) < 8 for p in pixels) / len(pixels),
        "white": sum(min(p) > 247 for p in pixels) / len(pixels),
        "colored": sum(max(p) - min(p) > 30 for p in pixels) / len(pixels),
        "mean": sum(rgb) / len(rgb),
    }


def sample(window, output, count=60, interval=0.1):
    if count < 2 or interval < 0:
        raise ValueError("Use at least two samples and a nonnegative interval")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    frames = []
    started = time.monotonic()
    for index in range(count):
        image = output / f"frame-{index:03}.png"
        subprocess.run(
            ["import", "-window", str(window), f"png:{image}"], check=True)
        # Exclude letterboxing and compositor borders, not the game viewport.
        rgb = subprocess.check_output([
            "magick", str(image), "-gravity", "center", "-crop", "80%x50%+0+0",
            "+repage", "-resize", "160x120!", "-depth", "8", "rgb:-",
        ])
        frames.append({
            "sample": index,
            "seconds": time.monotonic() - started,
            "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
            **metrics(rgb),
        })
        time.sleep(interval)
    report = {
        "window": str(window),
        "count": count,
        "seconds": time.monotonic() - started,
        "extreme_frames": [
            frame["sample"] for frame in frames
            if frame["black"] > 0.98 or frame["white"] > 0.98
        ],
        "frames": frames,
    }
    (output / "samples.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--window", required=True,
                        help="X11 window ID, e.g. from xdotool search --pid PID")
    parser.add_argument("--output", required=True)
    parser.add_argument("--count", type=int, default=60)
    parser.add_argument("--interval", type=float, default=0.1)
    args = parser.parse_args()
    report = sample(args.window, args.output, args.count, args.interval)
    print(f"{report['count']} samples in {report['seconds']:.1f}s; "
          f"black/white viewport samples={report['extreme_frames']}")
    if report["extreme_frames"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
