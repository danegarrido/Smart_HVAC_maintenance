#include <Arduino.h>
#include <ecc608.h>
#include <led_ctrl.h>
#include <log.h>
#include <lte.h>
#include <mqtt_client.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define ANOMALY_TERMINAL PIN_PA7 // D12

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C // 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, OLED_RESET);

const char MQTT_PUB_TOPIC_FMT[] PROGMEM = "devices/%s/messages/events/";
const char MQTT_SUB_TOPIC_FMT[] PROGMEM = "devices/%s/messages/devicebound/#";

static char mqtt_sub_topic[128];
static char mqtt_pub_topic[128];

static volatile bool got_message_event = false;
static char message_topic[384];
static volatile uint16_t message_length = 0;

static void onReceive(const char* topic,
                      const uint16_t length,
                      __attribute__((unused)) const int32_t id) {
    strcpy(message_topic, topic);
    message_length = length;
    got_message_event = true;
}

bool initTopics() {
  ATCA_STATUS status = ECC608.begin();

  if (status != ATCA_SUCCESS) {
    Log.errorf(F("Failed to initialize ECC, error code: %X\r\n"), status);
    return false;
  }

  // Find the device ID and set the publish and subscription topics
  uint8_t device_id[128];
  size_t device_id_length = sizeof(device_id);

  status = ECC608.readProvisionItem(AZURE_DEVICE_ID, device_id, &device_id_length);

  if (status != ATCA_SUCCESS) {
    Log.errorf(F("Could not retrieve device ID from the ECC, error code: "
                  "%X. Please provision the device with the provision "
                  "example sketch.\r\n"),
                  status);
    return false;
  }

  sprintf_P(mqtt_sub_topic, MQTT_SUB_TOPIC_FMT, device_id);
  sprintf_P(mqtt_pub_topic, MQTT_PUB_TOPIC_FMT, device_id);

  return true;
}

void printTextDisplay(char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(msg);
  display.display();
  delay(100);
}

void setup() {
  Log.begin(115200);
  LedCtrl.begin();
  LedCtrl.startupCycle();

  pinMode(ANOMALY_TERMINAL, PIN_DIR_INPUT);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Log.error(F("SSD1306 allocation failed"));
    while (true) {}
  }

  if (!initTopics()) {
    Log.error(F("Unable to initialize the MQTT topics. Stopping..."));
    printTextDisplay("Memory error");
    while (true) {}
  }
    
  printTextDisplay("Connecting...");

  if (!Lte.begin()) {
    Log.error(F("Failed to connect to operator"));
    printTextDisplay("Connection error");
    while (true) {}
  }

  // Attempt to connect to Azure
  if (MqttClient.beginAzure()) {
    MqttClient.subscribe(mqtt_sub_topic);
    MqttClient.onReceive(onReceive);
  } else {
    printTextDisplay("Azure IoT error");
    while (true) {}
  }
}

void loop() {
  printTextDisplay("Status: running");
  if (digitalRead(ANOMALY_TERMINAL) == HIGH) {
    Log.info("anomaly detected");
    printTextDisplay("Anomaly detected!");
    delay(2000);
    const bool published_successfully = MqttClient.publish(mqtt_pub_topic, "{\"Anomaly\": true}");
    if (published_successfully) {
      Log.info(F("Published message"));
    } else {
      Log.error(F("Failed to publish\r\n"));
    }
  }

  // if (got_message_event) {
  //   printTextDisplay("Got msg");
  //   String message = MqttClient.readMessage(mqtt_sub_topic, 100);
  //   Log.info("message");

  //   // Read message will return an empty string if there were no new
  //   // messages, so anything other than that means that there were a
  //   // new message
  //   if (message != "") {
  //     Log.infof(F("Got new message: %s\r\n"), message.c_str());
  //   }

  //   got_message_event = false;
  // }
}