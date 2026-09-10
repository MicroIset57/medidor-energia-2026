# Datos de la ingesta en MQTT

Ejemplo de json con datos de una telemetria:

- topic: telemetria/<nombre_de_telemetria>

- mensaje: {"device":"esp01","ts":"2026-06-11T19:59:00Z","tipo":"evento","data":{"temp":230,"humedad":22}}

---

# Medición de energía

Vamos a medir corriente con un ACS712, esto es medición por efecto hall

Mide corriente eléctrica continua o alterna sin conexión eléctrica directa entre el circuito de potencia y la salida de señal.

La corriente atraviesa el conductor interno del integrado y genera una salida analógica:

- Sin corriente: aproximadamente VCC / 2
- Con corriente positiva: la salida aumenta
- Con corriente negativa: la salida disminuye
- Alimentación típica: 5 V
- Salida analógica típica: entre 0 V y 5 V

Hay varias versiones:

- ACS712-05B: hasta ±5 A, sensibilidad aproximada 185 mV/A
- ACS712-20A: hasta ±20 A, sensibilidad aproximada 100 mV/A
- ACS712-30A: hasta ±30 A, sensibilidad aproximada 66 mV/A

Conexiones:

    ACS712 VCC  -> 5 V
    ACS712 GND  -> GND del ESP32
    ACS712 OUT  -> entrada ADC del ESP32

Importante: la salida del ACS712 puede llegar hasta casi 5 V, pero el ADC del ESP32 normalmente no tolera más de 3.3 V.

Usa un divisor resistivo, por ejemplo:

ACS712 OUT ---[ 10 kΩ ]---+--- GPIO ADC del ESP32
                        |
                      [20 kΩ]                       
                        |
                       GND

Esto reduce aproximadamente la señal de 0-5 V a 0-3.3 V                

---

El ESP32 debe leer muchas muestras del ADC durante un intervalo corto:

1. Leer el ADC.
2. Convertir la lectura a voltios.
3. Restar el punto medio, aproximadamente 2.5 V.
4. Calcular el valor RMS de la señal.
5. Dividir por la sensibilidad del modelo.
6. Para corriente alterna:

Ejemplo para un ACS712-20A:

El valor V_offset conviene medirlo al iniciar el ESP32 con la carga apagada, porque puede no ser exactamente 2.5 V.

IMPORTANTE: Para más precisión conviene promediar muchas muestras y calibrar con una pinza amperométrica.

# Potencia usando 200 V teóricos

Potencia aparente = 200 V × I_RMS

La fórmula práctica para el ESP32 sería:

potencia_w = 220.0 * corriente_rms;
energia_kwh += potencia_w * intervalo_horas / 1000.0;

---

# Cálculo de kWh desde los datos guardados en la base

El problema con enviar un acumulado local en el ESP32 es que al reiniciarse el dispositivo, la variable `kwh` vuelve a cero y el histórico queda roto.

La solución correcta es guardar solo la energía del intervalo y reconstruir el total en base de datos.

## 1. Guardar el consumo por intervalo

En cada lectura enviar:

- `ts` = timestamp de la medición
- `corriente`
- `tension`
- `potencia`
- `intervalo_segundos`

Por ejemplo:

```json
{
  "device": "zapa01",
  "ts": "2026-09-10T15:00:00-03:00",
  "tipo": "medicion",
  "data": {
    "corriente": 1.25,
    "tension": 220,
    "potencia": 275,
    "intervalo_segundos": 5
  }
}
```

## 2. Calcular el kWh de cada medición

Para cada registro, el consumo del intervalo es:

```text
kwh_intervalo = potencia_w * intervalo_horas / 1000
```

donde:

```text
intervalo_horas = intervalo_segundos / 3600
```

Ejemplo:

```text
potencia = 275 W
intervalo = 5 s
intervalo_horas = 5 / 3600 = 0.0013889
kwh_intervalo = 275 * 0.0013889 / 1000 = 0.0003819 kWh
```

El JSON que debe enviarse a Supabase desde el ESP32 es:

```json
{
  "device": "zapa01",
  "ts": "2026-09-10T15:00:00-03:00",
  "tipo": "medicion",
  "data": {
    "corriente": 1.25,
    "tension": 220,
    "potencia": 275,
    "intervalo_segundos": 5,
    "kwh_intervalo": 0.0003819
  }
}
```

## 3. Acumular en la base de datos

Con tu tabla `public.telemetria`, la consulta para leer cada lectura y su acumulado es:

```sql
SELECT
  id,
  device,
  ts,
  tipo,
  data,
  (data->>'corriente')::numeric AS corriente,
  (data->>'tension')::numeric AS tension,
  (data->>'potencia')::numeric AS potencia_w,
  (data->>'intervalo_segundos')::numeric AS intervalo_segundos,
  (data->>'kwh_intervalo')::numeric AS kwh_intervalo,
  SUM((data->>'kwh_intervalo')::numeric) OVER (
    PARTITION BY device
    ORDER BY ts
    ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
  ) AS kwh_acumulado_total
FROM public.telemetria
ORDER BY device, ts;
```

Y si solo querés el total de todo el historial por dispositivo:

```sql
SELECT
  device,
  SUM((data->>'kwh_intervalo')::numeric) AS kwh_total
FROM public.telemetria
GROUP BY device
ORDER BY device;
```

Para ver el consumo por día:

```sql
SELECT
  device,
  date(ts AT TIME ZONE 'UTC' AT TIME ZONE 'America/Argentina/Buenos_Aires') AS fecha,
  SUM((data->>'kwh_intervalo')::numeric) AS kwh_dia
FROM public.telemetria
GROUP BY device, date(ts AT TIME ZONE 'UTC' AT TIME ZONE 'America/Argentina/Buenos_Aires')
ORDER BY device, fecha;
```

Para ver el consumo por mes:

```sql
SELECT
  device,
  date_trunc('month', ts AT TIME ZONE 'UTC' AT TIME ZONE 'America/Argentina/Buenos_Aires') AS mes,
  SUM((data->>'kwh_intervalo')::numeric) AS kwh_mes
FROM public.telemetria
GROUP BY device, date_trunc('month', ts AT TIME ZONE 'UTC' AT TIME ZONE 'America/Argentina/Buenos_Aires')
ORDER BY device, mes;
```

## 4. Mejor práctica para no perder continuidad

No enviar un `kwh` acumulado que viva solo en memoria del ESP32.

En cambio:

- guardar `potencia` o `energia_del_intervalo`
- calcular el total en la base
- si el ESP32 se resetea, la nueva serie continúa desde cero, pero la base suma sobre los valores históricos

## 5. Regla práctica

El ESP32 debería enviar mediciones de potencia instantánea o energía del intervalo, no un contador total local.

La base es la responsable de responder:

```text
kwh_total_hasta_ahora = suma de todos los intervalos desde el inicio
```

Esto evita que al resetear el ESP32 el contador vuelva a 0.

---

energia de hoy:

SELECT
  device,
  SUM((data->>'kwh_intervalo')::numeric) AS kwh_total
FROM public.telemetria
WHERE ts BETWEEN '2026-09-10T00:00:00-03:00'::timestamptz
             AND '2026-09-10T23:59:59-03:00'::timestamptz
GROUP BY device
ORDER BY device;
