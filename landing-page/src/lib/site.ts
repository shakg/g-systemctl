export const SITE = {
  name: "g-systemctl",
  title: "g-systemctl: terminal UI for systemd, launchd and Docker",
  description:
    "Keyboard-driven terminal UI to list, filter, start, stop and restart systemd and launchd services and Docker containers, with live graphs and logs.",
  repo: "https://github.com/shakg/g-systemctl",
  author: "shakg",
} as const;

/**
 * Self-hosted Open Analytics (getopen.so). Both values are set at build time;
 * without them no tracker is rendered (local dev, forks, preview builds).
 * The key is write-only and ends up in the page anyway, so it is not a secret.
 */
const oaCollector = (import.meta.env.PUBLIC_OA_COLLECTOR ?? "").replace(/\/$/, "");
const oaKey = import.meta.env.PUBLIC_OA_KEY ?? "";
export const ANALYTICS = oaCollector && oaKey ? { collector: oaCollector, key: oaKey } : null;

/** Prefix a site-relative path with the configured base. */
export function url(path = ""): string {
  const base = import.meta.env.BASE_URL.replace(/\/$/, "");
  const p = path.replace(/^\//, "");
  return `${base}/${p}`;
}

export const formatDate = (d: Date) =>
  d.toLocaleDateString("en-US", { year: "numeric", month: "short", day: "numeric", timeZone: "UTC" });
