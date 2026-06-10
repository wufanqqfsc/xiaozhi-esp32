#!/usr/bin/env python3
"""Lightweight submodule populator with retry, github.com direct."""
import os, sys, subprocess, time, shutil

IDF_DIR = "/Users/sfan/.espressif/v5.5.4/esp-idf"
LOG = "/Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/.tools/submod.log"
os.chdir(IDF_DIR)

SUBS = {
    "components/bt/esp_ble_mesh/lib/lib": ("https://github.com/espressif/esp-ble-mesh-lib.git", "e44762384ed104a0ebc78f134eeba5f3ab648ddc"),
    "components/bt/host/nimble/nimble": ("https://github.com/espressif/esp-nimble.git", "5c1a43ca3d205c3d5183e57dedce335fb6a81155"),
    "components/cmock/CMock": ("https://github.com/ThrowTheSwitch/CMock.git", "eeecc49ce8af123cf8ad40efdb9673e37b56230f"),
    "components/esp_coex/lib": ("https://github.com/espressif/esp-coex-lib.git", "ee5cd79583c02f23e43e62931ffb55f5a4992d0f"),
    "components/esp_phy/lib": ("https://github.com/espressif/esp-phy-lib.git", "3d57415af6e4c92eff2c4c3463e20a51d7340aba"),
    "components/esp_wifi/lib": ("https://github.com/espressif/esp32-wifi-lib.git", "b87cce81803b85c3639fb14bac6c24cff6d0fbad"),
    "components/heap/tlsf": ("https://github.com/espressif/tlsf.git", "2867f6883a12920b1969ff9624c0ab0e4185c2ce"),
    "components/json/cJSON": ("https://github.com/DaveGamble/cJSON.git", "c859b25da02955fef659d658b8f324b5cde87be3"),
    "components/lwip/lwip": ("https://github.com/espressif/esp-lwip.git", "fd432e4ee2cfb7f7f1c7eb7227e0173412e7b84e"),
    "components/mbedtls/mbedtls": ("https://github.com/espressif/mbedtls.git", "ffb280bb63c78bfec1e1ab55040671768c85c923"),
    "components/openthread/lib": ("https://github.com/espressif/esp-thread-lib.git", "2e2d91a382ad6387ce77551c6adbb1514db12472"),
    "components/openthread/openthread": ("https://github.com/espressif/openthread.git", "a12ff0d0f54fd41954b45047fcdd08f302731c5f"),
    "components/protobuf-c/protobuf-c": ("https://github.com/protobuf-c/protobuf-c.git", "abc67a11c6db271bedbb9f58be85d6f4e2ea8389"),
    "components/spiffs/spiffs": ("https://github.com/pellepl/spiffs.git", "0dbb3f71c5f6fae3747a9d935372773762baf852"),
}

def has_content(path):
    if not os.path.isdir(path): return False
    for e in os.scandir(path):
        if e.name != ".git": return True
    return False

def clone_one(path, url, commit, retries=3):
    full = os.path.join(IDF_DIR, path)
    if has_content(full):
        return "SKIP"
    gd = os.path.join(full, ".git")
    if os.path.isdir(gd): shutil.rmtree(gd, ignore_errors=True)
    os.makedirs(full, exist_ok=True)
    t0 = time.time()
    r = subprocess.run(["git","init","-q"], cwd=full, capture_output=True, text=True, timeout=10)
    if r.returncode != 0: return f"FAIL init: {r.stderr[:100]}"
    subprocess.run(["git","remote","add","origin",url], cwd=full, capture_output=True, text=True, timeout=10)
    last_err = ""
    for attempt in range(1, retries+1):
        r = subprocess.run(["git","fetch","--depth=1","origin",commit], cwd=full, capture_output=True, text=True, timeout=300)
        if r.returncode == 0:
            break
        last_err = r.stderr[:200]
        time.sleep(3)
    else:
        return f"FAIL fetch: {last_err}"
    r = subprocess.run(["git","checkout","FETCH_HEAD","-q"], cwd=full, capture_output=True, text=True, timeout=30)
    if r.returncode != 0: return f"FAIL checkout: {r.stderr[:100]}"
    return f"OK ({time.time()-t0:.1f}s)"

def log(msg):
    with open(LOG, "a") as f:
        f.write(msg + "\n")
        f.flush()
    print(msg, flush=True)

log(f"[start] {time.strftime('%H:%M:%S')}")
for path, (url, commit) in SUBS.items():
    res = clone_one(path, url, commit)
    log(f"{res}: {path}")
log(f"[done] {time.strftime('%H:%M:%S')}")
