export const SITE = {
  name: "g-systemctl",
  title: "g-systemctl: terminal UI for Docker, Compose, systemd and launchd",
  description:
    "Keyboard-driven terminal UI for Docker containers, Compose projects and systemd or launchd services: filter, start, stop, restart, live graphs and logs.",
  repo: "https://github.com/shakg/g-systemctl",
  author: "shakg",
} as const;

/** Prefix a site-relative path with the configured base. */
export function url(path = ""): string {
  const base = import.meta.env.BASE_URL.replace(/\/$/, "");
  const p = path.replace(/^\//, "");
  return `${base}/${p}`;
}

export const formatDate = (d: Date) =>
  d.toLocaleDateString("en-US", { year: "numeric", month: "short", day: "numeric", timeZone: "UTC" });
