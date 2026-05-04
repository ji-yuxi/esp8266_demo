#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Servo.h>

#include "mqtt_config.h"
#include "wifi_config.h"

static WiFiClient s_wifi_client;
static PubSubClient s_mqtt_client(s_wifi_client);
static Servo s_light_servo;

static char s_mqtt_client_id[48];
static char s_mqtt_unique_id[48];
static char s_mqtt_discovery_topic[128];
static bool s_light_on = false;
static unsigned long s_last_mqtt_retry_ms = 0;
static bool s_servo_return_pending = false;
static unsigned long s_servo_return_due_ms = 0;

static const uint8_t SERVO_PIN_D4_GPIO2 = 2;
static const unsigned long MQTT_RETRY_INTERVAL_MS = 2000;

static void build_mqtt_ids(void)
{
    uint32_t chip_id = ESP.getChipId();

    snprintf(s_mqtt_client_id, sizeof(s_mqtt_client_id), "%s-%06X", MQTT_CLIENT_ID_PREFIX, chip_id);
    snprintf(s_mqtt_unique_id, sizeof(s_mqtt_unique_id), "%s_%06X", MQTT_UNIQUE_ID_PREFIX, chip_id);
    snprintf(s_mqtt_discovery_topic,
             sizeof(s_mqtt_discovery_topic),
             "%s/switch/%s/config",
             MQTT_DISCOVERY_PREFIX,
             s_mqtt_unique_id);
}

static void publish_availability(bool online)
{
    if (!s_mqtt_client.connected()) {
        return;
    }

    s_mqtt_client.publish(MQTT_TOPIC_AVAILABILITY, online ? "online" : "offline", true);
}

static void publish_light_state(void)
{
    if (!s_mqtt_client.connected()) {
        return;
    }

    const char *state = s_light_on ? "ON" : "OFF";
    s_mqtt_client.publish(MQTT_TOPIC_STATE, state, true);
}

static void publish_homeassistant_discovery(void)
{
    char payload[640];
    int written;

    if (!s_mqtt_client.connected()) {
        return;
    }

    written = snprintf(payload,
                       sizeof(payload),
                       "{"
                       "\"name\":\"%s\","
                       "\"unique_id\":\"%s\","
                       "\"command_topic\":\"%s\","
                       "\"state_topic\":\"%s\","
                       "\"availability_topic\":\"%s\","
                       "\"payload_on\":\"ON\","
                       "\"payload_off\":\"OFF\","
                       "\"state_on\":\"ON\","
                       "\"state_off\":\"OFF\","
                       "\"optimistic\":false,"
                       "\"qos\":0,"
                       "\"retain\":false,"
                       "\"device\":{"
                       "\"identifiers\":[\"%s\"],"
                       "\"name\":\"%s\","
                       "\"manufacturer\":\"%s\","
                       "\"model\":\"%s\","
                       "\"sw_version\":\"%s\""
                       "}"
                       "}",
                       MQTT_HA_ENTITY_NAME,
                       s_mqtt_unique_id,
                       MQTT_TOPIC_CMD,
                       MQTT_TOPIC_STATE,
                       MQTT_TOPIC_AVAILABILITY,
                       s_mqtt_unique_id,
                       MQTT_HA_DEVICE_NAME,
                       MQTT_HA_DEVICE_MANUFACTURER,
                       MQTT_HA_DEVICE_MODEL,
                       MQTT_HA_SW_VERSION);

    if ((written < 0) || ((size_t)written >= sizeof(payload))) {
        Serial.println("Home Assistant discovery payload is too large");
        return;
    }

    if (s_mqtt_client.publish(s_mqtt_discovery_topic, payload, true)) {
        Serial.printf("Published Home Assistant discovery: %s\r\n", s_mqtt_discovery_topic);
    } else {
        Serial.println("Failed to publish Home Assistant discovery");
    }
}

static void servo_write_us(uint16_t us)
{
    s_light_servo.writeMicroseconds(us);
}

static void set_light_state(bool turn_on)
{
    s_light_on = turn_on;
    servo_write_us(s_light_on ? SERVO_POS_ON_US : SERVO_POS_OFF_US);
    s_servo_return_due_ms = millis() + SERVO_ACTION_HOLD_MS;
    s_servo_return_pending = true;
    publish_light_state();
    Serial.printf("Light state: %s\r\n", s_light_on ? "ON" : "OFF");
}

static void trim_ascii(char *text)
{
    size_t start = 0;
    size_t end;

    if (text == NULL) {
        return;
    }

    end = strlen(text);
    while ((start < end) &&
           ((text[start] == ' ') || (text[start] == '\r') || (text[start] == '\n') || (text[start] == '\t'))) {
        ++start;
    }

    while ((end > start) &&
           ((text[end - 1] == ' ') || (text[end - 1] == '\r') || (text[end - 1] == '\n') ||
            (text[end - 1] == '\t'))) {
        --end;
    }

    if (start > 0) {
        memmove(text, text + start, end - start);
    }

    text[end - start] = '\0';
}

static void to_upper_ascii(char *text)
{
    if (text == NULL) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; ++i) {
        if ((text[i] >= 'a') && (text[i] <= 'z')) {
            text[i] = (char)(text[i] - 'a' + 'A');
        }
    }
}

static bool copy_mqtt_payload(char *buffer, size_t buffer_size, byte *payload, unsigned int length)
{
    unsigned int copy_len = length;

    if ((buffer == NULL) || (buffer_size == 0) || (payload == NULL)) {
        return false;
    }

    if (copy_len >= buffer_size) {
        copy_len = buffer_size - 1;
    }

    memcpy(buffer, payload, copy_len);
    buffer[copy_len] = '\0';
    trim_ascii(buffer);
    return true;
}

static void handle_light_command(char *cmd)
{
    to_upper_ascii(cmd);

    if ((strcmp(cmd, "ON") == 0) || (strcmp(cmd, "1") == 0)) {
        set_light_state(true);
    } else if ((strcmp(cmd, "OFF") == 0) || (strcmp(cmd, "0") == 0)) {
        set_light_state(false);
    }
}

static void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
    char message[32];

    if ((topic == NULL) || !copy_mqtt_payload(message, sizeof(message), payload, length)) {
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_CMD) == 0) {
        handle_light_command(message);
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_HA_STATUS) == 0) {
        to_upper_ascii(message);
        if (strcmp(message, "ONLINE") == 0) {
            publish_homeassistant_discovery();
            publish_availability(true);
            publish_light_state();
        }
    }
}

static void connect_wifi(void)
{
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("Connecting WiFi: %s\r\n", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.printf("\r\nWiFi connected, IP: %s\r\n", WiFi.localIP().toString().c_str());
}

static bool mqtt_connect_once(void)
{
    bool ok;

    if (s_mqtt_client_id[0] == '\0') {
        build_mqtt_ids();
    }

    if (strlen(MQTT_USERNAME) == 0) {
        ok = s_mqtt_client.connect(s_mqtt_client_id,
                                   MQTT_TOPIC_AVAILABILITY,
                                   1,
                                   true,
                                   "offline");
    } else {
        ok = s_mqtt_client.connect(s_mqtt_client_id,
                                   MQTT_USERNAME,
                                   MQTT_PASSWORD,
                                   MQTT_TOPIC_AVAILABILITY,
                                   1,
                                   true,
                                   "offline");
    }

    if (!ok) {
        Serial.printf("MQTT connect failed, rc=%d\r\n", s_mqtt_client.state());
        return false;
    }

    Serial.println("MQTT connected");
    s_mqtt_client.subscribe(MQTT_TOPIC_CMD);
    s_mqtt_client.subscribe(MQTT_TOPIC_HA_STATUS);
    publish_homeassistant_discovery();
    publish_availability(true);
    publish_light_state();
    return true;
}

static void servo_auto_return_update(void)
{
    if (!s_servo_return_pending) {
        return;
    }

    if ((long)(millis() - s_servo_return_due_ms) >= 0) {
        servo_write_us(SERVO_POS_CENTER_US);
        s_servo_return_pending = false;
        Serial.println("Servo back to center (90 deg)");
    }
}

void setup(void)
{
    Serial.begin(115200);
    delay(200);

    s_light_servo.attach(SERVO_PIN_D4_GPIO2, SERVO_ATTACH_MIN_US, SERVO_ATTACH_MAX_US);
    servo_write_us(SERVO_POS_CENTER_US);
    s_light_on = false;
    build_mqtt_ids();

    connect_wifi();

    if (!s_mqtt_client.setBufferSize(MQTT_PACKET_BUFFER_SIZE)) {
        Serial.println("Failed to resize MQTT packet buffer");
    }

    s_mqtt_client.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    s_mqtt_client.setCallback(mqtt_callback);
}

void loop(void)
{
    servo_auto_return_update();

    if (WiFi.status() != WL_CONNECTED) {
        connect_wifi();
    }

    if (!s_mqtt_client.connected()) {
        unsigned long now = millis();
        if (now - s_last_mqtt_retry_ms >= MQTT_RETRY_INTERVAL_MS) {
            s_last_mqtt_retry_ms = now;
            mqtt_connect_once();
        }
    } else {
        s_mqtt_client.loop();
    }

    delay(10);
}
