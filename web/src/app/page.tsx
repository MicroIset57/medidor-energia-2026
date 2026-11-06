"use client";

import { useEffect, useState, type CSSProperties, type ReactNode } from "react";

type Medicion = {
  device: string;
  ts: string | null;
  received_at: string;
  data: {
    corriente?: number;
    tension?: number;
    potencia?: number;
  };
};

type Respuesta =
  | { ok: true; ultima: Medicion | null; kwhHora: number; ahora?: string }
  | { ok: false; error: string };

const INTERVALO_MS = 5000;
// El equipo publica cada 5 s: si pasan 20 s sin datos se lo da por desconectado.
const LIMITE_ONLINE_MS = 20000;

const formatoFechaHora = new Intl.DateTimeFormat("es-AR", {
  timeZone: "America/Argentina/Buenos_Aires",
  day: "2-digit",
  month: "2-digit",
  year: "2-digit",
  hour: "2-digit",
  minute: "2-digit",
  hour12: false,
});

function FechaHora(iso: string | null) {
  if (!iso) return "--/--/-- --:--";
  return formatoFechaHora.format(new Date(iso)).replace(",", "");
}

// Coordenadas en pixeles del display real (240x135) pasadas a porcentaje, asi el
// rectangulo escala con la pantalla y conserva la misma disposicion que el TFT.
function Texto({
  x,
  y,
  alto,
  color,
  ancla = "tl",
  children,
}: {
  x: number;
  y: number;
  alto: number;
  color: string;
  ancla?: "tl" | "tr" | "mc" | "bc";
  children: ReactNode;
}) {
  const traslado = {
    tl: "translate(0, 0)",
    tr: "translate(-100%, 0)",
    mc: "translate(-50%, -50%)",
    bc: "translate(-50%, -100%)",
  }[ancla];

  const estilo: CSSProperties = {
    position: "absolute",
    left: `${(x / 240) * 100}%`,
    top: `${(y / 135) * 100}%`,
    transform: traslado,
    fontSize: `${(alto / 240) * 100}cqw`,
    lineHeight: 1,
    color,
    whiteSpace: "nowrap",
  };

  return <span style={estilo}>{children}</span>;
}

const COLOR = {
  blanco: "#ffffff",
  grisClaro: "#d3d3d3",
  gris: "#808080",
  cyan: "#00ffff",
  verde: "#00ff00",
  rojo: "#ff0000",
};

export default function Home() {
  const [respuesta, setRespuesta] = useState<Respuesta | null>(null);

  useEffect(() => {
    let activo = true;

    async function consultar() {
      try {
        const res = await fetch("/api/telemetrias", { cache: "no-store" });
        const data = (await res.json()) as Respuesta;
        if (activo) setRespuesta(data);
      } catch (error) {
        if (activo) {
          setRespuesta({
            ok: false,
            error: error instanceof Error ? error.message : "Sin conexion",
          });
        }
      }
    }

    consultar();
    const id = setInterval(consultar, INTERVALO_MS);
    return () => {
      activo = false;
      clearInterval(id);
    };
  }, []);

  const medicion = respuesta?.ok ? respuesta.ultima : null;
  const online =
    respuesta?.ok && medicion && respuesta.ahora
      ? Date.parse(respuesta.ahora) - Date.parse(medicion.received_at) < LIMITE_ONLINE_MS
      : false;

  let mensaje: [string, string?] | null = null;
  if (!respuesta) mensaje = ["Conectando..."];
  else if (!respuesta.ok) mensaje = ["Error", respuesta.error];
  else if (!medicion) mensaje = ["Sin datos"];

  return (
    <main className="flex min-h-screen flex-col items-center justify-center gap-6 bg-slate-950 px-4 py-12">
      <div className="w-full max-w-[720px] rounded-2xl bg-slate-800 p-3 shadow-2xl sm:p-5">
        <div
          className="relative w-full overflow-hidden rounded bg-black font-mono"
          style={{ aspectRatio: "240 / 135", containerType: "inline-size" }}
        >
          <Texto x={5} y={2} alto={14} color={COLOR.blanco}>
            {FechaHora(medicion?.ts ?? null)}
          </Texto>
          <Texto x={5} y={18} alto={14} color={COLOR.grisClaro}>
            {medicion?.device ?? ""}
          </Texto>

          {medicion && (
            <>
              <Texto x={150} y={2} alto={14} color={online ? COLOR.verde : COLOR.rojo}>
                {online ? "WiFi" : "----"}
              </Texto>
              <Texto x={195} y={2} alto={14} color={online ? COLOR.verde : COLOR.rojo}>
                {online ? "MQTT" : "----"}
              </Texto>
            </>
          )}

          {mensaje ? (
            <>
              <Texto x={120} y={mensaje[1] ? 74 : 84} alto={24} color={COLOR.blanco} ancla="mc">
                {mensaje[0]}
              </Texto>
              {mensaje[1] && (
                <Texto x={120} y={102} alto={12} color={COLOR.cyan} ancla="mc">
                  {mensaje[1].slice(0, 40)}
                </Texto>
              )}
            </>
          ) : (
            medicion && (
              <>
                <Texto x={120} y={62} alto={48} color={COLOR.cyan} ancla="mc">
                  {(medicion.data.potencia ?? 0).toFixed(0)} W
                </Texto>
                <Texto x={8} y={92} alto={24} color={COLOR.blanco}>
                  {(medicion.data.tension ?? 0).toFixed(0)} V
                </Texto>
                <Texto x={232} y={92} alto={24} color={COLOR.blanco} ancla="tr">
                  {(medicion.data.corriente ?? 0).toFixed(2)} A
                </Texto>
                <Texto x={120} y={135} alto={16} color={COLOR.gris} ancla="bc">
                  {respuesta?.ok ? respuesta.kwhHora.toFixed(3) : "-.---"} kWh/h
                </Texto>
              </>
            )
          )}
        </div>
      </div>

      <p className="text-sm text-slate-500">
        Medidor de energia ISET 57 · se actualiza cada 5 segundos
      </p>
    </main>
  );
}
