const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));
const RM = matchMedia("(prefers-reduced-motion: reduce)").matches;
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const rnd = (a, b) => a + Math.random() * (b - a);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Run `f` once the page has loaded and the main thread is free, so decoration never delays first paint.
function idle(f) {
  const go = () => (window.requestIdleCallback ? requestIdleCallback(f, { timeout: 1500 }) : setTimeout(f, 200));
  document.readyState === "complete" ? go() : addEventListener("load", go, { once: true });
}

// A clock that only advances while `el` is on screen and the tab is visible.
function gate(el, threshold = 0.15) {
  let vis = false; const waiters = [];
  const flush = () => { if (vis && !document.hidden) waiters.splice(0).forEach((f) => f()); };
  new IntersectionObserver(([e]) => { vis = e.isIntersecting; flush(); }, { threshold }).observe(el);
  document.addEventListener("visibilitychange", flush);
  const ready = () => (vis && !document.hidden) ? Promise.resolve() : new Promise((r) => waiters.push(r));
  const wait = async (ms) => { await sleep(ms); await ready(); };
  wait.visible = () => vis && !document.hidden;
  wait.ready = ready;
  return wait;
}

/* ---------------- nav ---------------- */
const nav = $("#nav");
const onScroll = () => {
  nav.classList.toggle("solid", scrollY > 30);
  const t = clamp(scrollY / 360, 0, 1);
  $("#term").style.setProperty("--t", RM ? 1 : t.toFixed(3));
};
addEventListener("scroll", onScroll, { passive: true });
requestAnimationFrame(onScroll); // reading scrollY during load forces a layout

/* ---------------- reveal ---------------- */
const ro = new IntersectionObserver((es) => es.forEach((e) => { if (e.isIntersecting) { e.target.classList.add("in"); ro.unobserve(e.target); } }), { threshold: 0.12, rootMargin: "0px 0px -40px 0px" });
$$("[data-r]").forEach((el) => ro.observe(el));

// pointer spotlight on cards
$$(".card").forEach((c) => c.addEventListener("pointermove", (e) => {
  const b = c.getBoundingClientRect();
  c.style.setProperty("--mx", (e.clientX - b.left) + "px");
  c.style.setProperty("--my", (e.clientY - b.top) + "px");
}));

/* ---------------- hero LED field ---------------- */
(function field() {
  const c = $("#field"), x = c.getContext("2d");
  let w = 0, h = 0, pts = [], vis = true;
  const G = 24;
  function size() {
    const dpr = Math.min(2, devicePixelRatio || 1);
    w = c.clientWidth; h = c.clientHeight;
    c.width = w * dpr; c.height = h * dpr; x.setTransform(dpr, 0, 0, dpr, 0, 0);
    pts = [];
    for (let yy = G / 2; yy < h; yy += G) for (let xx = (w % G) / 2; xx < w; xx += G)
      pts.push({ x: xx, y: yy, ph: Math.random() * 6.28, f: rnd(.4, 1.4), on: Math.random() < .8, fl: 0 });
  }
  function draw(now) {
    const t = now / 1000;
    x.clearRect(0, 0, w, h);
    const cx = w / 2, cy = h * .32, R = Math.max(w, h) * .62;
    const wave = ((t % 7) / 7) * R * 1.2;
    if (!RM && Math.random() < .5) { const p = pts[(Math.random() * pts.length) | 0]; if (p) { p.on = !p.on; p.fl = 1; } }
    for (const p of pts) {
      const d = Math.hypot(p.x - cx, (p.y - cy) * 1.3);
      const fade = 1 - d / R; if (fade <= 0) continue;
      const ring = Math.exp(-((d - wave) ** 2) / 3200) * (1 - wave / (R * 1.2));
      const tw = .5 + .5 * Math.sin(t * p.f + p.ph);
      let a = (p.on ? .10 + .16 * tw : .05) * fade + ring * .45 * fade;
      let col = p.on ? "159,214,189" : "144,148,166";
      if (p.fl > 0) { a += p.fl * .7 * fade; col = p.on ? "196,232,214" : "240,166,168"; p.fl = Math.max(0, p.fl - .02); }
      x.fillStyle = `rgba(${col},${a.toFixed(3)})`;
      const s = 2 + ring * 1.5 + p.fl;
      x.fillRect(p.x - s / 2, p.y - s / 2, s, s);
    }
    if (vis && !RM) requestAnimationFrame(draw);
  }
  idle(() => {
    size(); addEventListener("resize", size);
    new IntersectionObserver(([e]) => { const was = vis; vis = e.isIntersecting; if (vis && !was && !RM) requestAnimationFrame(draw); }).observe(c);
    requestAnimationFrame(draw);
  });
})();

/* ================= HERO: replay of the real app ================= */
// Frames were recorded from the g-systemctl binary (tools/demo-capture). The first
// one is already in the HTML; the rest load once the page is idle.
(function hero() {
  const scr = $("#scr");
  const kcKeys = $("#kc-keys"), kcCap = $("#kc-cap");
  const wait = gate($("#term"), 0.05);
  let capText = "";
  function caption(t) {
    if (t == null || t === capText) return;
    capText = t; kcCap.classList.add("out");
    setTimeout(() => { kcCap.textContent = t; kcCap.classList.remove("out"); }, 200);
  }
  function keys(ks) { if (ks.length) kcKeys.innerHTML = ks.map((k) => `<kbd>${esc(k)}</kbd>`).join(""); }

  idle(async () => {
    const [{ default: demo }, { frameHtml, findFrame }] = await Promise.all([import("../data/demo.json"), import("../lib/term.js")]);
    if (RM) {
      scr.innerHTML = frameHtml(demo, findFrame(demo, "Compose project: services, merged logs, graphs") + 3);
      caption("Compose project: services, merged logs, graphs");
      return;
    }
    for (;;) {
      for (let i = 0; i < demo.frames.length; i++) {
        const f = demo.frames[i];
        await wait.ready();
        scr.innerHTML = frameHtml(demo, i);
        if (f.k.length) keys(f.k); else if (i === 0) kcKeys.innerHTML = "";
        caption(f.c);
        await wait(f.h);
      }
    }
  });
})();

/* ================= LEVELS ================= */
// Steps through the three recorded stills; a click on a step jumps to it.
idle(function levels() {
  const lv = $("#lv"), ol = $("#steps"), ind = $(".ind", ol), steps = $$("li", ol);
  let level = 0, paused = false;
  // Moves the highlight onto the active step. Measured, so it also works when the steps stack on narrow screens.
  const place = () => {
    const li = steps[level];
    ind.style.transform = `translate(${li.offsetLeft}px, ${li.offsetTop}px)`;
    ind.style.width = li.offsetWidth + "px";
    ind.style.height = li.offsetHeight + "px";
  };
  const show = (n) => {
    if (n === level) return;
    lv.dataset.dir = n > level ? "down" : "up";
    level = n; lv.dataset.level = n;
    steps.forEach((s, i) => s.classList.toggle("on", i === n));
    place();
  };
  ol.classList.add("moving");
  ind.style.transition = "none";
  place();
  ind.offsetWidth; // apply the start position before transitions turn back on
  ind.style.transition = "";
  new ResizeObserver(place).observe(ol);
  steps.forEach((s, i) => s.querySelector("button").addEventListener("click", () => { paused = true; show(i); }));
  if (RM) return;
  const wait = gate(lv, 0.3);
  (async () => { for (;;) { await wait(2800); if (!paused) show((level + 1) % 3); } })();
});

/* ================= INSTALL ================= */
idle(function install() {
  const R = "https://github.com/shakg/g-systemctl/releases/latest/download";
  const T = {
    deb: { title: "bash — Debian / Ubuntu", lines: [["c", "# amd64 (use -arm64.deb on Raspberry Pi OS 64-bit, Graviton, ...)"], ["$", `curl -LO ${R}/g-systemctl-linux-amd64.deb`], ["$", "sudo apt install ./g-systemctl-linux-amd64.deb"], ["$", "sudo g-systemctl"]] },
    bin: { title: "bash — glibc 2.34+", lines: [["c", "# x86_64 or aarch64, picked by uname -m"], ["$", `curl -LO ${R}/g-systemctl-linux-$(uname -m).tar.gz`], ["$", "tar -xzf g-systemctl-linux-$(uname -m).tar.gz"], ["$", "sudo install -m 755 g-systemctl-*/g-systemctl /usr/local/bin/g-systemctl"], ["$", "sudo g-systemctl"]] },
    brew: { title: "zsh — macOS", lines: [["c", "# Download the formula from the latest release"], ["$", `curl -LO ${R}/g-systemctl.rb`], ["$", "brew install ./g-systemctl.rb"], ["$", "g-systemctl"]] },
    src: { title: "bash — CMake 3.14+, GCC 7+ / Clang 5+", lines: [["$", "git clone https://github.com/shakg/g-systemctl.git"], ["$", "cd g-systemctl && mkdir build && cd build"], ["$", "cmake -DCMAKE_BUILD_TYPE=Release .."], ["$", "make -j$(nproc)"], ["$", "sudo make install"]] },
  };
  const body = $("#ibody"), title = $("#i-title"), tabs = $$(".itab"), copy = $("#copy");
  let cur = "deb", token = 0;
  const lineHtml = (k, s) => k === "c" ? `<span class="c">${esc(s)}</span>` : `<span class="p">$</span> ${esc(s)}`;
  async function show(k, animate = true) {
    const my = ++token; cur = k;
    tabs.forEach((t) => t.setAttribute("aria-selected", String(t.dataset.k === k)));
    title.textContent = T[k].title;
    const L = T[k].lines;
    if (!animate || RM) { body.innerHTML = L.map(([a, b]) => lineHtml(a, b)).join("\n") + "\n<span class=\"p\">$</span> <span class=\"caret\"></span>"; return; }
    let done = "";
    for (const [a, s] of L) {
      for (let i = 1; i <= s.length; i += 2) {
        if (my !== token) return;
        body.innerHTML = done + (a === "c" ? `<span class="c">${esc(s.slice(0, i))}</span>` : `<span class="p">$</span> ${esc(s.slice(0, i))}`) + '<span class="caret"></span>';
        await sleep(a === "c" ? 6 : 12);
      }
      done += lineHtml(a, s) + "\n";
      await sleep(140);
    }
    if (my === token) body.innerHTML = done + '<span class="p">$</span> <span class="caret"></span>';
  }
  tabs.forEach((t) => t.addEventListener("click", () => show(t.dataset.k)));
  if (/Mac/i.test(navigator.platform || navigator.userAgent) && !/iPhone|iPad/i.test(navigator.userAgent)) cur = "brew";
  show(cur, false);
  let typed = false;
  new IntersectionObserver(([e]) => { if (e.isIntersecting && !typed) { typed = true; show(cur); } }, { threshold: .5 }).observe(body);
  copy.addEventListener("click", async () => {
    const text = T[cur].lines.filter((l) => l[0] === "$").map((l) => l[1]).join("\n");
    try {
      if (navigator.clipboard && isSecureContext) await navigator.clipboard.writeText(text);
      else { const ta = document.createElement("textarea"); ta.value = text; ta.style.cssText = "position:fixed;left:-9999px"; document.body.appendChild(ta); ta.select(); document.execCommand("copy"); ta.remove(); }
      copy.textContent = "Copied"; copy.classList.add("done");
    } catch { copy.textContent = "Copy failed"; }
    setTimeout(() => { copy.textContent = "Copy"; copy.classList.remove("done"); }, 1600);
  });
});
