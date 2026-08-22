import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import mqtt from "npm:mqtt@5.3.0"

serve(async (req) => {
  try {
    const { topic, message } = await req.json()

    if (!topic || !message) {
      return new Response(JSON.stringify({ error: "Faltan parámetros" }), { status: 400 })
    }

    // Configurar cliente con ID único para no colisionar
    const clientId = `supabase_${Math.random().toString(16).substring(2, 10)}`
    
    const client = mqtt.connect("wss://k1127d70.ala.us-east-1.emqxsl.com:8084/mqtt", {
      clientId: clientId,
      username: "device",
      password: "device",
      connectTimeout: 4000,
      reconnectPeriod: 0,
      clean: true
    })

    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        client.end(true)
        reject(new Error("Timeout al enviar mensaje a EMQX"))
      }, 3500)

      client.on("connect", () => {
        // Asegurar que el mensaje sea un string/Buffer válido
        const payload = typeof message === "object" ? JSON.stringify(message) : String(message)

        // Usar QoS 1 para requerir confirmación PUBACK del Broker
        client.publish(topic, payload, { qos: 1 }, (err) => {
          clearTimeout(timer)
          // Cerrar de forma limpia dejando procesar los paquetes pendientes
          client.end(false, () => {
            if (err) reject(err)
            else resolve(true)
          })
        })
      })

      client.on("error", (err) => {
        clearTimeout(timer)
        client.end(true)
        reject(err)
      })
    })

    return new Response(
      JSON.stringify({ success: true, message: "Mensaje MQTT enviado y confirmado" }),
      { status: 200, headers: { "Content-Type": "application/json" } }
    )

  } catch (error) {
    return new Response(
      JSON.stringify({ error: error.message }),
      { status: 500, headers: { "Content-Type": "application/json" } }
    )
  }
})
