#!/usr/bin/env python3
"""Cheap static checks on the reactions SQL (SEC1 item 4); no database needed. The behaviour is tested by
tests/scripts/test_reactions_sql.sh against a local Postgres; this catches an edit that drops a guard even where
no Postgres is installed."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2]
social = (root / "docs/sql/social.sql").read_text()
mig = (root / "docs/sql/reactions_privacy.sql").read_text()
bad = []

def need(cond, why):
    if not cond:
        bad.append(why)

# social.sql: RLS on, the three own-row policies, and no permissive "true" policy on the table.
need("alter table public.message_reactions enable row level security" in social, "RLS is not enabled on message_reactions")
block = social[social.index("create table if not exists public.message_reactions"):social.index("-- forums: public discussion")]
need("using (true)" not in block and "with check (true)" not in block, "a permissive using(true) policy on message_reactions")
need("auth.uid() = reactor" in block, "insert policy no longer ties the reactor to auth.uid()")

# the migration
need(re.search(r"char_length\(emoji\) between 1 and 40", mig), "emoji length check missing")
need("revoke update on public.message_reactions" in mig, "UPDATE is not revoked")
need("revoke execute on function public.can_see_reaction_target" in mig, "can_see_reaction_target is callable by anon")
need("as restrictive for insert" in mig and "as restrictive for select" in mig, "restrictive block policies missing")
need("create trigger okay_reactions_cleanup" in mig or "okay_reactions_cleanup" in mig, "orphan cleanup trigger missing")
rc = mig[mig.index("create or replace function public.reaction_counts"):]
need("security invoker" in rc, "reaction_counts must be SECURITY INVOKER (RLS applies)")
need(not re.search(r"returns table \([^)]*(reactor|email|user_id)", rc), "reaction_counts returns an identity column")
need("security definer" not in rc.split("revoke execute")[0], "reaction_counts must not be SECURITY DEFINER")
# every SECURITY DEFINER function pins its search_path (a definer function without one can be hijacked)
for m in re.finditer(r"security definer([^$]*?)\$", mig):
    need("search_path" in m.group(1), "a SECURITY DEFINER function without set search_path: " + m.group(1)[:60])
need(not re.search(r"\bgrant\b[^;]*\bto\b[^;]*\banon\b[^;]*;", mig.replace("grant select on public.message_reactions to anon", "")),
     "something is granted to anon besides read of the RLS-guarded table")
need("\\" not in mig.replace("\\n", ""), "stray backslash (psql meta-command) in the migration")

if bad:
    print("reactions_sql_static: FAIL")
    for b in bad:
        print("  -", b)
    sys.exit(1)
print("reactions_sql_static: ok")
