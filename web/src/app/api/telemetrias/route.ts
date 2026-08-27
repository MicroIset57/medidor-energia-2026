import { NextResponse } from "next/server";

const telemetriasUrl = process.env.url_telemetrias;
const supabaseKey = process.env.annon_public_api_key;

export async function GET() {
    try {
        if (!telemetriasUrl || !supabaseKey) {
            throw new Error("Faltan url_telemetrias o annon_public_api_key en .env");
        }

        const response = await fetch(
            `${telemetriasUrl}?select=id,device,ts,tipo,data,received_at&order=received_at.desc&limit=20`,
            {
                headers: {
                    apikey: supabaseKey,
                    Authorization: `Bearer ${supabaseKey}`,
                },
                cache: "no-store",
            },
        );

        const body = await response.json();

        if (!response.ok) {
            throw new Error(body.message ?? `Supabase respondió HTTP ${response.status}`);
        }

        return NextResponse.json({ ok: true, rows: body });
    } catch (error) {
        const message = error instanceof Error ? error.message : "Error desconocido";

        return NextResponse.json(
            { ok: false, error: message },
            { status: 500 },
        );
    }
}