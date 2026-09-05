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

    struct tm utcTime;
    gmtime_r(&currentTime, &utcTime);

    char formattedTime[25];
    strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    return String(formattedTime);
}

void connectToWiFi()
{
    Serial.printf("Conectando a %s\n", wifiSsid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false, true);
    delay(500);
    WiFi.begin(wifiSsid, wifiPassword);
    Serial.println("WiFi.begin ejecutado");

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("Conectado. IP: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("No se pudo conectar a la red.");
    }
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
        }
        else
        {
            Serial.printf("fallo (estado %d). Reintentando en 5 segundos...\n", mqttClient.state());
            delay(5000);
        }
    }
}

void publishMessage(float corriente, float tension, float potencia, float kwh)
{
    String internetTimestamp = getInternetTimestamp();
    if (internetTimestamp.isEmpty())
    {
        Serial.println("No se envia: la hora NTP aun no esta sincronizada.");
        return;
    }

    String message = "{\"device\":\"" + String(device_name) +
                     "\",\"ts\":\"" + internetTimestamp + "\"" +
                     ",\"tipo\":\"medicion\",\"data\":{\"corriente\":" +     //
                     String(corriente) + ",\"tension\":" + String(tension) + //
                     ",\"potencia\":" + String(potencia) + ",\"kwh\":" + String(kwh) + "}}";
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
    delay(1000);
    Serial.printf("Motivo del reset: %d\n", esp_reset_reason());
    Serial.println("\nCliente MQTT ESP32");
    connectToWiFi();
    if (WiFi.status() == WL_CONNECTED)
    {
        synchronizeClock();
    }
    secureClient.setInsecure();
    mqttClient.setServer(mqttHost, mqttPort);
}

void loop()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        connectToWiFi();
    }

    if (!mqttClient.connected())
    {
        connectToMqtt();
    }
    mqttClient.loop();

    // cada intervalo envio las mediciones:
    if (millis() - lastMessage >= messageInterval)
    {
        publishMessage(1.05, 220.0, 0.0, 0.0);
        lastMessage = millis();
    }
}
