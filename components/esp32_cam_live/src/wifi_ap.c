#include "wifi_ap.h"

#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_ap";

esp_err_t wifi_ap_start(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network stack init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                        "Event loop init failed");

    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(ap_netif), TAG,
                        "Unable to stop DHCP server");
    const esp_netif_ip_info_t ip_info = {
        .ip = { .addr = ESP_IP4TOADDR(192, 168, 4, 1) },
        .gw = { .addr = ESP_IP4TOADDR(192, 168, 4, 1) },
        .netmask = { .addr = ESP_IP4TOADDR(255, 255, 255, 0) },
    };
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(ap_netif, &ip_info), TAG,
                        "Unable to set AP address");
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(ap_netif), TAG,
                        "Unable to start DHCP server");

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_country_code("CN", true), TAG,
                        "Unable to set Wi-Fi country");

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = APP_WIFI_SSID,
            .password = APP_WIFI_PASSWORD,
            .ssid_len = sizeof(APP_WIFI_SSID) - 1,
            .channel = APP_WIFI_CHANNEL,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = APP_WIFI_MAX_CLIENTS,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG,
                        "Unable to set AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG,
                        "Unable to configure AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Unable to start AP");

    ESP_LOGI(TAG, "SoftAP started: SSID=%s password=%s",
             APP_WIFI_SSID, APP_WIFI_PASSWORD);
    ESP_LOGI(TAG, "Open http://192.168.4.1/");
    return ESP_OK;
}
