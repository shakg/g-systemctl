import rss from "@astrojs/rss";
import type { APIContext } from "astro";
import { SITE, url } from "../lib/site";
import { posts } from "../lib/posts";

export async function GET(context: APIContext) {
  const list = await posts();
  return rss({
    title: `${SITE.name} blog`,
    description: "How g-systemctl, a terminal UI for systemd, launchd and Docker, works.",
    site: new URL(url(), context.site).href,
    items: list.map((p) => ({
      title: p.data.title,
      description: p.data.description,
      pubDate: p.data.date,
      link: url(`blog/${p.id}/`),
      categories: p.data.tags,
    })),
    customData: "<language>en-us</language>",
  });
}
