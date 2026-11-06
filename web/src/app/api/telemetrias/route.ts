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

// La suma la hace la funcion public.kwh_hoy (supabase/sql/kwh_hoy.sql): la API
// REST tiene deshabilitados los agregados y traer las filas del dia seria pesado.
async function KwhHoy(device: string): Promise<number | null> {
    try {
        const rpcUrl = `${new URL(telemetriasUrl!).origin}/rest/v1/rpc/kwh_hoy`;
        const kwh = await Consultar(rpcUrl, {
            method: "POST",
            body: JSON.stringify({ p_device: device }),
        });
        return Number(kwh);
    } catch {
        return null; // la funcion todavia no se creo en Supabase
    }
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

        return NextResponse.json({
            ok: true,
            ultima,
            kwhHoy: ultima ? await KwhHoy(ultima.device) : null,
            ahora: new Date().toISOString(),
        });
    } catch (error) {
        const message = error instanceof Error ? error.message : "Error desconocido";

        return NextResponse.json(
            { ok: false, error: message },
            { status: 500 },
        );
    }
}
