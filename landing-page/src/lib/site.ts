export const SITE = {
  name: "g-systemctl",
  title: "g-systemctl — services and containers in one terminal UI",
  description:
    "g-systemctl is a keyboard-driven terminal UI for systemd, launchd and Docker. Filter, inspect, graph, start, stop, restart and follow logs.",
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
