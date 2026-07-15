#!/usr/bin/env python3
"""Convert bundled mystical divination SFX to xiaozhi-esp32 OGG format.

Source: Mixkit #930 "Cinematic whoosh magic gust"
License: Mixkit License (free for commercial/personal use)
https://mixkit.co/free-sound-effects/cinematic-whoosh-magic-gust-930/
"""

import subprocess
import sys
from pathlib import Path

import imageio_ffmpeg

SOURCE_NAME = "zhanbu_source.wav"
SOURCE_ID = 930
SOURCE_TITLE = "Cinematic whoosh magic gust"
SOURCE_URL = "https://mixkit.co/free-sound-effects/magic/"


def convert_to_ogg(wav_path: Path, ogg_path: Path) -> None:
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    cmd = [
        ffmpeg,
        "-y",
        "-i",
        str(wav_path),
        "-af",
        "loudnorm=I=-14:TP=-1:LRA=9",
        "-acodec",
        "libopus",
        "-b:a",
        "24k",
        "-ac",
        "1",
        "-ar",
        "16000",
        "-frame_duration",
        "60",
        str(ogg_path),
    ]
    subprocess.run(cmd, check=True, capture_output=True)


def probe_duration(ogg_path: Path) -> float:
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    result = subprocess.run(
        [ffmpeg, "-hide_banner", "-i", str(ogg_path)],
        capture_output=True,
        text=True,
    )
    for line in result.stderr.splitlines():
        if "Duration:" in line:
            parts = line.split("Duration:")[1].split(",")[0].strip()
            h, m, s = parts.split(":")
            return int(h) * 3600 + int(m) * 60 + float(s)
    return 0.0


def ensure_source(root: Path) -> Path:
    source = root / "resource" / SOURCE_NAME
    if source.exists():
        return source

    import urllib.request

    url = f"https://assets.mixkit.co/active_storage/sfx/{SOURCE_ID}/{SOURCE_ID}.wav"
    print(f"Downloading {SOURCE_TITLE} from Mixkit...")
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    data = urllib.request.urlopen(req, timeout=60).read()
    source.write_bytes(data)
    return source


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    source = ensure_source(root)
    out_ogg = root / "main" / "assets" / "common" / "zhanbu.ogg"
    resource_ogg = root / "resource" / "zhanbu.ogg"

    convert_to_ogg(source, out_ogg)
    convert_to_ogg(source, resource_ogg)

    duration_ms = int(probe_duration(out_ogg) * 1000)
    print(f"Source: Mixkit #{SOURCE_ID} - {SOURCE_TITLE}")
    print(f"Generated {out_ogg} ({out_ogg.stat().st_size} bytes)")
    print(f"Duration: {duration_ms} ms")
    print(f"SOUND_INTERVAL_MS={duration_ms}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
