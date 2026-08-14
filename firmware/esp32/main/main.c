#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "lwip/sockets.h"
#include "esp_mac.h"

static const char *TAG = "E131_DMX";

// -------------------------------------------------------------
// DMX Configuration
// -------------------------------------------------------------
#define DMX_UART_NUM      UART_NUM_1
#define DMX_TX_PIN        GPIO_NUM_4
#define DMX_RX_PIN        UART_PIN_NO_CHANGE
#define DMX_BAUD_RATE     250000
#define DMX_UNIVERSE      1
#define DMX_MAX_CHANNELS  512

// DMX packet is 513 bytes: Start Code (1 byte) + 512 channels
static uint8_t dmx_data[513] = {0};

void init_dmx_uart(void) {
    uart_config_t uart_config = {
        .baud_rate = DMX_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(DMX_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(DMX_UART_NUM, DMX_TX_PIN, DMX_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(DMX_UART_NUM, 1024, 0, 0, NULL, 0));
}

void send_dmx_packet(void) {
    // A standard DMX break is at least 92us.
    // At 250k baud, 1 bit = 4us. So 92us = 23 bits. We use 25 bit times (~100us) to be safe.
    uart_write_bytes_with_break(DMX_UART_NUM, dmx_data, sizeof(dmx_data), 25);
}

// -------------------------------------------------------------
// E1.31 Parsing
// -------------------------------------------------------------
#define E131_PORT 5568

static void e131_task(void *pvParameters) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(E131_PORT);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "E1.31 UDP Server listening on port %d for Universe %d", E131_PORT, DMX_UNIVERSE);

    uint8_t rx_buffer[1024];
    while (1) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            break;
        }

        // Basic E1.31 parsing
        if (len >= 126) {
            // Check ACN Packet Identifier
            if (memcmp(&rx_buffer[4], "ASC-E1.17\0\0\0", 12) == 0) {
                // Check root vector
                uint32_t root_vector = (rx_buffer[18] << 24) | (rx_buffer[19] << 16) | (rx_buffer[20] << 8) | rx_buffer[21];
                if (root_vector == 0x00000004) {
                    uint16_t universe = (rx_buffer[113] << 8) | rx_buffer[114];
                    if (universe == DMX_UNIVERSE) {
                        uint16_t property_value_count = (rx_buffer[123] << 8) | rx_buffer[124];
                        if (property_value_count > 0 && property_value_count <= 513) {
                            // Copy channel data. rx_buffer[125] is the start code.
                            memcpy(dmx_data, &rx_buffer[125], property_value_count);
                            send_dmx_packet();
                        }
                    }
                }
            }
        }
    }
    
    close(sock);
    vTaskDelete(NULL);
}

// -------------------------------------------------------------
// Ethernet Configuration for WT32-ETH01
// -------------------------------------------------------------
#define WT32_ETH01_MDC_PIN  23
#define WT32_ETH01_MDIO_PIN 18
#define WT32_ETH01_PHY_ADDR 1
#define WT32_ETH01_PHY_PWR  16

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}

void init_ethernet(void) {
    // Enable the PHY clock (GPIO16 on WT32-ETH01)
    gpio_reset_pin(WT32_ETH01_PHY_PWR);
    gpio_set_direction(WT32_ETH01_PHY_PWR, GPIO_MODE_OUTPUT);
    gpio_set_level(WT32_ETH01_PHY_PWR, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); // Give PHY time to power up

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);

    // Setup MAC configuration
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    
    // WT32-ETH01 specific MAC config
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_mdc_gpio_num = WT32_ETH01_MDC_PIN;
    esp32_emac_config.smi_mdio_gpio_num = WT32_ETH01_MDIO_PIN;
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    esp32_emac_config.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO; // GPIO 0 is used for clock input

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

    // Setup PHY configuration
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = WT32_ETH01_PHY_ADDR;
    phy_config.reset_gpio_num = -1; // Reset is handled by GPIO16 power toggle
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    // Attach Ethernet driver to TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting WT32-ETH01 E1.31 to DMX Node");

    // Initialize NVS (needed by WiFi/Ethernet)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_dmx_uart();
    init_ethernet();

    // Start E1.31 Task
    xTaskCreate(e131_task, "e131_task", 4096, NULL, 5, NULL);
}
