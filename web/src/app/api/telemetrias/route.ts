import { NextResponse } from "next/server";

const telemetriasUrl = process.env.url_telemetrias;
const supabaseKey = process.env.annon_public_api_key;

async function Consultar(url: string, init?: RequestInit) {
    const response = await fetch(url, {
        ...init,
        headers: {
            apikey: supabaseKey!,
            Authorization: `Bearer ${supabaseKey}`,
            "Content-Type": "application/json",
        },
        cache: "no-store",
    });
    const body = await response.json();
    if (!response.ok) {
        throw new Error(body.message ?? `Supabase respondió HTTP ${response.status}`);
    }
    return body;
}

const UNA_HORA_MS = 3600_000;

// Suma la energia de la ultima hora a partir de kwh_intervalo. Cada fila cubre
// [ts - intervalo_segundos, ts]; si el equipo estuvo sin red, el primer mensaje
// al volver trae todo lo acumulado con su intervalo real. Por eso no se integra
// potencia x tiempo (fallaria en los huecos) y la fila que empieza antes de la
// ventana se cuenta solo en la proporcion que cae dentro de la hora.
async function KwhUltimaHora(device: string, ahoraMs: number) {
    const inicioMs = ahoraMs - UNA_HORA_MS;
    const filas: Array<{ ts: string; kwh: number | null; seg: number | null }> = await Consultar(
        `${telemetriasUrl}?select=ts,kwh:data->kwh_intervalo,seg:data->intervalo_segundos` +
            `&device=eq.${encodeURIComponent(device)}&tipo=eq.medicion` +
            `&ts=gte.${encodeURIComponent(new Date(inicioMs).toISOString())}&limit=1000`,
    );

    let total = 0;
    for (const fila of filas) {
        const kwh = Number(fila.kwh ?? 0);
        const finMs = Date.parse(fila.ts);
        const duracionMs = Number(fila.seg ?? 0) * 1000;
        const desdeMs = finMs - duracionMs;

        if (duracionMs <= 0 || desdeMs >= inicioMs) {
            total += kwh;
        } else {
            total += kwh * ((finMs - inicioMs) / duracionMs);
        }
    }
    return total;
}

export async function GET() {
    try {
        if (!telemetriasUrl || !supabaseKey) {
            throw new Error("Faltan url_telemetrias o annon_public_api_key en .env");
        }

        const filas = await Consultar(
            `${telemetriasUrl}?select=device,ts,data,received_at&tipo=eq.medicion&order=received_at.desc&limit=1`,
        );
        const ultima = filas[0] ?? null;
        const ahoraMs = Date.now();

        return NextResponse.json({
            ok: true,
            ultima,
            kwhHora: ultima ? await KwhUltimaHora(ultima.device, ahoraMs) : 0,
            ahora: new Date(ahoraMs).toISOString(),
        });
    } catch (error) {
        const message = error instanceof Error ? error.message : "Error desconocido";

        return NextResponse.json(
            { ok: false, error: message },
            { status: 500 },
        );
    }
}
