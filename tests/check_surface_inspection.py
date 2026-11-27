"""Check the seven SDK P6 images produced by novacore_surface_inspection_probe."""
import argparse
from collections import Counter
import hashlib
from pathlib import Path


def read_frame(path):
    with path.open("rb") as stream:
        if stream.readline().strip() != b"P6":
            raise ValueError(f"{path} requires an RGB P6 capture")
        header = []
        while len(header) < 3:
            line = stream.readline()
            if not line:
                raise ValueError(f"{path} has an incomplete header")
            header.extend(line.partition(b"#")[0].split())
        if header != [b"640", b"360", b"255"]:
            raise ValueError(f"{path} has unexpected dimensions or channel depth")
        pixels = stream.read()
    if len(pixels) != 640 * 360 * 3:
        raise ValueError(f"{path} is incomplete")
    return pixels


def check(directory):
    frames = {i: read_frame(directory / f"{i}.ppm") for i in (7, 11, 15, 19, 23, 27, 31)}
    if frames[7] != frames[19] or frames[7] != frames[31]:
        raise ValueError("Restoring Lit changed the static frame")
    if frames[11] != frames[27]:
        raise ValueError("Textured/shadowed and fallback world normals differ")
    expected = {11: [(188, 188, 0), (188, 255, 188)], 15: [(231, 129, 56), (64, 128, 224)],
                23: [(231, 129, 56), (255, 255, 255)]}
    for index, pixels in frames.items():
        counts = Counter(zip(pixels[0::3], pixels[1::3], pixels[2::3]))
        if counts[(255, 0, 255)] != 32 * 16:
            raise ValueError(f"HUD colour/coverage changed at {index}")
        for target in expected.get(index, []):
            # Eight-bit conversion can differ by one between implementations.
            count = sum(n for rgb, n in counts.items() if max(abs(a - b) for a, b in zip(rgb, target)) <= 1)
            if count < 8000:
                raise ValueError(f"Missing {target} surface in frame {index}: {count} pixels")
        print(index, hashlib.sha256(pixels).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    check(parser.parse_args().directory)
