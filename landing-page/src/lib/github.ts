// Fetched once at build time, so pages make no API calls and nothing shifts after load.
// Failures (offline, rate limit) fall back to null and the UI omits the value.
type Repo = { stars: number | null; tag: string | null };

let cached: Promise<Repo> | undefined;

async function json(path: string): Promise<any> {
  const headers: Record<string, string> = { Accept: "application/vnd.github+json" };
  if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
  try {
    const r = await fetch(`https://api.github.com/repos/shakg/g-systemctl${path}`, {
      headers,
      signal: AbortSignal.timeout(5000),
    });
    return r.ok ? await r.json() : null;
  } catch {
    return null;
  }
}

export function repoInfo(): Promise<Repo> {
  cached ??= Promise.all([json(""), json("/releases/latest")]).then(([repo, rel]) => ({
    stars: typeof repo?.stargazers_count === "number" ? repo.stargazers_count : null,
    tag: typeof rel?.tag_name === "string" ? rel.tag_name : null,
  }));
  return cached;
}
