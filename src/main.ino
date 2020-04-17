#include <FastLED.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiConfig.h>
#include <WiFiUdp.h>
#include <M5Atom.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>

String mumble_host;
unsigned int mumble_port;

bool display_always_on;

bool mqtt_enabled;
bool mqtt_control_display;
String mqtt_host;
unsigned int mqtt_port;
String mqtt_on;
String mqtt_off;
String mqtt_topic;
String mqtt_topic_key;
String mqtt_topic_control;
String mqtt_topic_get;
String mqtt_topic_get_data;
bool lampstate = false;

bool display_hour_enabled;
short display_hour_on;
short display_hour_off;

const int buttonpin = 39;
const int ledpin = 27;
const int numleds = 25;
CRGB leds[numleds];
uint8_t hue = 0;
byte reqBuffer[12];
char respBuffer[24];
uint32_t connected = 0;
int lastTime = 0;

WiFiUDP udp;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

Button btn = Button(39, true, 10);

struct __attribute__ ((packed)) mumbleRequest {
	unsigned int command;
	unsigned long long ident;
};

const bool _ = false;
const bool X = true;
const bool sprites[] = {
	_, X, X, _,
	X, _, _, X,
	X, _, _, X,
	X, _, _, X,
	_, X, X, _,

	_, _, X, _,
	_, X, X, _,
	_, _, X, _,
	_, _, X, _,
	_, X, X, X,

	_, X, X, _,
	X, _, _, X,
	_, _, X, _,
	_, X, _, _,
	X, X, X, X,

	X, X, X, _,
	_, _, _, X,
	_, X, X, _,
	_, _, _, X,
	X, X, X, _,
	
	_, _, X, _,
	_, X, X, _,
	X, _, X, _,
	X, X, X, X,
	_, _, X, _,

	X, X, X, X,
	X, _, _, _,
	_, X, X, _,
	_, _, _, X,
	X, X, X, _,

	_, X, X, _,
	X, _, _, _,
	X, X, X, _,
	X, _, _, X,
	_, X, X, _,

	X, X, X, X,
	_, _, _, X,
	_, _, X, _,
	_, X, _, _,
	_, X, _, _,

	_, X, X, _,
	X, _, _, X,
	_, X, X, _,
	X, _, _, X,
	_, X, X, _,

	_, X, X, _,
	X, _, _, X,
	_, X, X, X,
	_, _, _, X,
	_, X, X, _,

	_, X, X, _,
	X, _, _, X,
	X, X, X, X,
	X, _, _, X,
	X, _, _, X,

	X, X, X, _,
	X, _, _, X,
	X, X, X, _,
	X, _, _, X,
	X, X, X, _,

	_, X, X, _,
	X, _, _, X,
	X, _, _, _,
	X, _, _, X,
	_, X, X, _,

	X, X, X, _,
	X, _, _, X,
	X, _, _, X,
	X, _, _, X,
	X, X, X, _,

	X, X, X, X,
	X, _, _, _,
	X, X, X, _,
	X, _, _, _,
	X, X, X, X,

	X, X, X, X,
	X, _, _, _,
	X, X, X, _,
	X, _, _, _,
	X, _, _, _
};

void number(uint32_t num) {
    uint32_t overflow;
	overflow = num >> 4;
    num &= 0x0F;
    if (overflow > 5) {overflow = 5;}

	for (int y=0; y<5; y++) {
		leds[20-y*5] = CRGB::Black;
		if (overflow > 0) {
			leds[20-y*5] = CHSV((hue+128)%255, 255, 255); // inverse of number hue
			overflow -= 1;
		}
		for (int x=0; x<4; x++) {
			leds[y*5+1+x] = sprites[num*5*4 + y*4 + x] ? CHSV((hue+y*5+x)%255, 255, 255) : CHSV(0, 0, 0);
		}
	}
}

void togglestate() {
    if (mqtt_topic_control.length()) {
        if (lampstate) {
            mqttClient.publish(mqtt_topic_control.c_str(), mqtt_off.c_str());
        } else {
            mqttClient.publish(mqtt_topic_control.c_str(), mqtt_on.c_str());
        }
        lampstate = !lampstate;
    } else if (!mqtt_enabled) {
        lampstate = !lampstate;
    }
}

void callback(char* topic, byte* p_payload, unsigned int p_length) {
    String payload;
    for (uint8_t i = 0; i < p_length; i++) {
        payload.concat((char)p_payload[i]);
    }
    DynamicJsonDocument doc(200);
    deserializeJson(doc, payload);

    Serial.print("Got message: ");
    Serial.print(payload);
    Serial.print(" on topic ");
    Serial.println(topic);

    if(mqtt_on.equals(doc[mqtt_topic_key].as<const char*>())) {
        lampstate = true;
    } else if(mqtt_off.equals(doc[mqtt_topic_key].as<const char*>())) {
        lampstate = false;
    }
}

boolean reconnect() {
    static long last = -5000;
    long current = millis();
    if (current - last < 5000) {
        return false;
    }
    last = current;

    Serial.println("MQTT reconnect");

    String mqttClientId = "ESP8266Client-";
    mqttClientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttClient.connect(mqttClientId.c_str())) {
        // Once connected, publish an announcement...
        // ... and resubscribe
        mqttClient.subscribe(mqtt_topic.c_str());
        if (mqtt_topic_get.length()) {
            mqttClient.publish(mqtt_topic_get.c_str(), mqtt_topic_get_data.c_str());
        }
        Serial.println("Connected!");
    }
    return mqttClient.connected();
}

void setup() {
	Serial.begin(115200);
	SPIFFS.begin(true);
	pinMode(buttonpin, INPUT);

	FastLED.addLeds < WS2812B, ledpin, GRB > (leds, numleds);
	FastLED.setBrightness(10);

	WiFiConfig.onWaitLoop = []() {
		static CHSV color(0, 255, 255);
		color.hue++;
		FastLED.showColor(color);
		if (! digitalRead(buttonpin)) WiFiConfig.portal();
		return 50;
	};
	WiFiConfig.onPortalWaitLoop = []() {
		static CHSV color(0, 255, 255);
		color.saturation--;
		FastLED.showColor(color);
	};

    mqtt_enabled = WiFiConfig.checkbox("mqtt_enabled", true, "MQTT enabled. When disabled, clicking the screen turns it off/on");
    mqtt_host = WiFiConfig.string("mqtt_host", 63, "", "MQTT server hostname / IP Address");
    mqtt_port = WiFiConfig.integer("mqtt_port", 0, 65535, 1883, "MQTT server port");
    mqtt_control_display = WiFiConfig.checkbox("mqtt_control_display", true, "MQTT state ON also sets the display on");
    mqtt_topic = WiFiConfig.string("mqtt_topic", 63, "", "Device topic, eg zigbee2mqtt/devicename");
    mqtt_topic_key = WiFiConfig.string("mqtt_topic_key", 63, "status", "JSON status key");
    mqtt_on = WiFiConfig.string("mqtt_on", 63, "ON", "JSON status 'on' value");
    mqtt_off = WiFiConfig.string("mqtt_on", 63, "OFF", "JSON status 'off' value");
    mqtt_topic_get = WiFiConfig.string("mqtt_topic_get", 63, "", "Topic to trigger status request. Leave empty if the status is retained");
    mqtt_topic_get_data = WiFiConfig.string("mqtt_topic_get_data", 63, "{\"state\":\"\"}", "Payload to send with get request");
    mqtt_topic_control = WiFiConfig.string("mqtt_topic_set", 63, "", "Topic to write new status to device. Leave empty if unneeded. eg. zigbee2mqtt/devicename/set/state");

    if (mqtt_enabled) {
        mqttClient.setServer(mqtt_host.c_str(), mqtt_port);
        mqttClient.setCallback(callback);
    } else {
        lampstate = true;
    }

    mumble_host = WiFiConfig.string("mumble_host", 63, "revspace.nl", "Mumble hostname");
    mumble_port = WiFiConfig.integer("mumble_port", 0, 65535, 64738, "Mumble port");

    display_hour_enabled = WiFiConfig.checkbox("display_hour_enabled", true, "Have the display always on between certain hours");
    display_hour_on = WiFiConfig.integer("display_hour_on", 7, "Enable the display from <i>value</i>:00");
    display_hour_off = WiFiConfig.integer("display_hour_off", 20, "Enable the display till <i>value</i>:59");

    btn.read();
    if (btn.wasPressed()) {
        WiFiConfig.portal();
    }
    WiFiConfig.connect();

    udp.begin(mumble_port);

    if (display_hour_enabled) {
        timeClient.begin();
    }
}


void loop() {
	if (millis() - lastTime > 1000) {
		lastTime = millis();
		mumbleRequest request = {
			.command = 0,
			.ident = millis()
		};

        udp.beginPacket(mumble_host.c_str(), mumble_port);
		udp.write((const uint8_t*) &request, sizeof(request));
		udp.endPacket();

		udp.parsePacket();
		udp.read(respBuffer, 24);
		// 4 bytes version
		// 8 bytes ident
		// 4 bytes connected
		// 4 bytes maximum connections
		// 4 bytes bandwidth
		connected = respBuffer[15];
		Serial.print(connected);
		Serial.println(" people are connected");
	}

	if ((mqtt_control_display && lampstate) ||
        (!mqtt_enabled && lampstate) || // Button controls display
	    (display_hour_enabled && display_hour_on <= timeClient.getHours() && timeClient.getHours() <= display_hour_off) ||
	    (!mqtt_control_display && !display_hour_enabled && mqtt_enabled)
    ) {
        number(connected);
    } else {
	    FastLED.clear();
	}
	static long lasthue = 0;
	long now = millis();
	if (now - lasthue > 25) {
        hue += 1;
        lasthue = now;
    }

    FastLED.show();

	if (mqtt_enabled) {
        if (!mqttClient.connected()) {
            reconnect();
        }
        mqttClient.loop();
    }

    if (display_hour_enabled) {
        timeClient.update();
    }

    btn.read();
    if (btn.wasPressed()) {
        togglestate();
    }
}
