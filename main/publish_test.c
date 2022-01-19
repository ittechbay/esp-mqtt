/* MQTT publish test

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "tcpip_adapter.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"



static const char *TAG = "PUBLISH_TEST";

static EventGroupHandle_t mqtt_event_group;
const static int CONNECTED_BIT = BIT0;

static esp_mqtt_client_handle_t mqtt_client = NULL;

static char *expected_data = NULL;
static char *actual_data = NULL;
static size_t expected_size = 0;
static size_t expected_published = 0;
static size_t actual_published = 0;
static int qos_test = 0;
static unsigned int gout = 0;;


#define GPIO_VCC 1

#define GPIO_SER 1
#define GPIO_OE 4 //

#define GPIO_RCLK 2 //
#define GPIO_SRCLR 6
#define GPIO_SRCLK 3 //

#define GPIO_OUTPUT_IO_0 18
#define GPIO_OUTPUT_IO_1 19
#define GPIO_OUTPUT_PIN_SEL ((1ULL << GPIO_VCC) | (1ULL << GPIO_OE) | (1ULL << GPIO_OE) | (1ULL << GPIO_SER) | (1ULL << GPIO_RCLK) | (1ULL << GPIO_SRCLR) | (1ULL << GPIO_SRCLK))



void open_port()
{
	gpio_set_level(GPIO_SER, 0);


	gpio_set_level(GPIO_SRCLK, 0);
	gpio_set_level(GPIO_RCLK, 0);

	gpio_set_level(GPIO_SRCLR, 1);
	
}
void write32(unsigned int x)
{
	int i,j;
	unsigned char array[4];
	array[3] = x&0xff;
	array[2] = (x>>8)&0xff;
	array[1] = (x>>16)&0xff;
	array[0] = (x>>24)&0xff;
	for (i = 0; i < 4; i++)
	{
		printf("out array %d\n", i);
		for (j = 0; j < 8; j++)
		{

			unsigned char a,b;
	
			a = (array[i]<<j);
			b = a&0x80;

			printf("a:%d b:%d\n", a, b);
			//vTaskDelay(300 / portTICK_RATE_MS);

			if (b == 0)
				gpio_set_level(GPIO_SER, 0);
			else
				gpio_set_level(GPIO_SER, 1);

			//vTaskDelay(1 / portTICK_RATE_MS);
			gpio_set_level(GPIO_SRCLK, 1);
			//vTaskDelay(1 / portTICK_RATE_MS);
			gpio_set_level(GPIO_SRCLK, 0);
		}
	}
	gpio_set_level(GPIO_RCLK, 1);
	//vTaskDelay(1 / portTICK_RATE_MS);
	gpio_set_level(GPIO_RCLK, 0);
	//vTaskDelay(1 / portTICK_RATE_MS);
	//printf("done\n");
}

#if CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_eclipse_org_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_EXAMPLE_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_eclipse_org_pem_start[]   asm("_binary_mqtt_eclipse_org_pem_start");
#endif
extern const uint8_t mqtt_eclipse_org_pem_end[]   asm("_binary_mqtt_eclipse_org_pem_end");

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    static int msg_id = 0;
    static int actual_len = 0;
    int gpio_index = 0;
    // your_context_t *context = event->context;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(mqtt_event_group, CONNECTED_BIT);
        msg_id = esp_mqtt_client_subscribe(client, CONFIG_EXAMPLE_SUBSCIBE_TOPIC, qos_test);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        //gpio_index = atoi(event->data);
		char tmp[10];
		memset(tmp, 0, 5);
		memcpy(tmp, event->data+1, event->data_len-1);
		gpio_index = atoi(tmp);
		if (event->data[0] == 'o')
		{

				gout |= 1<<gpio_index;
		}
		else if (event->data[0] == 'c')
		{
				gout &= ~(1<<gpio_index);
		}
        write32(gout);
        //printf("test ID=%d, total_len=%d, data_len=%d, current_data_offset=%d\n", event->msg_id, event->total_data_len, event->data_len, event->current_data_offset);
        printf("event->topic start");
        if (event->topic) {
            printf("event->topic ");
            actual_len = event->data_len;
            msg_id = event->msg_id;
        } else {
            printf("event->topic else");
            actual_len += event->data_len;
            // check consisency with msg_id across multiple data events for single msg
            if (msg_id != event->msg_id) {
                ESP_LOGI(TAG, "Wrong msg_id in chunked message %d != %d", msg_id, event->msg_id);
                //abort();
            }
        }
#if 0        
	printf("memcpy actual_data:%p data:%p current_data_offset%d data_len:%d\n", actual_data, event->data, 
                event->current_data_offset, event->data_len);
        memcpy(actual_data + event->current_data_offset, event->data, event->data_len);
        printf("memcpy after");

        if (actual_len == event->total_data_len) {
            if (0 == memcmp(actual_data, expected_data, expected_size)) {
                printf("OK!");
                memset(actual_data, 0, expected_size);
                actual_published ++;
                if (actual_published == expected_published) {
                    printf("Correct pattern received exactly x times\n");
                    ESP_LOGI(TAG, "Test finished correctly!");
                }
            } else {
                printf("FAILED!");
                //abort();
            }
        }
#endif
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}


static void mqtt_app_start(void)
{
    mqtt_event_group = xEventGroupCreate();
    const esp_mqtt_client_config_t mqtt_cfg = {
        .event_handle = mqtt_event_handler,
        .cert_pem = (const char *)mqtt_eclipse_org_pem_start,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
}

static void get_string(char *line, size_t size)
{

    int count = 0;
    while (count < size) {
        int c = fgetc(stdin);
        if (c == '\n') {
            line[count] = '\0';
            break;
        } else if (c > 0 && c < 127) {
            line[count] = c;
            ++count;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void app_main()
{
    char line[256];
    char pattern[32];
    char transport[32];
    int repeat = 0;

	gpio_config_t io_conf;
	//disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	//set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	//bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
	//disable pull-down mode
	io_conf.pull_down_en = 0;
	//disable pull-up mode
	io_conf.pull_up_en = 0;
	//configure GPIO with the given settings
	gpio_config(&io_conf);
        open_port();
		write32(0);
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    printf("mqtt_app_start/n");
    mqtt_app_start();
    printf("mqtt_app_start end/n");
#if 1
		ESP_LOGI(TAG, "[TCP transport] Startup..");
		esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_TCP_URI);
  
        xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);
        esp_mqtt_client_start(mqtt_client);
        ESP_LOGI(TAG, "Note free memory: %d bytes", esp_get_free_heap_size());
        xEventGroupWaitBits(mqtt_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
#endif
write32(0);
	while(1)
	{
		get_string(line, sizeof(line));
	}
    while (1) {
        get_string(line, sizeof(line));
        sscanf(line, "%s %s %d %d %d", transport, pattern, &repeat, &expected_published, &qos_test);
        ESP_LOGI(TAG, "PATTERN:%s REPEATED:%d PUBLISHED:%d\n", pattern, repeat, expected_published);
        int pattern_size = strlen(pattern);
        free(expected_data);
        free(actual_data);
        actual_published = 0;
        expected_size = pattern_size * repeat;
        expected_data = malloc(expected_size);
        actual_data = malloc(expected_size);
        for (int i = 0; i < repeat; i++) {
            memcpy(expected_data + i * pattern_size, pattern, pattern_size);
        }
        printf("EXPECTED STRING %.*s, SIZE:%d\n", expected_size, expected_data, expected_size);
        esp_mqtt_client_stop(mqtt_client);

        if (0 == strcmp(transport, "tcp")) {
            ESP_LOGI(TAG, "[TCP transport] Startup..");
            esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_TCP_URI);
        } else if (0 == strcmp(transport, "ssl")) {
            ESP_LOGI(TAG, "[SSL transport] Startup..");
            esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_SSL_URI);
        } else if (0 == strcmp(transport, "ws")) {
            ESP_LOGI(TAG, "[WS transport] Startup..");
            esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_WS_URI);
        } else if (0 == strcmp(transport, "wss")) {
            ESP_LOGI(TAG, "[WSS transport] Startup..");
            esp_mqtt_client_set_uri(mqtt_client, CONFIG_EXAMPLE_BROKER_WSS_URI);
        } else {
            ESP_LOGE(TAG, "Unexpected transport");
            abort();
        }
        xEventGroupClearBits(mqtt_event_group, CONNECTED_BIT);
        esp_mqtt_client_start(mqtt_client);
        ESP_LOGI(TAG, "Note free memory: %d bytes", esp_get_free_heap_size());
        xEventGroupWaitBits(mqtt_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

        for (int i = 0; i < expected_published; i++) {
            int msg_id = esp_mqtt_client_publish(mqtt_client, CONFIG_EXAMPLE_PUBLISH_TOPIC, expected_data, expected_size, qos_test, 0);
            ESP_LOGI(TAG, "[%d] Publishing...", msg_id);
        }
    }
}
