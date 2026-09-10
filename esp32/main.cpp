#include <Arduino.h>
#include <esp_system.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

const char *wifiSsid = "micromicro";
const char *wifiPassword = "micromicro";
const char *mqttHost = "k1127d70.ala.us-east-1.emqxsl.com";
const int mqttPort = 8883;
const char *mqttUser = "device";
const char *mqttPassword = "device";
const char *mqttTopic = "telemetria/zapa01";
const char *device_name = "zapa01";

const unsigned long wifiTimeoutMs = 20000;
const unsigned long mqttRetryMs = 5000;

const int ACS712_PIN = 34;               // ADC1_CH6 del ESP32
const float ACS712_SENSITIVITY = 0.185f; // 5A -> 185 mV/A
const float ACS712_OFFSET_VOLT = 1.65f;  // VCC/2 con 3.3V
const float TENSION_RED = 220.0f;        // tensión asumida para cálculo de potencia

// valores globales de medicion del ACS712:
float corriente = 0;     // Valor de corriente RMS en amperios
float tension = 0;       // Valor de tensión en voltios
float potencia = 0;      // Valor de potencia en vatios
float kwh = 0;           // Valor acumulado local de energía en kWh (solo para referencia)
float kwh_intervalo = 0; // Energía del intervalo actual en kWh
float adcOffsetVoltage = ACS712_OFFSET_VOLT;

unsigned long lastEnergyMillis = 0;

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
unsigned long lastMessage = 0;
const unsigned long messageInterval = 5000;

void synchronizeClock()
{
    configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    struct tm localTime;
    Serial.print("Sincronizando hora de Argentina");
    for (int attempt = 0; attempt < 20 && !getLocalTime(&localTime, 500); attempt++)
    {
        Serial.print(".");
    }
    Serial.println();

    if (getLocalTime(&localTime, 100))
    {
        char formattedTime[24];
        strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S", &localTime);
        Serial.printf("Hora de Argentina: %s\n", formattedTime);
    }
    else
    {
        Serial.println("No se pudo sincronizar la hora.");
    }
}

String getInternetTimestamp()
{
    time_t currentTime = time(nullptr);
    if (currentTime < 1000000000)
    {
        return "";
    }

    // todo en horal local ARGENTINA!
    struct tm localTime;
    localtime_r(&currentTime, &localTime);

    char formattedTime[30];
    strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%dT%H:%M:%S-03:00", &localTime);
    return String(formattedTime);
}

bool connectToWiFi()
{
    Serial.printf("Conectando a %s (timeout %lu ms)\n", wifiSsid, wifiTimeoutMs);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true);
    delay(250);
    WiFi.begin(wifiSsid, wifiPassword);
    WiFi.setAutoReconnect(true);

    unsigned long startTime = millis();
    int lastStatus = WiFi.status();
    Serial.print("Esperando conectarse");

    while (WiFi.status() != WL_CONNECTED && millis() - startTime < wifiTimeoutMs)
    {
        delay(500);
        int currentStatus = WiFi.status();
        if (currentStatus != lastStatus)
        {
            Serial.printf("\nWiFi status: %d", currentStatus);
            lastStatus = currentStatus;
        }
        Serial.print(".");
    }

    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("Conectado. IP: ");
        Serial.println(WiFi.localIP());
        return true;
    }

    Serial.printf("No se pudo conectar a la red. Estado WiFi final: %d\n", WiFi.status());
    return false;
}

void connectToMqtt()
{
    while (!mqttClient.connected())
    {
        String clientId = "esp01-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);
        Serial.printf("Conectando a MQTT (%s)... ", mqttHost);
        if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPassword))
        {
            Serial.println("conectado");
            return;
        }

        Serial.printf("fallo (estado %d). Reintentando en %lu ms...\n", mqttClient.state(), mqttRetryMs);
        delay(mqttRetryMs);
    }
}

void publishMessage(float corriente, float tension, float potencia, float kwhIntervalo)
{
    String internetTimestamp = getInternetTimestamp();
    if (internetTimestamp.isEmpty())
    {
        Serial.println("No se envia: la hora NTP aun no esta sincronizada.");
        return;
    }

    float intervaloSegundos = messageInterval / 1000.0f;
    String message = "{\"device\":\"" + String(device_name) +
                     "\",\"ts\":\"" + internetTimestamp + "\"" +
                     ",\"tipo\":\"medicion\",\"data\":{\"corriente\":" +
                     String(corriente) + ",\"tension\":" + String(tension) +
                     ",\"potencia\":" + String(potencia) +
                     ",\"intervalo_segundos\":" + String(intervaloSegundos) +
                     ",\"kwh_intervalo\":" + String(kwhIntervalo) + "}}";
    if (mqttClient.publish(mqttTopic, message.c_str()))
    {
        Serial.printf("Mensaje enviado a %s: %s\n", mqttTopic, message.c_str());
    }
    else
    {
        Serial.println("No se pudo enviar el mensaje MQTT.");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Serial.printf("Motivo del reset: %d\n", esp_reset_reason());
    Serial.println("\nCliente MQTT ESP32");

    pinMode(ACS712_PIN, INPUT);
    analogReadResolution(12);
    analogSetPinAttenuation(ACS712_PIN, ADC_11db);

    if (!connectToWiFi())
    {
        Serial.println("WiFi no disponible. Revisar SSID y password en main.cpp.");
        Serial.println("El ESP32 seguira intentando reconexion en el loop.");
    }
    else
    {
        synchronizeClock();
    }

    secureClient.setInsecure();
    mqttClient.setServer(mqttHost, mqttPort);
    mqttClient.setKeepAlive(60);
    mqttClient.setSocketTimeout(10);
    Serial.println("\nInit listo.");
}

void MedirValoresACS712()
{
    const uint16_t sampleCount = 120;
    const uint16_t sampleDelayMs = 2;
    float rmsAccumulator = 0.0f;

    for (uint16_t i = 0; i < sampleCount; i++)
    {
        uint32_t adcValue = analogRead(ACS712_PIN);
        float sensorVoltage = (adcValue / 4095.0f) * 3.3f;
        float deltaVoltage = sensorVoltage - adcOffsetVoltage;
        rmsAccumulator += deltaVoltage * deltaVoltage;
        delay(sampleDelayMs);
    }

    float rmsVoltage = sqrtf(rmsAccumulator / sampleCount);
    corriente = fabsf(rmsVoltage / ACS712_SENSITIVITY);

    if (corriente < 0.05f)
    {
        corriente = 0.0f;
    }

    tension = TENSION_RED;
    potencia = tension * corriente;

    float intervaloSegundos = messageInterval / 1000.0f;
    kwh_intervalo = (potencia * intervaloSegundos) / 1000.0f;

    unsigned long now = millis();
    if (lastEnergyMillis == 0)
    {
        lastEnergyMillis = now;
    }

    float elapsedHours = (now - lastEnergyMillis) / 3600000.0f;
    kwh += (potencia * elapsedHours) / 1000.0f;
    lastEnergyMillis = now;
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        if (!connectToWiFi())
        {
            delay(2000);
            return;
        }
    }

    if (!mqttClient.connected())
    {
        connectToMqtt();
    }

    mqttClient.loop();

    if (millis() - lastMessage >= messageInterval)
    {
        MedirValoresACS712();
        publishMessage(corriente, tension, potencia, kwh_intervalo);
        lastMessage = millis();
    }
}
