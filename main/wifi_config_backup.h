#pragma once

#include <string>

class WifiConfigBackup {
public:
    static WifiConfigBackup& GetInstance();

    void ClearNvs();
    int RestoreFromSdCard();
    int GetSdCardNetworkCount();
    bool HasSdCardBackup();
    const char* GetSdCardPath() const;

    // 将 SD 卡上的 WiFi 配置自动恢复至 NVS，返回恢复数量
    // 在 TryWifiConnect 进入配网模式前调用
    int AutoRestore();

    // 将 NVS 当前保存的所有 SSID+密码 备份到 SD 卡，返回备份条数
    // 失败返回 -1，SD 卡未挂载返回 -2
    int BackupToSdCard();

private:
    WifiConfigBackup();
    ~WifiConfigBackup();

    WifiConfigBackup(const WifiConfigBackup&) = delete;
    WifiConfigBackup& operator=(const WifiConfigBackup&) = delete;

    static const char* kBackupFilePath;
};
