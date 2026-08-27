"use client";

import { useState } from "react";

type TestResult =
  | { ok: true; rows: Array<Record<string, unknown>> }
  | { ok: false; error: string };

export default function Home() {
  const [result, setResult] = useState<TestResult | null>(null);
  const [loading, setLoading] = useState(false);

  async function testConnection() {
    setLoading(true);

    try {
      const response = await fetch("/api/telemetrias");
      const data = (await response.json()) as TestResult;
      setResult(data);
    } catch (error) {
      setResult({
        ok: false,
        error: error instanceof Error ? error.message : "No se pudo consultar la API",
      });
    } finally {
      setLoading(false);
    }
  }

  return (
    <main className="flex min-h-screen items-center justify-center bg-slate-950 px-6 py-12 text-slate-100">
      <section className="w-full max-w-2xl rounded-2xl border border-slate-800 bg-slate-900 p-8 shadow-2xl">
        <p className="mb-3 text-sm font-medium uppercase tracking-[0.25em] text-cyan-400">
          Diagnóstico de datos
        </p>
        <h1 className="text-4xl font-bold tracking-tight sm:text-5xl">
          Telemetrías
        </h1>
        <p className="mt-4 text-slate-400">
          Consulta segura de las últimas filas de <code>public.telemetria</code>.
        </p>
        <button
          className="mt-8 rounded-lg bg-cyan-400 px-5 py-3 font-semibold text-slate-950 transition hover:bg-cyan-300 disabled:cursor-wait disabled:opacity-60"
          disabled={loading}
          onClick={testConnection}
          type="button"
        >
          {loading ? "Consultando..." : "Probar conexión"}
        </button>

        {result && (
          <div className={`mt-8 rounded-lg border p-4 ${result.ok ? "border-emerald-400/40 bg-emerald-400/10" : "border-rose-400/40 bg-rose-400/10"}`}>
            <p className="font-semibold">{result.ok ? `Conexión correcta: ${result.rows.length} filas` : "La consulta falló"}</p>
            <pre className="mt-3 max-h-96 overflow-auto whitespace-pre-wrap text-sm text-slate-300">
              {JSON.stringify(result.ok ? result.rows : { error: result.error }, null, 2)}
            </pre>
          </div>
        )}
      </section>
    </main>
  );
}
