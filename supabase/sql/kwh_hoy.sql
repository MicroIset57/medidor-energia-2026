-- Energia acumulada del dia (desde las 00:00 de Argentina) para un dispositivo.
-- La usa la web via POST /rest/v1/rpc/kwh_hoy. Ejecutar una vez en el SQL Editor.

create index if not exists telemetria_device_ts_idx
    on public.telemetria (device, ts);

create or replace function public.kwh_hoy(p_device text)
returns double precision
language sql
stable
security invoker
as $$
    select coalesce(sum((data->>'kwh_intervalo')::double precision), 0)
    from public.telemetria
    where device = p_device
      and tipo = 'medicion'
      and ts >= date_trunc('day', now() at time zone 'America/Argentina/Buenos_Aires')
                at time zone 'America/Argentina/Buenos_Aires';
$$;

grant execute on function public.kwh_hoy(text) to anon;
