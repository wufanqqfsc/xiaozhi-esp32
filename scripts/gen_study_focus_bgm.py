#!/usr/bin/env python3
"""Concatenate Ogg Opus pages from UI sounds into ~5s study_focus_bgm.ogg (no ffmpeg)."""
from __future__ import annotations

import os
import struct
import sys

SAMPLE_RATE = 16000
TARGET_SECONDS = 5.0


def read_pages(data: bytes) -> list[bytes]:
    pages: list[bytes] = []
    pos = 0
    while pos + 27 <= len(data):
        if data[pos : pos + 4] != b"OggS":
            pos += 1
            continue
        seg_count = data[pos + 26]
        hdr_end = pos + 27 + seg_count
        if hdr_end > len(data):
            break
        body_len = sum(data[pos + 27 : hdr_end])
        page_end = hdr_end + body_len
        if page_end > len(data):
            break
        pages.append(data[pos:page_end])
        pos = page_end
    return pages


def page_granule(page: bytes) -> int:
    return struct.unpack("<q", page[6:14])[0]


def set_granule(page: bytearray, granule: int) -> None:
    page[6:14] = struct.pack("<q", granule)


def set_header_type(page: bytearray, header_type: int) -> None:
    page[5] = header_type


def is_audio_page(page: bytes) -> bool:
    if len(page) < 27:
        return False
    seg_count = page[26]
    hdr_end = 27 + seg_count
    if hdr_end >= len(page):
        return False
    body = page[hdr_end:]
    return not body.startswith(b"OpusHead") and not body.startswith(b"OpusTags")


def build_bgm(source_ogg: str, output_ogg: str, target_seconds: float) -> None:
    with open(source_ogg, "rb") as f:
        src = f.read()

    pages = read_pages(src)
    if len(pages) < 3:
        raise RuntimeError(f"unexpected ogg layout: {source_ogg}")

    header_pages = [bytearray(p) for p in pages if not is_audio_page(p)]
    audio_pages = [bytearray(p) for p in pages if is_audio_page(p)]
    if not audio_pages:
        raise RuntimeError("no audio pages found")

    last_granule = page_granule(audio_pages[-1])
    if last_granule <= 0:
        raise RuntimeError("invalid source granule")

    one_shot_sec = last_granule / SAMPLE_RATE
    repeat = max(1, int((target_seconds + one_shot_sec - 0.01) / one_shot_sec))

    out_pages: list[bytearray] = []
    for hp in header_pages:
        set_header_type(hp, 0x02 if hp is header_pages[0] else 0x00)
        out_pages.append(hp)

    granule = 0
    serial = struct.unpack("<I", audio_pages[0][14:18])[0]
    for r in range(repeat):
        for idx, ap in enumerate(audio_pages):
            page = bytearray(ap)
            struct.pack_into("<I", page, 14, serial)
            delta = page_granule(ap)
            if r == 0:
                granule = delta
            else:
                prev = page_granule(audio_pages[idx - 1]) if idx > 0 else 0
                granule += delta - prev
            set_granule(page, granule)
            flags = 0x00
            if r == 0 and idx == 0:
                flags = 0x00
            if r == repeat - 1 and idx == len(audio_pages) - 1:
                flags |= 0x04
            set_header_type(page, flags)
            out_pages.append(page)

    with open(output_ogg, "wb") as f:
        for p in out_pages:
            f.write(p)

    total_sec = granule / SAMPLE_RATE
    print(f"Wrote {output_ogg} ({len(out_pages)} pages, ~{total_sec:.2f}s)")


if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = os.path.join(root, "main", "assets", "common", "welcome.ogg")
    if not os.path.isfile(src):
        src = os.path.join(root, "main", "assets", "locales", "zh-CN", "welcome.ogg")
    out = os.path.join(root, "main", "assets", "common", "study_focus_bgm.ogg")
    build_bgm(src, out, TARGET_SECONDS)
