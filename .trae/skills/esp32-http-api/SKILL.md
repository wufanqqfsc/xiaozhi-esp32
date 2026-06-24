---
name: "esp32-http-api"
description: "ESP32 HTTP API reference for device status, logs, screenshots and SD card management. Invoke when user asks to query device status, manage logs/screenshots, or use HTTP API endpoints."
---

# ESP32 HTTP API Reference

ESP32 device HTTP service (port 8080) provides device status query, log management, screenshot management and more.

## Quick Reference

### Device Management

```bash
# Get device status (WiFi, SD card, memory, uptime)
curl http://<IP>:8080/api/device/status

# Get device logs (last 10KB)
curl http://<IP>:8080/api/device/logs

# Reboot device
curl -X POST http://<IP>:8080/api/device/reboot
```

### Screenshot Management

```bash
# Trigger screenshot (async)
curl -X POST http://<IP>:8080/api/sdcard/shots

# List screenshots
curl http://<IP>:8080/api/sdcard/shots

# Download screenshot
curl -o shot.jpg http://<IP>:8080/api/sdcard/shots/shot_20250624_143052.jpg

# Delete screenshot
curl -X DELETE http://<IP>:8080/api/sdcard/shots/shot_20250624_143052.jpg
```

### Log Management

```bash
# List log files
curl http://<IP>:8080/api/sdcard/logs

# Download log
curl -o boot.log http://<IP>:8080/api/sdcard/logs/xiaozhi_boot_1.log

# Delete log
curl -X DELETE http://<IP>:8080/api/sdcard/logs/xiaozhi_boot_1.log
```

### SD Card Management

```bash
# Get SD card info
curl http://<IP>:8080/api/sdcard/info

# Delete any file
curl -X DELETE http://<IP>:8080/api/sdcard/files/<filename>
```

## API Reference

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/device/status` | GET | Device status |
| `/api/device/logs` | GET | Device logs |
| `/api/device/reboot` | POST | Reboot device |
| `/api/sdcard/info` | GET | SD card info |
| `/api/sdcard/logs` | GET | Log file list |
| `/api/sdcard/logs/<filename>` | GET/DELETE | Download/delete log |
| `/api/sdcard/shots` | GET/POST | List/trigger screenshots |
| `/api/sdcard/shots/<filename>` | GET/DELETE | Download/delete screenshot |
| `/api/sdcard/files/<filename>` | DELETE | Delete any file |
| `/` | GET | Web UI |

## Response Examples

### Device Status

```json
{
  "wifi_connected": true,
  "sdcard_mounted": true,
  "log_active": true,
  "http_running": true,
  "http_port": 8080,
  "memory": {
    "free_heap": 7974688,
    "min_free_heap": 7809844
  },
  "uptime_seconds": 3600,
  "last_screenshot": "shot_20250624_143052.jpg",
  "last_error_screenshot": "error_OTA_20250624_143100.jpg"
}
```

### Screenshot List

```json
[
  {
    "name": "shot_20250624_143052.jpg",
    "size_bytes": 22048,
    "mtime": 315582252,
    "is_last": true
  },
  {
    "name": "error_OTA_20250624_143100.jpg",
    "size_bytes": 18500,
    "mtime": 315582310,
    "is_last": false
  }
]
```

## Notes

- **IP Address**: Device IP may change after reboot, set static IP on router
- **Access Timing**: HTTP service only available after WiFi connected
- **Async Screenshot**: POST /api/sdcard/shots returns immediately, screenshot in background
- **Filename Safety**: All file operations check filename for path traversal prevention

## Full Documentation

See [docs/HTTP_API.md](file:///Users/sfan/Desktop/cv/github/OpenMAIC/xiaozhi-esp32/docs/HTTP_API.md)
