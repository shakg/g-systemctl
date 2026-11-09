// @ts-check
import { defineConfig } from "astro/config";
import sitemap from "@astrojs/sitemap";

// Override for a custom domain: SITE_URL=https://example.com SITE_BASE=/ npm run build
const site = process.env.SITE_URL ?? "https://shakg.github.io";
const base = process.env.SITE_BASE ?? "/g-systemctl";

export default defineConfig({
  site,
  base,
  trailingSlash: "always",
  integrations: [sitemap()],
  build: {
    // One HTML request per page: no render-blocking stylesheet.
    inlineStylesheets: "always",
  },
  markdown: {
    shikiConfig: { theme: "github-dark-default" },
  },
});
