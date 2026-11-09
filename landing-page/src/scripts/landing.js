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

// Block-character bar graph, `h` rows tall.
const BL = " ▁▂▃▄▅▆▇█";
function blocks(vals, width, h, max, lo = 0) {
  const v = vals.slice(-width); while (v.length < width) v.unshift(0);
  const rows = [];
  for (let r = 0; r < h; r++) {
    let s = "";
    for (const x of v) {
      const lvl = (Math.max(0, x - lo) / ((max - lo) || 1)) * h * 8;
      let n = clamp(Math.round(lvl - (h - 1 - r) * 8), 0, 8);
      if (r === h - 1 && x > 0 && n === 0) n = 1;
      s += BL[n];
    }
    rows.push(s);
  }
  return rows;
}
const walk = (prev, lo, hi, step) => clamp(prev + rnd(-step, step), lo, hi);

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

/* ================= HERO TUI ================= */
(function hero() {
  const COLS = 88, ROWS = 24;
  const scr = $("#scr");
  const wait = gate($("#term"), 0.05);

  const len = (segs) => segs.reduce((n, s) => n + s[0].length, 0);
  function line(segs, cls = "") {
    let n = 0, html = "";
    for (const [t, c] of segs) { n += t.length; html += c ? `<span class="${c}">${esc(t)}</span>` : esc(t); }
    if (n < COLS) html += " ".repeat(COLS - n);
    return `<div class="r ${cls}">${html}</div>`;
  }
  function spread(left, right, cls) { return line([...left, [" ".repeat(Math.max(1, COLS - len(left) - len(right)))], ...right], cls); }
  const pad = (s, n) => (s.length > n ? s.slice(0, n - 1) + "…" : s + " ".repeat(n - s.length));
  const lpad = (s, n) => (s.length >= n ? s.slice(0, n) : " ".repeat(n - s.length) + s);

  const SVCS = [
    ["accounts-daemon.service", 1, "703", "0.0%", "7.2MB"],
    ["bluetooth.service", 0],
    ["containerd.service", 1, "921", "0.3%", "41.8MB"],
    ["cron.service", 1, "640", "0.0%", "2.1MB"],
    ["cups.service", 0],
    ["docker.service", 1, "980", "0.8%", "94.2MB"],
    ["NetworkManager.service", 1, "711", "0.1%", "18.9MB"],
    ["nginx.service", 1, "1184", "0.3%", "12.4MB"],
    ["nginx-exporter.service", 1, "2011", "0.1%", "9.8MB"],
    ["postgresql.service", 1, "1302", "1.2%", "186.0MB"],
    ["redis-server.service", 1, "1250", "0.4%", "8.1MB"],
    ["ssh.service", 1, "812", "0.0%", "6.3MB"],
    ["systemd-journald.service", 1, "402", "0.2%", "24.6MB"],
    ["systemd-resolved.service", 1, "588", "0.0%", "12.0MB"],
    ["ufw.service", 1, "-", "-", "-"],
    ["unattended-upgrades.service", 0],
    ["wpa_supplicant.service", 0],
  ];
  const CTRS = [
    { n: "web", i: "nginx:1.27-alpine", on: 1, up: "Up 3 hours", c: 0.4, m: 18, lim: "512MiB", p: 3 },
    { n: "api", i: "node:22-slim", on: 1, up: "Up 3 hours", c: 12.7, m: 214, lim: "1GiB", p: 23 },
    { n: "postgres", i: "postgres:16", on: 1, up: "Up 2 days", c: 2.1, m: 96, lim: "2GiB", p: 9 },
    { n: "redis", i: "redis:7.4", on: 1, up: "Up 2 days", c: 0.3, m: 8, lim: "256MiB", p: 6 },
    { n: "worker", i: "python:3.12-slim", on: 1, up: "Up 41 minutes", c: 31.0, m: 388, lim: "1GiB", p: 14 },
    { n: "traefik", i: "traefik:v3.1", on: 1, up: "Up 2 days", c: 0.6, m: 41, lim: "256MiB", p: 11 },
    { n: "minio", i: "minio/minio", on: 1, up: "Up 2 days", c: 1.1, m: 152, lim: "1GiB", p: 18 },
    { n: "loki", i: "grafana/loki:3.2", on: 1, up: "Up 5 hours", c: 3.8, m: 121, lim: "512MiB", p: 15 },
    { n: "grafana", i: "grafana/grafana", on: 0, up: "Exited (0) 2h", c: 0, m: 0, lim: "", p: 0 },
    { n: "mailhog", i: "mailhog/mailhog", on: 0, up: "Exited (2) 3d", c: 0, m: 0, lim: "", p: 0 },
  ];

  let S;
  function reset() {
    S = {
      tab: "svc", filter: "", sel: 0, view: "list", dialog: null, state: "active", pid: "1184",
      logs: [], cpu: [], mem: [], tasks: [], io: [], clock: 9 * 3600 + 41 * 60 + 7, status: "", edit: null, flash: "", shake: false,
    };
    for (let i = 0; i < 42; i++) sample();
    for (let i = 0; i < 5; i++) addLog(access());
    CTRS[1].lim = "1GiB";
  }
  function sample() {
    const spike = S.state !== "active" ? 1 : 0;
    S.cpu.push(spike ? rnd(6, 9) : walk(S.cpu.at(-1) ?? 1, 0.2, 4.2, 0.9));
    S.mem.push(spike ? rnd(4, 7) : walk(S.mem.at(-1) ?? 12, 10.5, 13.8, 0.4));
    S.tasks.push(S.state === "active" ? 5 : 1);
    S.io.push(Math.random() < .25 ? rnd(40, 220) : rnd(2, 30));
  }
  const hhmmss = (s) => [Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60].map((n) => String(n).padStart(2, "0")).join(":");
  const IPS = ["192.168.1.24", "10.0.0.7", "192.168.1.31", "10.0.0.12", "172.17.0.3"];
  const REQS = [["GET", "/api/health", 200, 2], ["GET", "/", 200, 5120], ["GET", "/assets/app.js", 200, 48211], ["POST", "/api/login", 302, 0], ["GET", "/favicon.ico", 404, 153], ["GET", "/api/orders?page=2", 200, 7724]];
  function access() { const q = REQS[(Math.random() * REQS.length) | 0]; return { src: `nginx[${S ? S.pid : 1184}]`, msg: `${IPS[(Math.random() * IPS.length) | 0]} "${q[0]} ${q[1]} HTTP/2.0" ${q[2]} ${q[3]}` }; }
  function addLog(l) { S.clock += 1 + ((Math.random() * 3) | 0); S.logs.push({ ...l, ts: "Nov 05 " + hhmmss(S.clock), fresh: true }); if (S.logs.length > 40) S.logs.shift(); }

  function topBar() {
    const svc = S.tab === "svc";
    return spread(
      [[" g-systemctl ", "a b"], [" "], [" Services ", svc ? "tabon" : "m"], ["│", "ln"], [" Containers ", svc ? "m" : "tabon"]],
      [[svc ? " Reload(Alt+R) " : " Refresh(Alt+R) ", "m"], [" Shortcuts(?) ", "m"]], "pn");
  }
  function hints(text) { return line([[" " + text, "m"]], "pn"); }
  function filterRow() { return line([[" Filter: ", "m"], [S.filter, "w"], ["▏", "cur"]]); }
  function fill(rows, n) { while (rows.length < n) rows.push(line([])); }

  function listView() {
    const rows = [topBar(), filterRow()];
    const W = COLS - 1 - 11 - 7 - 7 - 8;
    rows.push(line([[" "], [pad("SERVICE", W), "h"], [pad("STATUS", 11), "h"], [lpad("PID", 7), "h"], [lpad("CPU", 7), "h"], [lpad("MEM", 8), "h"]], "pn"));
    const items = SVCS.filter((s) => s[0].toLowerCase().includes(S.filter));
    items.slice(0, ROWS - 5).forEach((s, i) => {
      const sel = i === S.sel, on = s[1];
      rows.push(line([[sel ? "▌" : " ", "a"], [pad(s[0], W), sel ? "w b" : "t"], [pad(on ? "● active" : "○ inactive", 11), on ? "a b" : "off"],
      [lpad(s[2] || "-", 7), "m"], [lpad(s[3] || "-", 7), "m"], [lpad(s[4] || "-", 8), "m"]], sel ? "sel" : ""));
    });
    fill(rows, ROWS - 2);
    rows.push(line([[` ${items.length} of ${SVCS.length} services · --system`, "m"]]));
    rows.push(hints("Enter details  Alt+o start  Alt+x stop  Alt+r restart  Tab containers  ? help"));
    return rows;
  }

  function stateSeg() {
    if (S.state === "active") return ["● active (running)", "a b"];
    if (S.state === "deactivating") return ["● deactivating (stop-sigterm)", "wa b"];
    return ["● activating (start)", "wa b"];
  }
  function titleRow() {
    return spread([[" nginx.service ", "w b"], [" "], stateSeg()],
      [[" ▶ Start ", "bt"], [" "], [" ■ Stop ", "bt"], [" "], [" ↻ Restart ", "bt" + (S.flash === "restart" ? " flash" : "")], [" "], [" ← Back ", "bt"], [" "]], "pn");
  }
  function logRows(n) {
    return S.logs.slice(-n).map((l) => {
      const r = line([[" " + l.ts + " ", "m"], ["web ", "m"], [l.src + ": ", l.sys ? "wa" : "a"], [l.msg, l.sys ? "t" : "t"]], l.fresh ? "newrow" : "");
      l.fresh = false; return r;
    });
  }
  function detailView(full) {
    const rows = [topBar(), titleRow()];
    if (!full) {
      rows.push(line([["─".repeat(COLS), "ln"]]));
      const st = S.state === "active" ? "active (running) since 09:41:07" : S.state;
      const sum = [
        ["Description", "A high performance web server", "t"],
        ["State", st, S.state === "active" ? "a" : "wa"],
        ["Unit file", "enabled · /lib/systemd/system/nginx.service", "t"],
        ["Type", "forking", "t"],
        ["Main PID", S.state === "active" ? S.pid : "-", "t"],
        ["Tasks", String(S.tasks.at(-1)), "t"],
        ["Memory", S.mem.at(-1).toFixed(1) + " MiB", "t"],
        ["CPU time", (2.481 + S.cpu.length * 0.013).toFixed(3) + "s", "t"],
        ["Restart", "on-failure", "t"],
        ["User", "root", "t"],
        ["Command", "/usr/sbin/nginx", "w"],
        ["", "  -g daemon on; master_process on;", "a"],
        ["Wanted by", "multi-user.target", "t"],
      ];
      const GW = 41;
      const g = [];
      const metric = (name, vals, max, fmt, cls, lo = 0) => {
        const last = vals.at(-1), mx = Math.max(...vals.slice(-GW));
        const l = ` ${name}  ${fmt(last)}`, r = `max ${fmt(mx)} `;
        g.push([[l, "t"], [" ".repeat(Math.max(1, 43 - l.length - r.length))], [r, "m"]]);
        blocks(vals, GW, 2, max, lo).forEach((s) => g.push([[" "], [s, cls + " gb"], [" "]]));
      };
      g.push([]);
      metric("CPU", S.cpu, 9, (v) => v.toFixed(1) + "%", "a");
      metric("Memory", S.mem, 15, (v) => v.toFixed(1) + " MiB", "a", 8);
      metric("Tasks", S.tasks, 10, (v) => String(Math.round(v)), "a");
      metric("IO", S.io, 240, (v) => v.toFixed(0) + " KB/s", "wa");
      for (let i = 0; i < 13; i++) {
        const [k, v, c] = sum[i];
        rows.push(line([[" "], [pad(k, 12), "m"], [pad(v, 31), c], ["│", "ln"], ...(g[i] || [])]));
      }
    }
    rows.push(spread([[" Logs ", "w b"], [" journalctl -u nginx.service -f", "m"]],
      [[full ? " ▤ Info " : " ▦ Hide graphs ", "m"], [full ? " ⤡ Shrink " : " ⤢ Expand ", "a"], [" "]], "pn"));
    const n = ROWS - rows.length - 1;
    rows.push(...logRows(n));
    fill(rows, ROWS - 1);
    rows.push(hints(full ? "Enter shrink  PgUp/PgDn scroll  Esc back  Alt+r restart" : "Enter expand logs  Alt+g graphs  Alt+o/x/r start/stop/restart  Esc back"));
    return rows;
  }

  function ctrView() {
    const rows = [topBar(), filterRow()];
    const W = COLS - 1 - 18 - 10 - 15 - 7 - 18 - 6;
    rows.push(line([[" "], [pad("CONTAINER", W), "h"], [pad("IMAGE", 18), "h"], [pad("STATE", 10), "h"], [pad("STATUS", 15), "h"], [lpad("CPU", 7), "h"], [lpad("MEM", 18), "h"], [lpad("PIDS", 6), "h"]], "pn"));
    CTRS.forEach((c, i) => {
      const sel = i === S.sel;
      const mem = c.on ? `${c.m.toFixed(0)}MiB / ${c.lim}` : "-";
      rows.push(line([[sel ? "▌" : " ", "a"], [pad(c.n, W), sel ? "w b" : "t"], [pad(c.i, 17) + " ", "m"], [pad(c.on ? "running" : "exited", 10), c.on ? "a b" : "off"],
      [pad(c.up, 15), "m"], [lpad(c.on ? c.c.toFixed(1) + "%" : "-", 7), c.on && c.c > 20 ? "wa" : "t"], [lpad(mem, 18), "t"], [lpad(c.on ? String(c.p) : "-", 6), "m"]], sel ? "sel" : ""));
    });
    fill(rows, ROWS - 2);
    rows.push(line([[" " + (S.status || `${CTRS.length} containers · docker · refreshing every 2s`), S.status ? "a" : "m"]]));
    rows.push(hints("Enter details  Alt+u limits  Alt+s secrets  Alt+o/x/r start/stop/restart  Tab services"));
    return rows;
  }

  function dialogHtml() {
    if (S.dialog === "confirm") {
      return `<div class="dlg${S.shake ? " shake" : ""}" style="left:21ch;top:calc(7 * var(--lh));width:46ch">` +
        `<div class="r"><span class="wa b"> Confirm</span></div><div class="sep"></div>` +
        `<div class="r"> Restart nginx.service?</div><div class="r"> </div>` +
        `<div class="r" style="text-align:right"><span class="yes"> y  Restart </span>   <span class="no"> n  Cancel </span> </div>` +
        `<div class="sep"></div><div class="r m"> y confirm · n / Esc cancel</div></div>`;
    }
    if (S.dialog === "limits") {
      const E = S.edit;
      const f = (i, label, v) => `<div class="r"> <span class="m">${pad(label, 16)}</span><span class="fld${E.field === i ? " on" : ""}"> ${esc(pad(v + (E.field === i ? "▏" : ""), 18))}</span></div>`;
      return `<div class="dlg" style="left:18ch;top:calc(5 * var(--lh));width:52ch">` +
        `<div class="r"> <span class="a b">Resources  </span><span class="wa">api</span></div><div class="sep"></div>` +
        f(0, "CPUs", E.cpus) + f(1, "Memory", E.mem) + f(2, "Memory + swap", E.swap) + f(3, "PIDs limit", E.pids) + f(4, "Restart policy", E.policy) +
        `<div class="r"> </div><div class="r m"> Applied live with \`docker update\`. Env, ports and</div><div class="r m"> volumes need the container to be recreated.</div>` +
        `<div class="sep"></div><div class="r m"> Tab/↑↓ field  Enter apply  Esc cancel</div></div>`;
    }
    return "";
  }

  function render() {
    let rows;
    if (S.tab === "ctr") rows = ctrView();
    else if (S.view === "list") rows = listView();
    else rows = detailView(S.view === "logs");
    scr.innerHTML = rows.join("") + dialogHtml();
    scr.classList.toggle("dimmed", !!S.dialog);
  }

  // keycast
  const kcKeys = $("#kc-keys"), kcCap = $("#kc-cap");
  let capText = "";
  function key(keys, cap) {
    kcKeys.innerHTML = keys.map((k) => `<kbd>${esc(k)}</kbd>`).join("");
    if (cap !== undefined && cap !== capText) caption(cap);
  }
  function caption(t) {
    capText = t; kcCap.classList.add("out");
    setTimeout(() => { kcCap.textContent = t; kcCap.classList.remove("out"); }, 200);
  }

  // live tickers
  let liveT = null, ctrT = null;
  function startLive() {
    stopLive();
    let k = 0;
    liveT = setInterval(() => {
      if (!wait.visible()) return;
      sample(); if (k++ % 2 === 0 && S.state === "active") addLog(access());
      if (S.view !== "list" && S.tab === "svc") render();
    }, 650);
  }
  function stopLive() { clearInterval(liveT); liveT = null; }
  function startCtr() {
    stopCtr();
    ctrT = setInterval(() => {
      if (!wait.visible()) return;
      CTRS.forEach((c) => { if (c.on) { c.c = walk(c.c, 0.1, c.n === "worker" ? 48 : c.n === "api" ? 24 : 4, c.n === "worker" ? 6 : 1.5); c.m = walk(c.m, c.m * .97, c.m * 1.03, c.m * .02); } });
      render();
    }, 900);
  }
  function stopCtr() { clearInterval(ctrT); ctrT = null; }

  async function type(field, text, ms = 150) { for (const ch of text) { field(ch); render(); await wait(ms); } }

  async function loop() {
    for (; ;) {
      reset(); stopCtr(); render();
      key([], "All units, with status, PID, CPU and memory");
      await wait(2000);
      caption("Type to filter");
      for (const ch of "nginx") { S.filter += ch; S.sel = 0; key([ch]); render(); await wait(240); }
      await wait(900);
      key(["↓"]); S.sel = 1; render(); await wait(500);
      key(["↑"]); S.sel = 0; render(); await wait(600);
      key(["Enter"], "Detail page: summary, graphs and logs");
      S.view = "detail"; render(); startLive(); await wait(4200);
      key(["Alt", "r"], "Restart asks for confirmation");
      S.flash = "restart"; S.dialog = "confirm"; render(); S.flash = ""; await wait(1700);
      key(["Enter"], "Enter is ignored in the prompt");
      S.shake = true; render(); S.shake = false; await wait(1900);
      key(["y"], "y confirms");
      S.dialog = null; S.state = "deactivating";
      addLog({ src: "systemd[1]", msg: "Stopping nginx.service - A high performance web server...", sys: 1 }); render(); await wait(1100);
      S.state = "activating"; S.pid = String(4000 + ((Math.random() * 900) | 0));
      addLog({ src: "systemd[1]", msg: "nginx.service: Deactivated successfully.", sys: 1 });
      addLog({ src: "systemd[1]", msg: "Starting nginx.service - A high performance web server...", sys: 1 }); render(); await wait(1100);
      S.state = "active";
      addLog({ src: "systemd[1]", msg: "Started nginx.service - A high performance web server.", sys: 1 }); render(); await wait(2200);
      key(["Enter"], "Full-size logs");
      S.view = "logs"; render(); await wait(3000);
      key(["Esc"], "Back one level");
      S.view = "detail"; render(); await wait(1100);
      key(["Esc"]); S.view = "list"; stopLive(); render(); await wait(1000);
      key(["Esc"], "On the list, Esc clears the filter");
      S.filter = ""; S.sel = 7; render(); await wait(1300);
      key(["Tab"], "Containers tab");
      S.tab = "ctr"; S.sel = 0; render(); startCtr(); await wait(1600);
      key(["↓"]); S.sel = 1; render(); await wait(1600);
      key(["Alt", "u"], "Edit limits with docker update");
      S.edit = { field: 0, cpus: "1.00", mem: "1g", swap: "2g", pids: "200", policy: "unless-stopped" };
      S.dialog = "limits"; render(); await wait(1100);
      S.edit.cpus = ""; render(); await wait(250);
      await type((c) => (S.edit.cpus += c), "2.00");
      key(["Tab"]); S.edit.field = 1; render(); await wait(450);
      S.edit.mem = ""; render(); await wait(200);
      await type((c) => (S.edit.mem += c), "2g");
      await wait(500);
      key(["Enter"], "Applied while the container runs");
      S.dialog = null; CTRS[1].lim = "2GiB"; S.status = "Updated api: --cpus 2.00 --memory 2g"; render(); await wait(2800);
      key(["Tab"], ""); S.tab = "svc"; stopCtr(); S.status = "";
    }
  }

  if (RM) {
    reset(); S.filter = "nginx"; S.view = "detail"; render();
    caption("Detail page: summary, graphs and logs");
  } else {
    loop();
  }
})();

/* Everything below the hero starts once the page is idle. */

/* ================= LEVELS ================= */
idle(function levels() {
  const lv = $("#lv"), list = $("#lv-list"), keyEl = $("#lv-key"), steps = $$("#steps li");
  const names = [["docker.service", 1, 70], ["cron.service", 1, 40], ["nginx.service", 1, 55], ["postgresql.service", 1, 85], ["cups.service", 0, 30], ["ssh.service", 1, 45], ["redis-server.service", 1, 60]];
  list.innerHTML = names.map(([n, on, w], i) => `<div class="lv-row${on ? "" : " offr"}${i === 2 ? " cur" : ""}"><span class="dot"></span><span>${n}</span><span class="sp"></span><span class="sk" style="width:${w}px"></span></div>`).join("");
  const gr = $("#lv-gr");
  gr.innerHTML = ["CPU", "Memory", "Tasks"].map((n) => `<div><div class="gl">${n} <b>–</b></div><div class="lv-gb">${"<i></i>".repeat(28)}</div></div>`).join("");
  const ll = $("#lv-ll");
  const lines = ['"GET /api/health" 200', '"GET / HTTP/2.0" 200 5120', '"POST /api/login" 302', '"GET /assets/app.js" 200', '"GET /favicon.ico" 404', '"GET /api/orders" 200'];
  let sec = 7;
  function addLine() {
    sec++; const d = document.createElement("div");
    d.innerHTML = `<span class="ts">09:41:${String(sec % 60).padStart(2, "0")}</span> nginx[1184]: ${esc(lines[(Math.random() * lines.length) | 0])}`;
    ll.appendChild(d); while (ll.children.length > 18) ll.firstChild.remove();
  }
  for (let i = 0; i < 18; i++) addLine();
  function setRow() {
    const row = list.children[2], b = lv.getBoundingClientRect(), r = row.getBoundingClientRect();
    const top = r.top - b.top - 34, h = b.height - 34;
    lv.style.setProperty("--rt", (top / h * 100).toFixed(2) + "%");
    lv.style.setProperty("--rb", ((h - top - r.height) / h * 100).toFixed(2) + "%");
  }
  setRow(); addEventListener("resize", setRow);
  const wait = gate(lv, 0.3);
  function bars() {
    $$(".lv-gb", gr).forEach((g) => $$("i", g).forEach((i) => (i.style.height = rnd(15, 100).toFixed(0) + "%")));
    $$(".gl b", gr).forEach((b, i) => (b.textContent = [rnd(.2, 3).toFixed(1) + "%", rnd(11, 13).toFixed(1) + " MiB", "5"][i]));
  }
  function level(n, k) {
    lv.dataset.level = n;
    steps.forEach((s, i) => s.classList.toggle("on", i === n));
    $("#lv-exp").textContent = n === 2 ? "⤡ Shrink" : "⤢ Expand";
    if (k) { keyEl.innerHTML = `<kbd class="hit">${k}</kbd>`; }
  }
  bars();
  if (RM) { level(1); return; }
  setInterval(() => { if (wait.visible()) { bars(); addLine(); } }, 900);
  (async () => {
    for (; ;) {
      level(0, "↓"); await wait(1800);
      level(1, "Enter"); await wait(2600);
      level(2, "Enter"); await wait(2600);
      level(1, "Esc"); await wait(1800);
      level(0, "Esc"); await wait(1400);
    }
  })();
});

/* ================= KEYMAP ================= */
idle(function keymap() {
  const root = $("#keymap");
  const tiles = $$(".kt", root);
  if (RM) return;
  const wait = gate(root, .3);
  (async () => { let i = 0; for (; ;) { tiles.forEach((t) => t.classList.remove("hot")); const t = tiles[i++ % tiles.length]; void t.offsetWidth; t.classList.add("hot"); await wait(1100); } })();
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
