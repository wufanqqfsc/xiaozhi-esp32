#!/usr/bin/env python3
import re
import urllib.request

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"

for page in (1, 2):
    url = f"https://mixkit.co/free-sound-effects/magic/?page={page}"
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    html = urllib.request.urlopen(req, timeout=30).read().decode("utf-8", "ignore")
    print(f"--- page {page} ---")
    for m in re.finditer(
        r"<h2[^>]*>([^<]+)</h2>.*?active_storage/sfx/(\d+)/",
        html,
        flags=re.S,
    ):
        print(f"{m.group(2)}\t{m.group(1).strip()}")
