// BongGIF crash-report relay: app -> this Worker -> GitHub issue (no user login).
// Dedups by fingerprint (KV), rate-limits per IP (KV counters), caps comment spam.
const MAX_BYTES = 64 * 1024;
const LABEL = "auto-report";

const json = (obj, status = 200) =>
  new Response(JSON.stringify(obj), { status, headers: { "content-type": "application/json" } });

function validate(r) {
  if (!r || typeof r !== "object") return "body must be an object";
  if (r.schema !== 1 || r.app !== "BongGIF") return "bad schema/app";
  for (const k of ["version", "type", "fingerprint", "os"]) if (typeof r[k] !== "string" || !r[k]) return `missing ${k}`;
  if (!/^[0-9a-f]{8,40}$/.test(r.fingerprint)) return "bad fingerprint";
  if (!/^\d+\.\d+\.\d+([-.\w]*)$/.test(r.version)) return "bad version";
  for (const k of ["message", "traceback", "log_tail", "context", "kind"]) if (r[k] != null && typeof r[k] !== "string") return `bad ${k}`;
  return null;
}

const fence = (s, max) => "```\n" + String(s || "").slice(-max).replace(/```/g, "ˋˋˋ") + "\n```";

function issueBody(r) {
  return [
    `**Auto-reported** · v${r.version} · ${r.kind || "crash"} · fingerprint \`${r.fingerprint}\``,
    "",
    `- OS: ${r.os}`, `- Python: ${r.python || "?"} · frozen: ${!!r.frozen}`,
    r.context ? `- Context: ${r.context}` : "",
    "", `**${r.type}**: ${String(r.message || "").slice(0, 500)}`,
    "", "<details><summary>Traceback</summary>", "", fence(r.traceback, 20000), "</details>",
    "", "<details><summary>Log tail (anonymized)</summary>", "", fence(r.log_tail, 20000), "</details>",
  ].filter((x) => x !== null).join("\n");
}

async function gh(env, path, method, body) {
  const res = await fetch(`https://api.github.com/repos/${env.GITHUB_REPO}${path}`, {
    method,
    headers: {
      authorization: `Bearer ${env.GITHUB_TOKEN}`,
      accept: "application/vnd.github+json",
      "user-agent": "bonggif-report-relay",
      "content-type": "application/json",
    },
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!res.ok) throw new Error(`GitHub ${method} ${path} -> ${res.status} ${await res.text()}`);
  return res.json();
}

async function rateLimited(env, ip) {
  const hour = Math.floor(Date.now() / 3600000);
  const key = `rl:${ip}:${hour}`;
  const n = parseInt((await env.REPORTS.get(key)) || "0", 10) + 1;
  await env.REPORTS.put(key, String(n), { expirationTtl: 3700 });
  return n > parseInt(env.RATE_PER_HOUR || "20", 10);
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/") return json({ ok: true, service: "bonggif-report" });
    if (request.method !== "POST" || url.pathname !== "/report") return json({ error: "not found" }, 404);
    if (request.headers.get("x-bonggif-key") !== env.APP_KEY) return json({ error: "forbidden" }, 403);
    const len = parseInt(request.headers.get("content-length") || "0", 10);
    if (len > MAX_BYTES) return json({ error: "too large" }, 413);
    const raw = await request.text();
    if (raw.length > MAX_BYTES) return json({ error: "too large" }, 413);
    let r;
    try { r = JSON.parse(raw); } catch { return json({ error: "bad json" }, 400); }
    const bad = validate(r);
    if (bad) return json({ error: bad }, 400);

    const ip = request.headers.get("cf-connecting-ip") || "unknown";
    if (await rateLimited(env, ip)) return json({ error: "rate limited" }, 429);

    const fpKey = `fp:${r.fingerprint}`;
    const known = JSON.parse((await env.REPORTS.get(fpKey)) || "null");
    try {
      if (!known) {
        const title = `[auto-report] ${r.type}: ${String(r.message || "").slice(0, 80)} (v${r.version})`;
        const issue = await gh(env, "/issues", "POST", { title, body: issueBody(r), labels: [LABEL] });
        await env.REPORTS.put(fpKey, JSON.stringify({ issue: issue.number, count: 1, comments: 0 }));
        return json({ ok: true, issue: issue.number, new: true }, 201);
      }
      known.count += 1;
      if (known.comments < parseInt(env.MAX_COMMENTS || "10", 10)) {
        await gh(env, `/issues/${known.issue}/comments`, "POST", {
          body: `Seen again (#${known.count}) · v${r.version} · ${r.os}\n\n**${r.type}**: ${String(r.message || "").slice(0, 300)}`,
        });
        known.comments += 1;
      }
      await env.REPORTS.put(fpKey, JSON.stringify(known));
      return json({ ok: true, issue: known.issue, count: known.count });
    } catch (e) {
      return json({ error: "upstream", detail: String(e).slice(0, 300) }, 502);
    }
  },
};
