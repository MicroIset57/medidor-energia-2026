# ESP32 -> MQTT

Para conectar el ESP32 a MQTT usar esta conexion similar:

mosquitto_pub -h k1127d70.ala.us-east-1.emqxsl.com -p 8883 -u device -P device -t 'telemetria/esp01' -m '{"device":"esp01","ts":"2026-06-11T19:59:00Z","tipo":"","data":{"temp":230}}'

----------------------
host: k1127d70.ala.us-east-1.emqxsl.com
port: 8883
user: device
password: device
SSL modo no seguro
----------------------
