-- Sample harmony 3b: a route's role setups made in the lab - each role's synth, its controls, and the sample of a sampler
-- role - kept apart from the route's own patch until the switch-over (sub-project 6). The live site and the apps never read
-- this table, and the lab never writes the route row: no read-modify-write of properties, so neither side can overwrite the
-- other's edit. Setters only, as everything a setter makes (public.is_setter(), 0005).
--
-- Samples: recordings/lab/<route id>/<role>-<ms>.<ext>. The anon read policy (0015) opens a path whose first segment is a
-- published feature id; "lab" never is, so these stay setters-only under the setter policies already on the bucket (0012).
-- Deletes stay soft everywhere: the lab upserts a route's row and never deletes it; a replaced sample stays in the bucket.

create table if not exists public.lab_route_roles (
  route_id   uuid primary key references public.features (id) on delete cascade,
  roles      jsonb not null default '{}'::jsonb,
  updated_by uuid references auth.users (id) on delete set null,
  updated_at timestamptz not null default now()
);

-- New tables in public are born with no grants (0009's default privileges): authenticated gets exactly what the lab uses -
-- read, add, update - and never DELETE or TRUNCATE (soft deletes; TRUNCATE skips RLS). anon gets nothing.
revoke all on public.lab_route_roles from anon, authenticated;
grant select, insert, update on public.lab_route_roles to authenticated;

alter table public.lab_route_roles enable row level security;

drop policy if exists lab_route_roles_setter on public.lab_route_roles;
create policy lab_route_roles_setter on public.lab_route_roles
  for all to authenticated
  using (public.is_setter()) with check (public.is_setter());

drop trigger if exists lab_route_roles_touch on public.lab_route_roles;
create trigger lab_route_roles_touch before update on public.lab_route_roles
  for each row execute function public.touch_updated_at();
