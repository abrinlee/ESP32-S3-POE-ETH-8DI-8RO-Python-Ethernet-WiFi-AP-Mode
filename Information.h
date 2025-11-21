#pragma once

// ---------------- WiFi / Host Config ----------------
#define WIFI_SSID   "Denis_Private"
#define WIFI_PASS   "89dorpcj"
#define HOSTNAME    "relayboard"

// ---------------- TCA9554 Address -------------------
#define TCA9554_ADDR 0x20

// ---------------- MQTT Configuration ----------------
#define MQTT_BROKER_IP "192.168.0.94"  // MQTT broker
#define MQTT_PORT 1883
#define MQTT_USER "mqtt_user"  // MQTT username
#define MQTT_PASS "mqtt_password"   // MQTT password
#define MQTT_CLIENT_ID "mqtt_client_id"
#define MQTT_BASE_TOPIC "relayboard"
#define MQTT_STATE_INTERVAL 59000
#define MQTT_RECONNECT_INTERVAL 60000
