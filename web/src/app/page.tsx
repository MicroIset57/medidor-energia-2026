export default function Home() {
  return (
    <main className="flex min-h-screen items-center justify-center px-6">
      <section className="text-center">
        <p className="mb-4 text-sm font-medium uppercase tracking-[0.3em] text-sky-600">
          Next.js + Vercel
        </p>
        <h1 className="text-5xl font-bold tracking-tight text-slate-900 sm:text-6xl">
          Hola mundo
        </h1>
        <p className="mt-5 text-lg text-slate-600">
          Tu primera app web está lista para desplegar.
        </p>
        <div className="mx-auto mt-8 h-1 w-20 rounded-full bg-sky-500" />
      </section>
    </main>
  );
}
