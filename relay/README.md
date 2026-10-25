# BongGIF crash-report relay (Cloudflare Worker)

Receives anonymized error reports from the app (`bonggif/crashreport.py`) and files them as GitHub issues
labeled `auto-report` on `Baegovda/BongGIF`. Users need no GitHub account. Same error fingerprint → one issue
(+ up to `MAX_COMMENTS` "seen again" comments). Per-IP rate limit via KV. Body ≤ 64 KB.

## Deploy (owner, one time)
1. Cloudflare account (free plan OK). `npm i -g wrangler` (or use `npx wrangler`), then `wrangler login`.
2. `cd relay && npx wrangler kv namespace create REPORTS` → put the printed `id` into `wrangler.toml`.
3. GitHub → Settings → Developer settings → Fine-grained tokens → new token:
   repository access **only Baegovda/BongGIF**, permission **Issues: Read and write**. Copy it.
4. `npx wrangler secret put GITHUB_TOKEN` and paste the token.
5. Create label once: `gh label create auto-report --repo Baegovda/BongGIF --color B60205` (optional; GitHub creates it on first use if the token may).
6. `npx wrangler deploy` → note URL, e.g. `https://bonggif-report.<subdomain>.workers.dev`.
7. Check: `curl https://bonggif-report.<subdomain>.workers.dev/` → `{"ok":true,...}`.
8. Set `ENDPOINT = "https://bonggif-report.<subdomain>.workers.dev/report"` in `bonggif/crashreport.py`, bump version, release.

## Endpoint
`POST /report` · headers `Content-Type: application/json`, `X-BongGIF-Key: <APP_KEY>` · body schema 1:
`{schema, app:"BongGIF", version, kind, os, python, frozen, type, message, context, traceback, log_tail, fingerprint, ts}`.
Responses: 201 new issue · 200 duplicate counted · 400 invalid · 403 bad key · 413 too large · 429 rate limited · 502 GitHub error.

The app key is a filter, not a secret. Abuse is bounded by rate limit, size cap, and fingerprint dedup.
