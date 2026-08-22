# Medidor de energia
# ISET 57
# 2026

Este proyecto esta pensado para colocar un medidor de energía (Kwh) e instantáneo (Pot, Tension y corriente) a una zapatilla, para así medir energía de elementos enchufados.

Las telemetrias se envian a una base de datos por medio de wifi -> MQTT -> Base de datos.

# Infraestructura:

## ESP32 con :
- Display LCD de 1 pulgada, 
- ACS712 (medidor de corriente por efecto hall)
- Uso del WiFi del ESP32 para conectarse a internet.
- Un relé para comando remoto.

## EMQX broker :
- https://www.emqx.com
- Un proyecto Mqtt con conexión a supabase (HTTP POST) para envío de MQTT.

## Supabase database :
- https://supabase.com
- Un proyecto en supabase para almacenar telemetrias.
- Accesos por HTTP POST para conectar el EMQX.

## Github :
- Código fuente del ESP32 y de la APP web.

## Página web :
- Server https://vercel.com/
- Una pagina web para mostrar datos de la base de datos.
- En principio creada en Next.Js
- https://medidor-energia-2026.vercel.app/

- *Test local*: npm run dev
