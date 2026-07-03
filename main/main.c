#include <stdio.h>
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "i2c_bus.h"
#include "bme280.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_http_server.h"
#include <math.h>
#include "esp_console.h"
#include "argtable3/argtable3.h"

#define I2C_MASTER_SCL_IO   GPIO_NUM_22
#define I2C_MASTER_SDA_IO   GPIO_NUM_21
#define BOOT_BUTTON_GPIO    GPIO_NUM_0
#define I2C_MASTER_FREQ_HZ  100000
#define I2C_BME280_ADDR     BME280_I2C_ADDRESS_DEFAULT

#define RELAY GPIO_NUM_33

// Параметры сервера
#define SERVER_HOST "10.193.100.223"
#define SERVER_PORT 12345
// LED-индикатор статуса
#define STATUS_LED_GPIO     GPIO_NUM_2

// Параметры сети WiFi
#define WIFI_SSID           "GomoNatural"
#define WIFI_PASS           "natural228"
static const char *TAG = "HTTP_SERVER";
#define CONNECTED_BIT   BIT0
#define WIFI_CONFIG_DONE_BIT BIT1
#define WIFI_FAIL_BIT   BIT2
#define WIFI_STOPPED_BIT BIT3
#define WIFI_NAMESPACE "wifi_creds"

static float TEMP = 0.0f;
const float SMOOTHING = 1.0;
static bool  WORK = false;
static bool  AUTO = false;

float heat_index(float t, float h){
    t = t * 9 / 5 + 32;
    h = h;
    float Hi;
    if (t <= 40){
        Hi = t;
    }
    else if (t < 79){
        float A = -10.3 + 1.1 * t + 0.047 * h;
        Hi = A;
    }
    else{
        float B = -42.379 + 2.04901523 * t + 10.14333127 * h - 0.22475541 * t * h 
        - 6.83783 * 0.001 * t * t - 5.481717 * 0.01 * h * h + 1.22874 * 0.001 * t
        * t * h + 8.5282 * 0.0001 * t * h * h - 1.99 * 0.000001 * t * t * h * h;
        if (h <= 0.13 && t >= 80 && t <= 112){
            Hi = B - ((13 - h) / 4) * sqrt((17 - fabs(t - 95) / 17));
        }
        else if (h > 0.85 && t >= 80 && t <= 87){
            Hi = B + 0.02 * (h - 85) * (87 - t);
            //printf("%f %f %f", t, h, Hi);
        }
        else {
            Hi = B;
        }
    }
    return (Hi - 32) * 5 / 9;
}

static esp_err_t temp_hum_handler(httpd_req_t *req)
{
    char  buf[128];
    char  param_a[32];
    float a;

    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        if (httpd_query_key_value(buf, "temp", param_a, sizeof(param_a)) == ESP_OK) {
            a = strtof(param_a, NULL);
            TEMP = a;
            ESP_LOGI(TAG, "Floats received: temp=%.3f", a);
            snprintf(buf, sizeof(buf), "OK: temp=%.3f", a);
            httpd_resp_send(req, buf, strlen(buf));
            return ESP_OK;
        }
    }

    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t work_handler(httpd_req_t *req)
{
    char buf[64];
    char param_val[8];

    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        if (httpd_query_key_value(buf, "work", param_val, sizeof(param_val)) == ESP_OK) {
            bool val = (strcmp(param_val, "true") == 0 || strcmp(param_val, "1") == 0);
            WORK = val;
            ESP_LOGI(TAG, "Work received: %s", val ? "true" : "false");
            snprintf(buf, sizeof(buf), "OK: work=%s", val ? "true" : "false");
            httpd_resp_send(req, buf, strlen(buf));
            return ESP_OK;
        }
    }

    httpd_resp_send_404(req);
    return ESP_FAIL;
}

static esp_err_t auto_handler(httpd_req_t *req)
{
    char buf[64];
    char param_val[8];

    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        if (httpd_query_key_value(buf, "auto", param_val, sizeof(param_val)) == ESP_OK) {
            bool val = (strcmp(param_val, "true") == 0 || strcmp(param_val, "1") == 0);
            AUTO = val;
            ESP_LOGI(TAG, "Auto received: %s", val ? "true" : "false");
            snprintf(buf, sizeof(buf), "OK: auto=%s", val ? "true" : "false");
            httpd_resp_send(req, buf, strlen(buf));
            return ESP_OK;
        }
    }

    httpd_resp_send_404(req);
    return ESP_FAIL;
}
httpd_handle_t server = NULL;
// Регистрация URI и запуск сервера
static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_float = {
            .uri       = "/float",
            .method    = HTTP_GET,
            .handler   = temp_hum_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_float);

        httpd_uri_t uri_work = {
            .uri       = "/work",
            .method    = HTTP_GET,
            .handler   = work_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_work);

        httpd_uri_t uri_auto = {
            .uri       = "/auto",
            .method    = HTTP_GET,
            .handler   = auto_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(server, &uri_auto);

        printf("HTTP server started on port %d\n", config.server_port);
    }
}

static void stop_webserver(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
        printf("HTTP server stopped\n");
    }
}

static EventGroupHandle_t wifi_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        if (disconn->reason == WIFI_REASON_AUTH_FAIL ||
            disconn->reason == WIFI_REASON_NO_AP_FOUND ||
            disconn->reason == WIFI_REASON_ASSOC_FAIL) {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        } else {
            esp_wifi_connect();
        }
    } else if (event_id == WIFI_EVENT_STA_STOP) {
        xEventGroupSetBits(wifi_event_group, WIFI_STOPPED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}
static char g_ssid[33] = {0};      
static char g_password[65] = {0}; 
static void console_task(void *arg);
// Основная задача - подключение по WiFi
static void wifi_task(void *pvParameter) {
    esp_netif_ip_info_t ip_info;
    bool prov;
    int schet = 0;
    while (1) {
        schet++;
        prov = false;
        if (schet >= 10){
            prov = true;
        }
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group,
            CONNECTED_BIT | WIFI_FAIL_BIT,  
            pdFALSE,                     
            pdFALSE,                       
            portMAX_DELAY
        );

        if (bits & CONNECTED_BIT) {
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
            printf("Connected!\n");
            printf("IP Address:  " IPSTR "\n", IP2STR(&ip_info.ip));
            printf("Subnet mask: " IPSTR "\n", IP2STR(&ip_info.netmask));
            printf("Gateway:     " IPSTR "\n", IP2STR(&ip_info.gw));
            schet = 0;
            start_webserver(); 
            xEventGroupClearBits(wifi_event_group, WIFI_STOPPED_BIT);
            continue;
        }
        else if (bits & WIFI_FAIL_BIT) {
            xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
            printf("Reconnect...\n");
            esp_wifi_disconnect();
            esp_wifi_stop();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            xEventGroupWaitBits(
                wifi_event_group,
                WIFI_STOPPED_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY
            );
            esp_wifi_start();
            xEventGroupClearBits(wifi_event_group, WIFI_STOPPED_BIT);
        }
        if (prov){
            xEventGroupClearBits(wifi_event_group, WIFI_CONFIG_DONE_BIT);
            stop_webserver();
            esp_wifi_disconnect();
            esp_wifi_stop();
            nvs_flash_erase();
            nvs_flash_init();
            printf("Waiting for Wi-Fi credentials... Use 'set_wifi --ssid <name> --pass <pwd>'\n"); //set_wifi --ssid GomoNatural --pass 12345678
            schet = 0;
            xEventGroupWaitBits(wifi_event_group,
                                WIFI_CONFIG_DONE_BIT,
                                pdFALSE,    
                                pdTRUE,     
                                portMAX_DELAY);

            printf("Credentials received. Initializing Wi-Fi...\n");
            wifi_config_t wifi_config = {0};
            strncpy((char *)wifi_config.sta.ssid, g_ssid, sizeof(wifi_config.sta.ssid));
            strncpy((char *)wifi_config.sta.password, g_password, sizeof(wifi_config.sta.password));
            xEventGroupWaitBits(
                wifi_event_group,
                WIFI_STOPPED_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY
            );
            esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
            esp_wifi_start();
            printf("Connecting to %s\n", WIFI_SSID);
        }
    }
  }

static SemaphoreHandle_t pwm_mutex = NULL;

typedef struct {
    float temp;
    float hum;
} data_area_t;

data_area_t data = {
    .temp = 0.0,
    .hum = 0.0
};

void relay(void *pvParameters){
    float temp, hum, h_i;
    gpio_set_direction(RELAY, GPIO_MODE_INPUT_OUTPUT);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    while (true)
    {
        xSemaphoreTake(pwm_mutex, portMAX_DELAY);
        temp = data.temp;
        hum = data.hum;
        xSemaphoreGive(pwm_mutex);
        if(AUTO){
            h_i = heat_index(temp, hum);
            printf("Heat Index: %f\n", h_i);
            if (h_i > TEMP){
                gpio_set_level(RELAY, 1);
            } else if (h_i + SMOOTHING < TEMP) {
                gpio_set_level(RELAY, 0);
            }
        }
        else{
            if(WORK){
                gpio_set_level(RELAY, 1);
            }
            else{
                gpio_set_level(RELAY, 0);
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void bme(void *pvParameters){
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_bus_handle_t i2c_bus = i2c_bus_create(I2C_NUM_0, &conf);
    bme280_handle_t bme280 = bme280_create(i2c_bus, I2C_BME280_ADDR);

    bme280_default_init(bme280);

    pwm_mutex = xSemaphoreCreateMutex();

    float temperature = 0.0, humidity = 0.0;
    while (1)
    {
        if (ESP_OK == bme280_read_temperature(bme280, &temperature)) {
            //printf("Temperature: %f ", temperature);
            xSemaphoreTake(pwm_mutex, portMAX_DELAY);
            data.temp = temperature;
            xSemaphoreGive(pwm_mutex);
        }
        if (ESP_OK == bme280_read_humidity(bme280, &humidity)) {
            //printf("Humidity: %f \n", humidity);
            xSemaphoreTake(pwm_mutex, portMAX_DELAY);
            data.hum = humidity;
            xSemaphoreGive(pwm_mutex);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    bme280_delete(&bme280);
    i2c_bus_delete(&i2c_bus);
}
 

// Сохранить учётные данные в NVS
static esp_err_t save_creds_to_nvs(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    if (err == ESP_OK) {
        err = nvs_set_str(handle, "ssid", ssid);
        err = nvs_set_str(handle, "pass", pass);
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

// Загрузить учётные данные из NVS (возвращает ESP_OK, если найдены)
static esp_err_t load_creds_from_nvs(char *ssid, size_t ssid_size,
                                     char *pass, size_t pass_size)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t len = ssid_size;
    err = nvs_get_str(handle, "ssid", ssid, &len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    len = pass_size;
    err = nvs_get_str(handle, "pass", pass, &len);
    nvs_close(handle);
    return err;
}

static int set_wifi_cmd(int argc, char **argv)
{
    struct arg_str *ssid = arg_str1(NULL, "ssid", "<ssid>", "Network SSID");
    struct arg_str *pass = arg_str1(NULL, "pass", "<pass>", "Network password");
    struct arg_end *end = arg_end(2);
    void *argtable[] = {ssid, pass, end};

    int nerrors = arg_parse(argc, argv, argtable);
    if (nerrors != 0) {
        arg_print_errors(stderr, end, argv[0]);
        return 1;
    }
    
    strncpy(g_ssid, ssid->sval[0], sizeof(g_ssid) - 1);
    strncpy(g_password, pass->sval[0], sizeof(g_password) - 1);

    printf("Saved: SSID = %s, password = %s\n", g_ssid, g_password);
    if (save_creds_to_nvs(g_ssid, g_password) == ESP_OK) {
        printf("Credentials stored in NVS.\n");
    } else {
        printf("Failed to store credentials in NVS!\n");
    }
    xEventGroupSetBits(wifi_event_group, WIFI_CONFIG_DONE_BIT);
    return 0;
}

static void register_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "set_wifi",
        .help = "Store SSID and password. Usage: set_wifi --ssid <ssid> --pass <pass>",
        .hint = NULL,
        .func = &set_wifi_cmd,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void console_task(void *arg)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "smart_fan> ";
    repl_config.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    register_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_NONE);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);
    // Инициализация стека TCP/IP
    esp_netif_init();

    // Создание цикла обработки сообщений по умолчанию
    esp_event_loop_create_default();

    // Создание Wi-Fi станции
    esp_netif_create_default_wifi_sta();

    // Создание дескриптора групповых событий (определяем статус подключения Wi-Fi)
    wifi_event_group = xEventGroupCreate();

    // Инициализация драйвера Wi-Fi
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);

    // Регистрация событий в обработчике
    esp_event_handler_instance_register(WIFI_EVENT,
                    ESP_EVENT_ANY_ID,
                    &wifi_event_handler,
                    NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT,
                    IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler,
                    NULL, NULL);
    char loaded_ssid[33] = {0};
    char loaded_pass[65] = {0};
    bool has_creds = (load_creds_from_nvs(loaded_ssid, sizeof(loaded_ssid),
                                          loaded_pass, sizeof(loaded_pass)) == ESP_OK);
    wifi_config_t wifi_config = {0};
    if (has_creds){
        strcpy(g_ssid, loaded_ssid);
        strcpy(g_password, loaded_pass);
        printf("Loaded saved credentials: SSID=%s\n", g_ssid);
    }
    else{
        printf("Waiting for Wi-Fi credentials... Use 'set_wifi --ssid <name> --pass <pwd>'\n");
        xEventGroupWaitBits(wifi_event_group,
                            WIFI_CONFIG_DONE_BIT,
                            pdFALSE,    
                            pdTRUE,     
                            portMAX_DELAY);

        printf("Credentials received. Initializing Wi-Fi...\n");
    }
    strncpy((char *)wifi_config.sta.ssid, g_ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, g_password, sizeof(wifi_config.sta.password));

    // Установить Wi-Fi в режим станции (STA)
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);

    // Запустить Wi-Fi
    esp_wifi_start();
    printf("Connecting to %s\n", WIFI_SSID);
    static TaskHandle_t wifi_task_handle = NULL;
    // Запуск основной задачи
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, &wifi_task_handle);
    xTaskCreate(relay, "relay", 4096, NULL, 5, NULL);
    xTaskCreate(bme, "bme", 8192, NULL, 4, NULL);
}