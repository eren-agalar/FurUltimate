-- FurUltimate online leaderboard — Supabase (PostgreSQL) schema.
-- Run once in the Supabase SQL editor. Reads are public; writes only go through submit_score(),
-- which validates the payload and keeps only each player's personal best per board and GPU.

create table if not exists public.leaderboard (
    id            bigint generated always as identity primary key,
    player_id     uuid        not null,
    nickname      text        not null check (char_length(nickname) between 2 and 20),
    board         text        not null,
    gpu           text        not null check (char_length(gpu) between 2 and 120),
    gpu_vendor    text,
    driver        text,
    score         integer     not null check (score between 1 and 200000),
    tops          real,
    details       jsonb       not null default '{}'::jsonb,
    bench_version text        not null,
    app_version   text,
    created_at    timestamptz not null default now(),
    updated_at    timestamptz not null default now(),
    unique (player_id, board, gpu)
);
create index if not exists leaderboard_board_score on public.leaderboard (board, score desc);

-- No direct table access for clients (player_id must stay private).
alter table public.leaderboard enable row level security;
revoke all on public.leaderboard from anon, authenticated;

-- Public views without player identifiers (dropped first so the script can be re-run to upgrade).
drop view if exists public.leaderboard_public;
drop view if exists public.gpu_ranking;
create or replace view public.leaderboard_public as
    select id as entry_id, board, nickname, gpu, gpu_vendor, driver, score, tops, bench_version, updated_at
    from public.leaderboard;
create or replace view public.gpu_ranking as
    select board, gpu, count(*)::int as players, round(avg(score))::int as avg_score, max(score) as best_score
    from public.leaderboard
    group by board, gpu;
grant select on public.leaderboard_public, public.gpu_ranking to anon, authenticated;

-- Submit a result. Returns {"status": "inserted" | "improved" | "kept", "rank": n} or {"error": "..."}.
create or replace function public.submit_score(
    p_player uuid, p_nick text, p_board text, p_gpu text, p_vendor text, p_driver text,
    p_score integer, p_tops real, p_details jsonb, p_bench_version text, p_app_version text)
returns jsonb language plpgsql security definer set search_path = public as $$
declare
    v_existing public.leaderboard%rowtype;
    v_status text;
    v_rank int;
    g double precision; s double precision; c double precision; v double precision; f double precision;
    v_expected double precision;
begin
    p_nick := btrim(p_nick);
    if p_nick is null or char_length(p_nick) < 2 or char_length(p_nick) > 20 or p_nick !~ '^[[:alnum:] _.\-çğıöşüÇĞİÖŞÜ]+$' then
        return jsonb_build_object('error', 'invalid nickname');
    end if;
    if p_board not in ('overall:STANDARD-1080P', 'overall:HIGH-1440P', 'overall:ULTRA-2160P',
                       'ai:DirectML FP16', 'ai:DirectML FP32', 'ai:Shader FP32') then
        return jsonb_build_object('error', 'invalid board');
    end if;
    if p_score is null or p_score < 1 or p_score > 200000 then
        return jsonb_build_object('error', 'invalid score');
    end if;
    if p_board like 'overall:%' then
        if p_bench_version <> 'FNM-Bench 1.0' then return jsonb_build_object('error', 'unsupported benchmark version'); end if;
        -- Consistency: the overall score must be the documented weighted geometric mean of the sub-scores.
        g := (p_details->>'graphics')::double precision; s := (p_details->>'shader')::double precision;
        c := (p_details->>'compute')::double precision;  v := (p_details->>'vram')::double precision;
        f := (p_details->>'stability')::double precision;
        if g is null or s is null or c is null or v is null or f is null or least(g, s, c, v, f) < 1 or f > 1000 then
            return jsonb_build_object('error', 'missing sub-scores');
        end if;
        v_expected := exp(0.35 * ln(g) + 0.20 * ln(s) + 0.20 * ln(c) + 0.15 * ln(v) + 0.10 * ln(f));
        if abs(v_expected - p_score) > greatest(2, p_score * 0.01) then
            return jsonb_build_object('error', 'inconsistent score');
        end if;
    else
        if p_bench_version <> 'FNM-AI 1.0' then return jsonb_build_object('error', 'unsupported AI version'); end if;
        if p_tops is null or p_tops <= 0 or p_tops > 10000 or abs(1000.0 * p_tops / 50.0 - p_score) > greatest(2, p_score * 0.01) then
            return jsonb_build_object('error', 'inconsistent AI score');
        end if;
    end if;

    select * into v_existing from public.leaderboard where player_id = p_player and board = p_board and gpu = p_gpu;
    if found then
        if v_existing.updated_at > now() - interval '30 seconds' then
            return jsonb_build_object('error', 'rate limited');
        end if;
        if p_score > v_existing.score then
            update public.leaderboard
               set score = p_score, tops = p_tops, details = p_details, nickname = p_nick, driver = p_driver,
                   gpu_vendor = p_vendor, bench_version = p_bench_version, app_version = p_app_version, updated_at = now()
             where id = v_existing.id;
            v_status := 'improved';
        else
            update public.leaderboard set nickname = p_nick where id = v_existing.id;
            v_status := 'kept';
        end if;
    else
        if (select count(*) from public.leaderboard where player_id = p_player) >= 50 then
            return jsonb_build_object('error', 'too many entries');
        end if;
        insert into public.leaderboard (player_id, nickname, board, gpu, gpu_vendor, driver, score, tops, details, bench_version, app_version)
        values (p_player, p_nick, p_board, p_gpu, p_vendor, p_driver, p_score, p_tops, coalesce(p_details, '{}'::jsonb), p_bench_version, p_app_version);
        v_status := 'inserted';
    end if;
    select 1 + count(*) into v_rank from public.leaderboard
     where board = p_board and score > (select score from public.leaderboard where player_id = p_player and board = p_board and gpu = p_gpu);
    return jsonb_build_object('status', v_status, 'rank', v_rank);
end $$;

drop function if exists public.my_entries(uuid);
-- The caller's own entries with their rank (player_id is only known to the installation that created it).
create or replace function public.my_entries(p_player uuid)
returns table (entry_id bigint, board text, gpu text, score integer, rank bigint, total bigint)
language sql security definer set search_path = public as $$
    select l.id, l.board, l.gpu, l.score,
           (select 1 + count(*) from public.leaderboard x where x.board = l.board and x.score > l.score) as rank,
           (select count(*) from public.leaderboard x where x.board = l.board) as total
    from public.leaderboard l where l.player_id = p_player;
$$;

grant execute on function public.submit_score(uuid, text, text, text, text, text, integer, real, jsonb, text, text) to anon, authenticated;
grant execute on function public.my_entries(uuid) to anon, authenticated;
