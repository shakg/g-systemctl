export const SITE = {
  name: "g-systemctl",
  title: "g-systemctl: terminal UI for Docker, Compose, systemd and launchd",
  description:
    "Keyboard-driven terminal UI for Docker containers, Compose projects and systemd or launchd services: filter, start, stop, restart, live graphs and logs.",
  repo: "https://github.com/shakg/g-systemctl",
  author: "shakg",
} as const;

/**
 * Self-hosted Umami. Both values are set at build time; without them no tracker
 * is rendered (local dev, forks, preview builds). The website id ends up in the
 * page anyway, so it is not a secret.
 */
const umamiScript = import.meta.env.PUBLIC_UMAMI_SCRIPT ?? "";
const umamiId = import.meta.env.PUBLIC_UMAMI_WEBSITE_ID ?? "";
export const ANALYTICS = umamiScript && umamiId
  ? { script: umamiScript, origin: new URL(umamiScript).origin, websiteId: umamiId, domain: new URL(import.meta.env.SITE).hostname }
  : null;

/** Prefix a site-relative path with the configured base. */
export function url(path = ""): string {
  const base = import.meta.env.BASE_URL.replace(/\/$/, "");
  const p = path.replace(/^\//, "");
  return `${base}/${p}`;
}

export const formatDate = (d: Date) =>
  d.toLocaleDateString("en-US", { year: "numeric", month: "short", day: "numeric", timeZone: "UTC" });
