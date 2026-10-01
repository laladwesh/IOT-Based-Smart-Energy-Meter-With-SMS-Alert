#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

void sendSMS();

// --------------------------------------
// Hardware Pins & Setup
// --------------------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
PZEM004Tv30 pzem(Serial2, 16, 17); 

// --------------------------------------
// WiFi & MQTT Credentials
// --------------------------------------
const char* ssid = "koiwifi";
const char* password = "12345678";
const char* mqtt_server = "broker.hivemq.com"; 
const int   mqtt_port   = 1883;

const char* apiKey = "cd_aru_140426_6BeCgQ";    
const char* templateID = "101";
const char* mobileNumber = "918141712433";

WiFiClient espClient;
PubSubClient client(espClient);

// MQTT Topics
const char* tV   = "avinash/home1/voltage";
const char* tI   = "avinash/home1/current";
const char* tP   = "avinash/home1/power";
const char* tE   = "avinash/home1/energy";
const char* tF   = "avinash/home1/frequency";
const char* tPF  = "avinash/home1/pf";

// --------------------------------------
// State Variables
// --------------------------------------
bool acState = true; 
unsigned long lastStableTime = 0;
const unsigned long stabilityDelay = 2000; 
unsigned long lastMqttPublish = 0;
unsigned long lastSMS = 0;
const unsigned long smsCooldown = 60000;

// --------------------------------------
// Helper Functions
// --------------------------------------
void setup_wifi() {
  Serial.print("[WiFi] Connecting...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected!");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("[MQTT] Connecting...");
    String cid = "ESP32-Avinash-" + String(random(1000, 9999));
    if (client.connect(cid.c_str())) {
      Serial.println("OK");
    } else {
      Serial.print("Fail rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void sendSMS() {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://www.circuitdigest.cloud/api/v1/send_sms?ID=" + String(templateID);
  http.begin(secureClient, url);
  http.addHeader("Authorization", apiKey);
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"mobiles\":\"" + String(mobileNumber) + "\",\"var1\":\"Energy meter\",\"var2\":\"High Voltage\"}";
  http.POST(payload);
  http.end();
}

// --------------------------------------
// SETUP
// --------------------------------------
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17); 
  
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.print("Energy Monitor");
  delay(2000);
  lcd.clear();
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
}

// --------------------------------------
// LOOP
// --------------------------------------
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  // 1. Read Raw Sensor Data
  float rawV = pzem.voltage();
  float rawI = pzem.current();
  float rawP = pzem.power();
  float rawE = pzem.energy();
  float rawF = pzem.frequency();
  float rawPF = pzem.pf();

  // 2. INSTANT PHYSICAL DETECTION
  // If Voltage is NaN or below 10V, the AC is physically OFF right now.
  bool isPhysicallyOff = (isnan(rawV) || rawV < 10.0);

  // 3. Confirming the state (Debouncing for logs)
  if (isPhysicallyOff != !acState) { 
    if (millis() - lastStableTime > stabilityDelay) {
      acState = !isPhysicallyOff;
      if (acState) Serial.println("⚡ AC ON CONFIRMED");
      else Serial.println("⚠️ AC OFF CONFIRMED");
    }
  } else {
    lastStableTime = millis();
  }

  // 4. INSTANT ZEROING: If physically off, force 0.0 IMMEDIATELY
  // We don't wait for 'acState' here to show 0.0
  float dispV = isPhysicallyOff ? 0.0 : rawV;
  float dispI = isPhysicallyOff ? 0.0 : (isnan(rawI) ? 0.0 : rawI);
  float dispP = isPhysicallyOff ? 0.0 : (isnan(rawP) ? 0.0 : rawP);
  float dispF = isPhysicallyOff ? 0.0 : (isnan(rawF) ? 0.0 : rawF);
  float dispPF = isPhysicallyOff ? 0.0 : (isnan(rawPF) ? 0.0 : rawPF);
  float dispE = isnan(rawE) ? 0.0 : rawE; // Energy persists

  // 5. SMS Alert
  if (acState && dispV > 250 && millis() - lastSMS > smsCooldown) {
    sendSMS();
    lastSMS = millis();
  }

  // 6. Serial & MQTT Output (Timed to 2 seconds)
  if (millis() - lastMqttPublish > 2000) {
    lastMqttPublish = millis();
    
    Serial.printf("V:%.1f I:%.3f P:%.1f E:%.2f F:%.1f PF:%.2f\n", dispV, dispI, dispP, dispE, dispF, dispPF);

    client.publish(tV, String(dispV, 1).c_str());
    client.publish(tI, String(dispI, 3).c_str());
    client.publish(tP, String(dispP, 1).c_str());
    client.publish(tE, String(dispE, 2).c_str());
    client.publish(tF, String(dispF, 1).c_str());
    client.publish(tPF, String(dispPF, 2).c_str());
  }

  // 7. LCD Display
  static int screen = 0;
  static unsigned long lastLcdChange = 0;
  if (millis() - lastLcdChange > 2500) {
    lastLcdChange = millis();
    lcd.clear();
    switch(screen) {
      case 0:
        lcd.print("V:"); lcd.print(dispV, 1);
        lcd.setCursor(0, 1);
        lcd.print("I:"); lcd.print(dispI, 3);
        break;
      case 1:
        lcd.print("P:"); lcd.print(dispP, 1);
        lcd.setCursor(0, 1);
        lcd.print("E:"); lcd.print(dispE, 2);
        break;
      case 2:
        lcd.print("F:"); lcd.print(dispF, 1);
        lcd.setCursor(0, 1);
        lcd.print("PF:"); lcd.print(dispPF, 2);
        break;
    }
    screen = (screen + 1) % 3;
  }
}