// @ts-check
import { defineConfig } from "astro/config";
import sitemap from "@astrojs/sitemap";

// Override for another host, e.g. SITE_URL=https://shakg.github.io SITE_BASE=/g-systemctl npm run build
const site = process.env.SITE_URL ?? "https://g-systemctl.org";
const base = process.env.SITE_BASE ?? "/";

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
