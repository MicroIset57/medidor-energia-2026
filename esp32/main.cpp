#include <Arduino.h>
#include <esp_system.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <TFT_eSPI.h>

// ------------------------------------------------------------------ configuracion
const char *mqttHost = "k1127d70.ala.us-east-1.emqxsl.com";
const int mqttPort = 8883;
const char *mqttUser = "device";
const char *mqttPassword = "device";
const char *mqttTopicBase = "telemetria/";
const char *nombreFabrica = "zapa01";

// Red de fabrica: se guarda sola en el primer arranque si la flash esta vacia,
// asi el equipo sigue funcionando sin pasar por el portal.
const char *wifiSsidFabrica = "micromicro";
const char *wifiPasswordFabrica = "micromicro";

const unsigned long wifiTimeoutMs = 12000; // por red; se prueban varias seguidas
const unsigned long mqttRetryMs = 5000;
const unsigned long messageInterval = 5000;
const unsigned long reintentoWifiMs = 30000; // cada cuanto reintentar desde el portal
const unsigned long graciaPortalMs = 45000;  // pausa de reintentos tras usar el portal

// GPIO34 esta ocupado en la T-Display: va al divisor que mide la bateria.
// La salida del ACS712 (0-5V) pasa por un divisor 10k/20k antes del pin.
const int ACS712_PIN = 32; // ADC1_CH4, libre en el header
const float ACS712_DIVISOR = 20.0f / (10.0f + 20.0f);
const float ACS712_SENSITIVITY = 0.185f * ACS712_DIVISOR; // 5A: 185 mV/A en el sensor

// RELE conectado al pin 25 para encender la carga.
const int RELE_PIN = 25;

// Entrada de tension (ver documentacion/circuito.jpeg): transformador 220/11.5V,
// divisor 100k/6k8, capacitor de 1uF y polarizacion a 1.65V con 100k/100k.
// El capacitor en serie con los 50k de polarizacion tambien atenua a 50 Hz.
// Si la tension mostrada no coincide con un tester, ajustar TRAFO_SECUNDARIO.
const int TENSION_PIN = 33; // ADC1_CH5, libre en el header
const float TRAFO_PRIMARIO = 220.0f;
// Nominal 11.5V; calibrado contra multimetro (220.5V reales, mostraba 237V con 11.5).
const float TRAFO_SECUNDARIO = 11.5f * 237.0f / 220.5f;
const float DIV_SERIE = 100000.0f, DIV_INFERIOR = 6800.0f;
const float POLARIZACION = 50000.0f; // 100k || 100k
const float REACTANCIA_C = 1.0f / (2.0f * PI * 50.0f * 1e-6f);
const float DIV_THEVENIN = DIV_SERIE * DIV_INFERIOR / (DIV_SERIE + DIV_INFERIOR);
const float TENSION_ESCALA =
    (TRAFO_PRIMARIO / TRAFO_SECUNDARIO) *
    ((DIV_SERIE + DIV_INFERIOR) / DIV_INFERIOR) *
    (sqrtf(powf(DIV_THEVENIN + POLARIZACION, 2) + powf(REACTANCIA_C, 2)) / POLARIZACION);

const uint32_t VENTANA_MUESTREO_MS = 200;
const float CORRIENTE_RUIDO = 0.15f;  // RMS que marca el sensor sin carga (medido)
const float CORRIENTE_MINIMA = 0.10f; // tras restar el ruido, debajo de esto es cero
const float TENSION_MINIMA = 20.0f;   // sin red el ADC lee ~1V de ruido: debajo de esto es cero

const uint8_t MAX_REDES = 5;

// ------------------------------------------------------------------ estado
float corriente = 0;        // Valor de corriente RMS en amperios
float tension = 0;          // Valor de tension en voltios
float potencia = 0;         // Valor de potencia en vatios
float kwh = 0;              // Acumulado total local, solo para mostrar
float adcOffsetVoltage = 0; // offset medido en la ultima lectura (diagnostico)

// Energia acumulada desde el ultimo envio exitoso. Sin WiFi se sigue sumando y al
// reconectar sale todo junto en un solo mensaje, con su intervalo real en segundos.
float kwhPendiente = 0;
float segundosPendientes = 0;

unsigned long ultimaMedicionMs = 0;
unsigned long lastMessage = 0;
unsigned long ultimoIntentoMqtt = 0;
unsigned long ultimoIntentoWifi = 0;

String redSsid[MAX_REDES];
String redPass[MAX_REDES];
uint8_t cantidadRedes = 0;

bool portalActivo = false;
unsigned long ultimoClienteMs = 0; // ultima vez que hubo alguien asociado al AP
String apSsid;
String redesEscaneadas; // <option> cacheados del ultimo escaneo

String deviceName; // editable desde el portal, persiste en flash
String mqttTopic;  // telemetria/<deviceName>, se rearma al renombrar

Preferences prefs;
WebServer server(80);
WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
TFT_eSPI tft = TFT_eSPI();

// ------------------------------------------------------------------ redes en flash
void GuardarRedes()
{
    prefs.begin("wifi", false);
    prefs.putUChar("n", cantidadRedes);
    for (uint8_t i = 0; i < cantidadRedes; i++)
    {
        prefs.putString(("s" + String(i)).c_str(), redSsid[i]);
        prefs.putString(("p" + String(i)).c_str(), redPass[i]);
    }
    prefs.end();
}

void CargarRedes()
{
    prefs.begin("wifi", true);
    cantidadRedes = prefs.getUChar("n", 0);
    if (cantidadRedes > MAX_REDES)
    {
        cantidadRedes = MAX_REDES;
    }
    for (uint8_t i = 0; i < cantidadRedes; i++)
    {
        redSsid[i] = prefs.getString(("s" + String(i)).c_str(), "");
        redPass[i] = prefs.getString(("p" + String(i)).c_str(), "");
    }
    prefs.end();

    if (cantidadRedes == 0 && strlen(wifiSsidFabrica) > 0)
    {
        redSsid[0] = wifiSsidFabrica;
        redPass[0] = wifiPasswordFabrica;
        cantidadRedes = 1;
        GuardarRedes();
    }
}

// Si el SSID ya estaba guardado se actualiza la clave en lugar de duplicarlo.
void AgregarRed(const String &ssid, const String &pass)
{
    for (uint8_t i = 0; i < cantidadRedes; i++)
    {
        if (redSsid[i] == ssid)
        {
            redPass[i] = pass;
            GuardarRedes();
            return;
        }
    }

    if (cantidadRedes == MAX_REDES)
    {
        // Lista llena: se descarta la mas vieja para hacer lugar.
        for (uint8_t i = 0; i < MAX_REDES - 1; i++)
        {
            redSsid[i] = redSsid[i + 1];
            redPass[i] = redPass[i + 1];
        }
        cantidadRedes--;
    }

    redSsid[cantidadRedes] = ssid;
    redPass[cantidadRedes] = pass;
    cantidadRedes++;
    GuardarRedes();
}

void BorrarRed(uint8_t indice)
{
    if (indice >= cantidadRedes)
    {
        return;
    }
    for (uint8_t i = indice; i < cantidadRedes - 1; i++)
    {
        redSsid[i] = redSsid[i + 1];
        redPass[i] = redPass[i + 1];
    }
    cantidadRedes--;
    GuardarRedes();
}

// ------------------------------------------------------------------ display
const int Y_DATOS = 33; // arriba queda el encabezado: fecha/hora y nombre

// Encabezado: fecha y hora, y debajo el nombre (que puede cambiar desde el portal).
void DibujarEncabezado()
{
    char fechaHora[20] = "--/--/-- --:--";
    time_t ahora = time(nullptr);
    if (ahora >= 1000000000) // hora NTP ya sincronizada
    {
        struct tm localTime;
        localtime_r(&ahora, &localTime);
        strftime(fechaHora, sizeof(fechaHora), "%d/%m/%y %H:%M", &localTime);
    }

    tft.fillRect(0, 0, 145, Y_DATOS, TFT_BLACK); // hasta 145: a la derecha van WiFi/MQTT
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(fechaHora, 5, 0, 2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(deviceName, 5, 16, 2);
}

// Mensaje centrado en el area de datos mientras el equipo arranca o reconecta.
// Con el portal abierto no se dibuja: taparia las instrucciones de conexion.
void MostrarEstado(const String &linea1, const String &linea2 = "")
{
    if (portalActivo)
    {
        return;
    }

    tft.fillRect(0, Y_DATOS, 240, 135 - Y_DATOS, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);

    int fuente = tft.textWidth(linea1, 4) <= 230 ? 4 : 2;
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(linea1, 120, linea2.isEmpty() ? 84 : 74, fuente);

    if (!linea2.isEmpty())
    {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawString(linea2, 120, 102, 2);
    }

    tft.setTextDatum(TL_DATUM);
}

void IniciarDisplay()
{
    tft.init();
    tft.setRotation(3); // 240x135, apaisado
    tft.fillScreen(TFT_BLACK);
    DibujarEncabezado();
    MostrarEstado("Inicializando...");
}

void MostrarPortal()
{
    DibujarEncabezado();
    tft.fillRect(145, 0, 95, Y_DATOS, TFT_BLACK); // borra los indicadores WiFi/MQTT viejos
    tft.fillRect(0, Y_DATOS, 240, 135 - Y_DATOS, TFT_BLACK);

    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawString("SIN WIFI", 8, 35, 2);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Conectese a la red :", 8, 51, 2);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(apSsid, 8, 67, 4);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("y abra 192.168.4.1", 8, 95, 2);

    // Se aclara que la energia no se pierde mientras no hay red.
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("pend: " + String(kwhPendiente, 3) + " kWh", 8, 116, 2);
}

void ActualizarDisplay()
{
    if (portalActivo)
    {
        MostrarPortal();
        return;
    }

    DibujarEncabezado(); // la hora avanza en cada actualizacion
    tft.fillRect(0, Y_DATOS, 240, 135 - Y_DATOS, TFT_BLACK);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString(String(potencia, 0) + " W", 120, 62, 4);
    tft.setTextSize(1);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(tension, 0) + " V", 8, 92, 4);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(String(corriente, 2) + " A", 232, 92, 4);

    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString(String(kwh, 3) + " kWh", 120, 135, 2);
    tft.setTextDatum(TL_DATUM);

    // Estado de conexion, arriba a la derecha junto al nombre del equipo.
    bool wifiOk = WiFi.status() == WL_CONNECTED;
    tft.setTextColor(wifiOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.drawString(wifiOk ? "WiFi" : "----", 150, 3, 2);

    bool mqttOk = mqttClient.connected();
    tft.setTextColor(mqttOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.drawString(mqttOk ? "MQTT" : "----", 195, 3, 2);
}

// ------------------------------------------------------------------ nombre
// El nombre va dentro del topic MQTT, asi que no puede llevar los caracteres que
// MQTT reserva (/ + #) ni espacios: se reemplazan por guion.
String LimpiarNombre(const String &crudo)
{
    String limpio;
    for (uint16_t i = 0; i < crudo.length() && limpio.length() < 20; i++)
    {
        char c = crudo[i];
        bool valido = isalnum(c) || c == '-' || c == '_';
        limpio += valido ? c : '-';
    }
    limpio.trim();
    return limpio.length() > 0 ? limpio : String(nombreFabrica);
}

void AplicarNombre(const String &nombre)
{
    deviceName = nombre;
    mqttTopic = String(mqttTopicBase) + deviceName;
}

void CargarNombre()
{
    prefs.begin("cfg", true);
    String guardado = prefs.getString("nombre", nombreFabrica);
    prefs.end();
    AplicarNombre(LimpiarNombre(guardado));
}

void GuardarNombre(const String &crudo)
{
    AplicarNombre(LimpiarNombre(crudo));

    prefs.begin("cfg", false);
    prefs.putString("nombre", deviceName);
    prefs.end();

    DibujarEncabezado();
    Serial.printf("Nombre: %s  topic: %s\n", deviceName.c_str(), mqttTopic.c_str());
}

// ------------------------------------------------------------------ hora
void SincronizarHora()
{
    MostrarEstado("Sincronizando", "hora NTP");
    configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    struct tm localTime;
    Serial.print("Sincronizando hora");
    for (int intento = 0; intento < 20 && !getLocalTime(&localTime, 500); intento++)
    {
        Serial.print(".");
    }
    Serial.println();
}

String ObtenerTimestamp()
{
    time_t ahora = time(nullptr);
    if (ahora < 1000000000)
    {
        return "";
    }

    // todo en hora local ARGENTINA!
    struct tm localTime;
    localtime_r(&ahora, &localTime);

    char texto[30];
    strftime(texto, sizeof(texto), "%Y-%m-%dT%H:%M:%S-03:00", &localTime);
    return String(texto);
}

// ------------------------------------------------------------------ portal AP
// El ESP32 tiene una sola radio: escanear la obliga a saltar de canal unos segundos
// y desasocia a los clientes del AP. Por eso se escanea aparte y se cachea el
// resultado, en vez de hacerlo en cada carga de la pagina.
void EscanearRedes()
{
    redesEscaneadas = "";
    int encontradas = WiFi.scanNetworks();
    for (int i = 0; i < encontradas; i++)
    {
        redesEscaneadas += "<option>" + WiFi.SSID(i) + "</option>";
    }
    WiFi.scanDelete();

    if (encontradas <= 0)
    {
        redesEscaneadas = F("<option>(no se encontraron redes)</option>");
    }
}

String PaginaPrincipal()
{
    String h = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                 "<style>body{font-family:sans-serif;margin:16px;max-width:420px}"
                 "select,input{width:100%;padding:8px;margin:4px 0;box-sizing:border-box}"
                 "button{padding:10px 16px;width:100%}li{margin:6px 0}"
                 "a{color:#c00;text-decoration:none}</style><h2>Medidor ");
    h += deviceName;

    h += F("</h2><h3>Nombre del dispositivo</h3>"
           "<form method=POST action=/nombre>"
           "<input name=nombre maxlength=20 value='");
    h += deviceName;
    h += F("'><button>Renombrar</button></form>"
           "<p style='font-size:.85em;color:#666'>Se publica en <code>");
    h += mqttTopic;
    h += F("</code></p>");

    h += F("<h3>Redes guardadas</h3><ul>");
    if (cantidadRedes == 0)
    {
        h += F("<li>ninguna</li>");
    }
    for (uint8_t i = 0; i < cantidadRedes; i++)
    {
        h += "<li>" + redSsid[i] + " <a href='/borrar?i=" + String(i) + "'>[borrar]</a></li>";
    }

    h += F("</ul><h3>Agregar red</h3><form method=POST action=/guardar><select name=ssid>");
    h += redesEscaneadas; // cacheado: escanear aca voltearia al cliente del AP

    h += F("</select><input name=pass placeholder=Contrasena>"
           "<button>Guardar y conectar</button></form>"
           "<p><a href='/rescan' style='color:#06c'>Volver a escanear</a>"
           " <span style='font-size:.85em;color:#666'>(se corta un momento)</span></p>");
    return h;
}

bool IntentarConectar(const String &ssid, const String &pass)
{
    Serial.printf("Probando %s...\n", ssid.c_str());
    MostrarEstado("Conectando WiFi...", ssid);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < wifiTimeoutMs)
    {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        // Se vuelve a habilitar: el portal lo apaga para no arrastrar la radio de canal.
        WiFi.setAutoReconnect(true);
        Serial.print("Conectado. IP: ");
        Serial.println(WiFi.localIP());
        MostrarEstado("WiFi conectado", WiFi.localIP().toString());
        return true;
    }
    return false;
}

// Escanea y prueba solo las redes guardadas que esten efectivamente al alcance.
bool ConectarAGuardadas()
{
    if (cantidadRedes == 0)
    {
        return false;
    }

    MostrarEstado("Buscando redes...");
    int encontradas = WiFi.scanNetworks();
    for (int i = 0; i < encontradas; i++)
    {
        String ssid = WiFi.SSID(i);
        for (uint8_t j = 0; j < cantidadRedes; j++)
        {
            if (redSsid[j] == ssid && IntentarConectar(redSsid[j], redPass[j]))
            {
                WiFi.scanDelete();
                return true;
            }
        }
    }
    WiFi.scanDelete();
    return false;
}

void DetenerPortal()
{
    if (!portalActivo)
    {
        return;
    }
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    portalActivo = false;
}

void ManejarRaiz()
{
    server.send(200, "text/html", PaginaPrincipal());
}

void ManejarGuardar()
{
    String ssid = server.arg("ssid");
    if (ssid.length() > 0)
    {
        AgregarRed(ssid, server.arg("pass"));
    }
    server.send(200, "text/html",
                F("<meta http-equiv=refresh content='5;url=/'>"
                  "<p style='font-family:sans-serif'>Guardada. Probando la conexion...</p>"));

    // Se prueba recien despues de responder, asi el celular alcanza a ver la pagina.
    delay(500);
    if (IntentarConectar(ssid, server.arg("pass")))
    {
        DetenerPortal();
        SincronizarHora();
        ActualizarDisplay();
    }
}

void ManejarRescan()
{
    EscanearRedes();
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "");
}

void ManejarNombre()
{
    GuardarNombre(server.arg("nombre"));
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "");
}

void ManejarBorrar()
{
    BorrarRed(server.arg("i").toInt());
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "");
}

void IniciarPortal()
{
    if (portalActivo)
    {
        return;
    }

    apSsid = "medidor-" + deviceName;
    MostrarEstado("Sin red conocida", "abriendo portal...");

    // El escaneo va ANTES de levantar el AP, con la radio libre. Y hay que cortar
    // la parte STA: si sigue reintentando una red guardada en segundo plano, arrastra
    // la radio a otro canal y el celular no logra asociarse.
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(true);
    delay(100);
    EscanearRedes();

    WiFi.mode(WIFI_AP_STA);                  // AP_STA: el STA hace falta para reintentar y para /rescan
    WiFi.softAP(apSsid.c_str(), nullptr, 1); // canal 1 fijo, abierta (sin clave)

    server.on("/", ManejarRaiz);
    server.on("/guardar", HTTP_POST, ManejarGuardar);
    server.on("/nombre", HTTP_POST, ManejarNombre);
    server.on("/rescan", ManejarRescan);
    server.on("/borrar", ManejarBorrar);
    server.begin();

    portalActivo = true;
    ultimoClienteMs = 0;
    ultimoIntentoWifi = millis();
    Serial.printf("Portal abierto. Red: %s  ->  http://192.168.4.1\n", apSsid.c_str());
    ActualizarDisplay();
}

// ------------------------------------------------------------------ medicion
// Resultado de muestrear un pin durante una ventana completa.
struct MedicionRms
{
    float offsetVolt; // componente continua: el punto de reposo real de la entrada
    float rmsVolt;    // valor eficaz de la componente alterna
    uint32_t muestras;
};

// Muestrea lo mas rapido posible durante la ventana entera. No asume ningun offset:
// la media de las muestras ES el punto de reposo, porque la senal alterna es
// simetrica alrededor de el. Asi las dos entradas se calibran solas en cada lectura,
// al encender y siguiendo la deriva termica y de alimentacion.
MedicionRms MedirRms(int pin)
{
    uint32_t muestras = 0;
    double suma = 0.0;
    double sumaCuadrados = 0.0;

    unsigned long inicio = millis();
    while (millis() - inicio < VENTANA_MUESTREO_MS)
    {
        // analogReadMilliVolts usa la calibracion de fabrica del chip y corrige la
        // no linealidad del ADC, a diferencia de (adc / 4095) * 3.3
        double milliVolt = analogReadMilliVolts(pin);
        suma += milliVolt;
        sumaCuadrados += milliVolt * milliVolt;
        muestras++;
    }

    MedicionRms resultado = {0.0f, 0.0f, muestras};
    if (muestras == 0)
    {
        return resultado;
    }

    double media = suma / muestras;
    double varianza = sumaCuadrados / muestras - media * media;
    if (varianza < 0.0)
    {
        varianza = 0.0;
    }

    resultado.offsetVolt = media / 1000.0f;
    resultado.rmsVolt = sqrt(varianza) / 1000.0f;
    return resultado;
}

void MedirValores()
{
    MedicionRms sensor = MedirRms(ACS712_PIN);
    if (sensor.muestras == 0)
    {
        return;
    }

    adcOffsetVoltage = sensor.offsetVolt;
    // El ruido no esta correlacionado con la senal: se suma en cuadratura al RMS,
    // asi que se resta en cuadratura y no linealmente.
    float corrienteLeida = sensor.rmsVolt / ACS712_SENSITIVITY;
    float cuadrado = corrienteLeida * corrienteLeida - CORRIENTE_RUIDO * CORRIENTE_RUIDO;
    corriente = cuadrado > 0.0f ? sqrtf(cuadrado) : 0.0f;
    if (corriente < CORRIENTE_MINIMA)
    {
        corriente = 0.0f;
    }

    // Tension: misma tecnica, escalada por el transformador + divisor.
    MedicionRms red = MedirRms(TENSION_PIN);
    tension = red.rmsVolt * TENSION_ESCALA;
    if (tension < TENSION_MINIMA)
    {
        tension = 0.0f;
    }
    potencia = tension * corriente;

    Serial.printf("%.3f A  %.1f V  %.0f W  (pin V: %.3f Vrms, offset %.3f V)\n",
                  corriente, tension, potencia, red.rmsVolt, red.offsetVolt);

    // La energia se integra con el tiempo real transcurrido, no con el nominal de
    // 5 s: si el envio se atrasa o el portal bloquea el loop, el kWh sigue siendo
    // correcto.
    unsigned long ahora = millis();
    if (ultimaMedicionMs != 0)
    {
        float deltaSegundos = (ahora - ultimaMedicionMs) / 1000.0f;
        float deltaKwh = (potencia * deltaSegundos) / 3600000.0f;
        kwh += deltaKwh;
        kwhPendiente += deltaKwh;
        segundosPendientes += deltaSegundos;
    }
    ultimaMedicionMs = ahora;
}

// ------------------------------------------------------------------ mqtt
bool PublicarMedicion()
{
    String timestamp = ObtenerTimestamp();
    if (timestamp.isEmpty())
    {
        Serial.println("No se envia: la hora NTP aun no esta sincronizada.");
        return false;
    }

    String mensaje = "{\"device\":\"" + deviceName +
                     "\",\"ts\":\"" + timestamp + "\"" +
                     ",\"tipo\":\"medicion\",\"data\":{\"corriente\":" +
                     String(corriente) + ",\"tension\":" + String(tension) +
                     ",\"potencia\":" + String(potencia) +
                     ",\"intervalo_segundos\":" + String(segundosPendientes, 1) +
                     ",\"kwh_intervalo\":" + String(kwhPendiente, 6) + "}}";

    if (!mqttClient.publish(mqttTopic.c_str(), mensaje.c_str()))
    {
        Serial.println("No se pudo enviar el mensaje MQTT.");
        return false;
    }

    Serial.printf("Enviado: %s\n", mensaje.c_str());
    return true;
}

bool ConectarMqtt()
{
    ultimoIntentoMqtt = millis();

    String clientId = "esp32-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);
    if (!mqttClient.connect(clientId.c_str(), mqttUser, mqttPassword))
    {
        Serial.printf("MQTT fallo (estado %d)\n", mqttClient.state());
        return false;
    }
    return true;
}

void AtenderMqtt()
{
    if (mqttClient.connected())
    {
        mqttClient.loop();
        return;
    }

    if (millis() - ultimoIntentoMqtt < mqttRetryMs)
    {
        return;
    }
    ConectarMqtt();
}

// ------------------------------------------------------------------ setup / loop
void setup()
{
    Serial.begin(115200);
    CargarNombre(); // antes del display: el encabezado muestra el nombre
    IniciarDisplay();

    // enciendo el RELE por ahora, porque no lo puedo controlar,
    // falta agregar un comando MQTT para encender/apagar la carga
    // o bien un boton en el portal para hacerlo,
    // o un boton fisico en el equipo, que es lo mas simple.
    pinMode(RELE_PIN, OUTPUT);
    digitalWrite(RELE_PIN, HIGH); // activo el rele para encender la carga

    delay(3000); // tiempo para abrir el monitor serie
    Serial.printf("Motivo del reset: %d\n", esp_reset_reason());

    // El canal ADC del pin se adjunta en la primera lectura: hacemos una lectura
    // dummy antes de fijar la atenuacion, si no analogSetPinAttenuation() falla con
    // "Pin is not configured as analog channel".
    analogReadResolution(12);
    analogRead(ACS712_PIN);
    analogSetPinAttenuation(ACS712_PIN, ADC_11db);
    analogRead(TENSION_PIN);
    analogSetPinAttenuation(TENSION_PIN, ADC_11db);

    CargarRedes();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    if (ConectarAGuardadas())
    {
        SincronizarHora();
    }
    else
    {
        IniciarPortal();
    }

    secureClient.setInsecure();
    mqttClient.setServer(mqttHost, mqttPort);
    mqttClient.setKeepAlive(60);
    mqttClient.setSocketTimeout(10);
    // El default de 256 bytes no alcanza para topic + JSON con un nombre largo.
    mqttClient.setBufferSize(512);

    if (WiFi.status() == WL_CONNECTED)
    {
        MostrarEstado("Conectando MQTT...", mqttHost);
        ConectarMqtt();
    }

    MostrarEstado("Midiendo...");
    MedirValores();
    lastMessage = millis();
    ActualizarDisplay();
    Serial.println("Init listo.");
}

void loop()
{
    if (portalActivo)
    {
        server.handleClient();

        // Los reintentos se suspenden mientras haya alguien asociado al AP (un scan o
        // un WiFi.begin() en medio de la configuracion mueve la radio de canal y
        // voltea al celular), y por un rato despues de que se va, por si fue un corte
        // momentaneo. Pasada esa gracia se vuelven a habilitar solos.
        if (WiFi.softAPgetStationNum() > 0)
        {
            ultimoClienteMs = millis();
        }
        bool portalEnUso = ultimoClienteMs != 0 && millis() - ultimoClienteMs < graciaPortalMs;

        // Si nadie esta usando el portal se reintentan las redes guardadas: cubre el
        // caso de que el router vuelva solo despues de un corte.
        if (!portalEnUso && millis() - ultimoIntentoWifi >= reintentoWifiMs)
        {
            ultimoIntentoWifi = millis();
            if (ConectarAGuardadas())
            {
                DetenerPortal();
                SincronizarHora();
                ActualizarDisplay();
            }
        }
    }
    else if (WiFi.status() != WL_CONNECTED)
    {
        if (!ConectarAGuardadas())
        {
            IniciarPortal();
        }
        else
        {
            SincronizarHora();
            ActualizarDisplay();
        }
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        AtenderMqtt();
    }

    if (millis() - lastMessage >= messageInterval)
    {
        lastMessage = millis();
        MedirValores();
        ActualizarDisplay();

        // Solo se limpia el acumulado si el envio salio bien: si falla, la energia
        // se sigue sumando y viaja en el proximo mensaje.
        if (mqttClient.connected() && PublicarMedicion())
        {
            kwhPendiente = 0;
            segundosPendientes = 0;
        }
    }
}
