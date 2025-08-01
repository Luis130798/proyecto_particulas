#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include <ArduinoJson.h>

// -------- CONFIGURACIÓN --------
#define DHTPIN            13
#define DHTTYPE           DHT11
#define MQ2_PIN           34
#define MQ135_PIN         35
#define LED_GREEN_PIN     25
#define LED_YELLOW_PIN    26
#define LED_RED_PIN       33
#define BUZZER_PIN        27

#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define OLED_RESET        -1

#define WIFI_SSID         "jmaitac"
#define WIFI_PASSWORD     "12345678"
#define THINGSPEAK_APIKEY "69T2P3TIZAJBX0NV"

#define TELEGRAM_BOT_TOKEN "7825542825:AAE3mPWmsr1SFIH1Tk_aYca6ezCL3jERzGg"
#define TELEGRAM_CHAT_ID   "865409686"

// -------- OBJETOS Y VARIABLES --------
DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

long   lastUpdateId    = 0;
float  lastTemp        = NAN;
float  lastHum         = NAN;
float  lastMQ2         = NAN;
float  lastMQ135       = NAN;
int    lastICA         = 0;
String lastClasificacion;
bool   alertsEnabled   = true;

// Contadores de alerta (máximo 3 envíos consecutivos)
int alertTempCount = 0;
int alertHumCount  = 0;
int alertICAcount  = 0;

// -------- PROTOTIPOS --------
void beep(int d);
void displayError();
String urlEncode(const char *msg);
void sendTelegramMessage(const String &message);
void checkTelegramCommands();
void sendThingSpeakData(float t, float h, int ica, float mq135, float mq2);

// -------- SETUP --------
void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(MQ2_PIN, INPUT);
  pinMode(MQ135_PIN, INPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED no detectada");
    while (true);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Iniciando...");
  display.display();
  beep(200); delay(100); beep(200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi conectado");
  beep(500);

  sendTelegramMessage("🤖 Sistema activado y conectado.");
}

// -------- LOOP --------
void loop() {
  checkTelegramCommands();

  // Lectura de sensores
  float t          = dht.readTemperature();
  float h          = dht.readHumidity();
  int rawMQ2       = analogRead(MQ2_PIN);
  int rawMQ135     = analogRead(MQ135_PIN);
  float voltMQ2    = rawMQ2   * (3.3f / 4095.0f);
  float voltMQ135  = rawMQ135 * (3.3f / 4095.0f);
  float avgVolt    = (voltMQ2 + voltMQ135) / 2;

  if (isnan(t) || isnan(h)) {
    Serial.println("Error DHT11");
    displayError();
    delay(2000);
    return;
  }

  // Cálculo de ICA
  int ica;
  String clasificacion;
  if      (avgVolt <= 0.80)  { ica = 50;  clasificacion = "Buena"; }
  else if (avgVolt <= 1.00)  { ica = 100; clasificacion = "Moderada"; }
  else if (avgVolt <= 1.20)  { ica = 150; clasificacion = "Dañina a sensibles"; }
  else if (avgVolt <= 1.50)  { ica = 200; clasificacion = "Dañina"; }
  else if (avgVolt <= 2.00)  { ica = 300; clasificacion = "Muy dañina"; }
  else if (avgVolt <= 2.50)  { ica = 400; clasificacion = "Peligrosa"; }
  else                       { ica = 500; clasificacion = "Peligrosa extrema"; }

  // Guardar últimos valores
  lastTemp           = t;
  lastHum            = h;
  lastMQ2            = voltMQ2;
  lastMQ135          = voltMQ135;
  lastICA            = ica;
  lastClasificacion  = clasificacion;

  // Control de LEDs
  digitalWrite(LED_GREEN_PIN,  (ica <= 50));
  digitalWrite(LED_YELLOW_PIN, (ica > 50 && ica <= 150));
  digitalWrite(LED_RED_PIN,    (ica > 150));

  // Mostrar en OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,  0);
  display.println("Datos ambientales:");
  display.printf("T:    %.1f C\n", t);
  display.printf("H:    %.1f %%\n", h);
  display.printf("MQ2:  %.2f V\n", voltMQ2);
  display.printf("MQ135:%.2f V\n", voltMQ135);
  display.printf("ICA:  %d\n", ica);
  display.printf("Clas: %s", clasificacion.c_str());
  display.display();

  // Enviar a ThingSpeak
  sendThingSpeakData(t, h, ica, voltMQ135, voltMQ2);

  // Alertas Telegram (hasta 3 veces consecutivas)
  if (alertsEnabled) {
    // Temperatura
    if (t > 35.0) {
      if (alertTempCount < 3) {
        sendTelegramMessage("🌡️ *Alerta temperatura dañina*\nT: " + String(t,1) + " °C");
        beep(300);
        alertTempCount++;
      }
    } else {
      alertTempCount = 0;
    }

    // Humedad
    if (h > 70.0) {
      if (alertHumCount < 3) {
        sendTelegramMessage("💧 *Alerta humedad dañina*\nH: " + String(h,1) + " %");
        beep(300);
        alertHumCount++;
      }
    } else {
      alertHumCount = 0;
    }

    // ICA
    if (ica > 150) {
      if (alertICAcount < 3) {
        sendTelegramMessage("⚠️ *Alerta ICA dañino*\nICA: " + String(ica) + " (" + clasificacion + ")");
        beep(300);
        alertICAcount++;
      }
    } else {
      alertICAcount = 0;
    }
  }

  delay(15000);
}

// -------- FUNCIONES AUXILIARES --------
void beep(int d) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(d);
  digitalWrite(BUZZER_PIN, LOW);
}

void displayError() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Error sensor DHT");
  display.display();
}

String urlEncode(const char *msg) {
  const char *hex = "0123456789ABCDEF";
  String encoded;
  while (*msg) {
    char c = *msg++;
    if (isalnum(c))      encoded += c;
    else if (c == ' ')   encoded += '+';
    else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0xF];
      encoded += hex[c & 0xF];
    }
  }
  return encoded;
}

void sendTelegramMessage(const String &message) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage?chat_id=" + TELEGRAM_CHAT_ID +
               "&text=" + urlEncode(message.c_str());
  http.begin(url);
  http.GET();
  http.end();
}

void sendThingSpeakData(float t, float h, int ica, float mq135, float mq2) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://api.thingspeak.com/update?api_key=" + String(THINGSPEAK_APIKEY) +
               "&field1=" + String(t) +
               "&field2=" + String(h) +
               "&field3=" + String(ica) +
               "&field4=" + String(mq135, 2) +
               "&field5=" + String(mq2,   2);
  http.begin(url);
  http.GET();
  http.end();
}

void checkTelegramCommands() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/getUpdates?offset=" + String(lastUpdateId + 1);
  http.begin(url);
  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    if (!deserializeJson(doc, payload)) {
      for (JsonObject res : doc["result"].as<JsonArray>()) {
        lastUpdateId = res["update_id"].as<long>();
        String txt = res["message"]["text"].as<String>();
        if (txt.equalsIgnoreCase("/datos")) {
          String msg = "📡 Últimos datos:\n";
          msg += "T:     " + String(lastTemp,1)   + " °C\n";
          msg += "H:     " + String(lastHum,1)    + " %\n";
          msg += "MQ2:   " + String(lastMQ2,2)    + " V\n";
          msg += "MQ135: " + String(lastMQ135,2)  + " V\n";
          msg += "ICA:   " + String(lastICA)      + "\n";
          msg += lastClasificacion;
          sendTelegramMessage(msg);
        }
        else if (txt.equalsIgnoreCase("/stop")) {
          alertsEnabled = false;
          sendTelegramMessage("⏸️ Alertas deshabilitadas.");
        }
        else if (txt.equalsIgnoreCase("/reanudar")) {
          alertsEnabled = true;
          sendTelegramMessage("▶️ Alertas reanudadas.");
        }
      }
    }
  }
  http.end();
}
