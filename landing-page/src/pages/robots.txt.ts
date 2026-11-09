import type { APIContext } from "astro";
import { url } from "../lib/site";

// Crawlers only read /robots.txt at the domain root.
export function GET({ site }: APIContext) {
  const sitemap = new URL(url("sitemap-index.xml"), site).href;
  return new Response(`User-agent: *\nAllow: /\n\nSitemap: ${sitemap}\n`, {
    headers: { "Content-Type": "text/plain; charset=utf-8" },
  });
}
