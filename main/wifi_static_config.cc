#include "wifi_static_config.h"

#include <esp_log.h>
#include <esp_netif.h>
#include <lwip/ip4_addr.h>

#define TAG "WifiStatic"

void WifiStaticConfigApplyAfterStationStart() {
#if CONFIG_XIAOZHI_WIFI_USE_STATIC_IP
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        ESP_LOGW(TAG, "WIFI_STA_DEF netif not ready");
        return;
    }

    esp_netif_ip_info_t ip_info = {};
    int o1 = 0, o2 = 0, o3 = 0, o4 = 0;
    if (sscanf(CONFIG_XIAOZHI_WIFI_STATIC_IP, "%d.%d.%d.%d", &o1, &o2, &o3, &o4) != 4) {
        ESP_LOGE(TAG, "Invalid static IP: %s", CONFIG_XIAOZHI_WIFI_STATIC_IP);
        return;
    }
    IP4_ADDR(&ip_info.ip, o1, o2, o3, o4);

    int g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    if (sscanf(CONFIG_XIAOZHI_WIFI_STATIC_GATEWAY, "%d.%d.%d.%d", &g1, &g2, &g3, &g4) != 4) {
        ESP_LOGE(TAG, "Invalid gateway: %s", CONFIG_XIAOZHI_WIFI_STATIC_GATEWAY);
        return;
    }
    IP4_ADDR(&ip_info.gw, g1, g2, g3, g4);

    int m1 = 0, m2 = 0, m3 = 0, m4 = 0;
    if (sscanf(CONFIG_XIAOZHI_WIFI_STATIC_NETMASK, "%d.%d.%d.%d", &m1, &m2, &m3, &m4) != 4) {
        ESP_LOGE(TAG, "Invalid netmask: %s", CONFIG_XIAOZHI_WIFI_STATIC_NETMASK);
        return;
    }
    IP4_ADDR(&ip_info.netmask, m1, m2, m3, m4);

    esp_err_t err = esp_netif_dhcpc_stop(netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "dhcpc_stop: %s", esp_err_to_name(err));
    }
    err = esp_netif_set_ip_info(netif, &ip_info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_ip_info failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Static IP applied: %s gw=%s mask=%s",
             CONFIG_XIAOZHI_WIFI_STATIC_IP,
             CONFIG_XIAOZHI_WIFI_STATIC_GATEWAY,
             CONFIG_XIAOZHI_WIFI_STATIC_NETMASK);
#else
    (void)0;
#endif
}
