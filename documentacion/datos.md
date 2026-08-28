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

ACS712 OUT --- 10 kΩ ---+--- GPIO ADC del ESP32
                        |
                       20 kΩ
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

