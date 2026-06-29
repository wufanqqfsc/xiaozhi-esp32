#include "wifi_config_backup.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

#include <esp_log.h>
#include <esp_system.h>

#include <cJSON.h>
#include <ssid_manager.h>

#define TAG "WifiConfigBackup"

const char* WifiConfigBackup::kBackupFilePath = "/sdcard/wifi_config.json";

WifiConfigBackup::WifiConfigBackup() {}

WifiConfigBackup::~WifiConfigBackup() {}

WifiConfigBackup& WifiConfigBackup::GetInstance() {
    static WifiConfigBackup instance;
    return instance;
}

void WifiConfigBackup::ClearNvs() {
    ESP_LOGW(TAG, "Clearing all WiFi credentials from NVS");
    SsidManager::GetInstance().Clear();
    ESP_LOGI(TAG, "NVS WiFi credentials cleared. SD card backup preserved at %s", kBackupFilePath);
}

int WifiConfigBackup::RestoreFromSdCard() {
    FILE* fp = fopen(kBackupFilePath, "rb");
    if (fp == nullptr) {
        ESP_LOGW(TAG, "SD card backup not found: %s", kBackupFilePath);
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGW(TAG, "SD card backup file is empty");
        fclose(fp);
        return 0;
    }

    char* json_str = (char*)malloc(file_size + 1);
    if (json_str == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for backup file");
        fclose(fp);
        return 0;
    }

    size_t bytes_read = fread(json_str, 1, file_size, fp);
    fclose(fp);
    json_str[bytes_read] = '\0';

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);

    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse SD card backup JSON");
        return 0;
    }

    cJSON* networks = cJSON_GetObjectItem(root, "networks");
    if (networks == nullptr || !cJSON_IsArray(networks)) {
        ESP_LOGW(TAG, "No 'networks' array found in backup file");
        cJSON_Delete(root);
        return 0;
    }

    int restored = 0;
    int array_size = cJSON_GetArraySize(networks);
    for (int i = 0; i < array_size; i++) {
        cJSON* item = cJSON_GetArrayItem(networks, i);
        if (item == nullptr) continue;

        cJSON* ssid_json = cJSON_GetObjectItem(item, "ssid");
        cJSON* password_json = cJSON_GetObjectItem(item, "password");

        if (ssid_json == nullptr || !cJSON_IsString(ssid_json)) continue;

        const char* ssid = ssid_json->valuestring;
        const char* password = (password_json && cJSON_IsString(password_json))
                               ? password_json->valuestring : "";

        SsidManager::GetInstance().AddSsid(ssid, password);
        restored++;
        ESP_LOGI(TAG, "Restored SSID: %s", ssid);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Restored %d WiFi network(s) from SD card backup", restored);
    return restored;
}

int WifiConfigBackup::GetSdCardNetworkCount() {
    FILE* fp = fopen(kBackupFilePath, "rb");
    if (fp == nullptr) {
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fp);
        return -1;
    }

    char* json_str = (char*)malloc(file_size + 1);
    if (json_str == nullptr) {
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(json_str, 1, file_size, fp);
    fclose(fp);
    json_str[bytes_read] = '\0';

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);

    if (root == nullptr) {
        return -1;
    }

    cJSON* networks = cJSON_GetObjectItem(root, "networks");
    if (networks == nullptr || !cJSON_IsArray(networks)) {
        cJSON_Delete(root);
        return -1;
    }

    int count = cJSON_GetArraySize(networks);
    cJSON_Delete(root);
    return count;
}

bool WifiConfigBackup::HasSdCardBackup() {
    struct stat st;
    if (stat(kBackupFilePath, &st) != 0) {
        return false;
    }
    return st.st_size > 0;
}

const char* WifiConfigBackup::GetSdCardPath() const {
    return kBackupFilePath;
}

int WifiConfigBackup::AutoRestore() {
    if (!HasSdCardBackup()) {
        ESP_LOGI(TAG, "AutoRestore: no SD card backup found at %s", kBackupFilePath);
        return 0;
    }
    int count = RestoreFromSdCard();
    if (count > 0) {
        ESP_LOGI(TAG, "AutoRestore: %d WiFi network(s) restored from SD card", count);
    }
    return count;
}

int WifiConfigBackup::BackupToSdCard() {
    struct stat st;
    if (stat("/sdcard", &st) != 0) {
        ESP_LOGW(TAG, "BackupToSdCard: /sdcard not mounted");
        return -2;
    }

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssids = ssid_manager.GetSsidList();
    if (ssids.empty()) {
        ESP_LOGW(TAG, "BackupToSdCard: no SSID in NVS to backup");
        return 0;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", "1");
    cJSON_AddNumberToObject(root, "saved_at", (double)time(nullptr));
    cJSON* networks = cJSON_AddArrayToObject(root, "networks");

    int count = 0;
    for (const auto& s : ssids) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", s.ssid.c_str());
        cJSON_AddStringToObject(item, "password", s.password.c_str());
        cJSON_AddItemToArray(networks, item);
        count++;
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == nullptr) {
        ESP_LOGE(TAG, "BackupToSdCard: failed to serialize JSON");
        return -1;
    }

    FILE* fp = fopen(kBackupFilePath, "wb");
    if (fp == nullptr) {
        ESP_LOGE(TAG, "BackupToSdCard: failed to open %s for writing", kBackupFilePath);
        free(json_str);
        return -1;
    }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, fp);
    fclose(fp);
    free(json_str);

    if (written != len) {
        ESP_LOGE(TAG, "BackupToSdCard: short write (%u/%u)", (unsigned)written, (unsigned)len);
        return -1;
    }

    ESP_LOGI(TAG, "BackupToSdCard: %d WiFi network(s) backed up to %s", count, kBackupFilePath);
    return count;
}
