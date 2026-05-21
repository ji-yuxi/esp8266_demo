#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H

// MQTT broker settings (fill these with your broker info).
#define MQTT_BROKER_HOST ""
#define MQTT_BROKER_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""

// Client and topic settings.
#define MQTT_CLIENT_ID_PREFIX "esp8266-light"
#define MQTT_UNIQUE_ID_PREFIX "esp8266_light"
#define MQTT_TOPIC_CMD "home/light/cmd"
#define MQTT_TOPIC_STATE "home/light/state"
#define MQTT_TOPIC_AVAILABILITY "home/light/availability"
#define MQTT_TOPIC_HA_STATUS "homeassistant/status"

// Home Assistant MQTT discovery settings.
#define MQTT_DISCOVERY_PREFIX "homeassistant"
#define MQTT_HA_ENTITY_NAME "ESP8266 Servo Light"
#define MQTT_HA_DEVICE_NAME "ESP8266 Light Controller"
#define MQTT_HA_DEVICE_MANUFACTURER "DIY"
#define MQTT_HA_DEVICE_MODEL "ESP-07"
#define MQTT_HA_SW_VERSION "1.0.0"

// PubSubClient's default packet buffer is too small for discovery JSON.
#define MQTT_PACKET_BUFFER_SIZE 768

// Servo pulse settings (us). Adjust if your servo range differs.
#define SERVO_ATTACH_MIN_US 500
#define SERVO_ATTACH_MAX_US 2500
#define SERVO_POS_OFF_US 500
#define SERVO_POS_CENTER_US 1500
#define SERVO_POS_ON_US 2500

// Hold time at ON/OFF position before auto return to center.
#define SERVO_ACTION_HOLD_MS 450

#endif
