#!/usr/bin/env python3
"""Stand-in for systemctl, journalctl, ps, docker and sudo, so the real g-systemctl
binary can be run and screenshotted in a container with no systemd or Docker."""
import os
import sys
import time
import random

prog = os.path.basename(sys.argv[0])
args = sys.argv[1:]

# name, active, pid, cpu%, rss_kb, description
SVCS = [
    ("accounts-daemon.service", 1, 703, "0.0", 7373, "Accounts Service"),
    ("bluetooth.service", 0, 0, "", 0, "Bluetooth service"),
    ("containerd.service", 1, 921, "0.3", 42803, "containerd container runtime"),
    ("cron.service", 1, 640, "0.0", 2150, "Regular background program processing daemon"),
    ("cups.service", 0, 0, "", 0, "CUPS Scheduler"),
    ("docker.service", 1, 980, "0.8", 96460, "Docker Application Container Engine"),
    ("NetworkManager.service", 1, 711, "0.1", 19353, "Network Manager"),
    ("nginx.service", 1, 1184, "0.3", 12697, "A high performance web server and a reverse proxy server"),
    ("nginx-exporter.service", 1, 2011, "0.1", 10035, "NGINX Prometheus Exporter"),
    ("postgresql.service", 1, 1302, "1.2", 190464, "PostgreSQL RDBMS"),
    ("redis-server.service", 1, 1250, "0.4", 8294, "Advanced key-value store"),
    ("ssh.service", 1, 812, "0.0", 6451, "OpenBSD Secure Shell server"),
    ("systemd-journald.service", 1, 402, "0.2", 25190, "Journal Service"),
    ("systemd-resolved.service", 1, 588, "0.0", 12288, "Network Name Resolution"),
    ("ufw.service", 1, 0, "", 0, "Uncomplicated firewall"),
    ("unattended-upgrades.service", 0, 0, "", 0, "Unattended Upgrades Shutdown"),
    ("wpa_supplicant.service", 0, 0, "", 0, "WPA supplicant"),
]
BY = {s[0]: s for s in SVCS}
START = time.time() - 3600  # counters grow steadily with wall time


def systemctl():
    a = [x for x in args if x != "--user"]
    if a[:1] == ["list-units"]:
        for n, on, pid, *_rest in SVCS:
            sub = "running" if on and pid else ("exited" if on else "dead")
            print(f"{n} loaded {'active' if on else 'inactive'} {sub} {BY[n][5]}")
    elif a[:1] == ["list-unit-files"]:
        for n, *_ in SVCS:
            print(f"{n} enabled enabled")
    elif a[:1] == ["show"] and "--value" in a:
        for n in [x for x in a[1:] if x.endswith(".service")]:
            print(BY[n][2])
    elif a[:1] == ["show"]:
        n = a[1]
        on, pid, rss = BY[n][1], BY[n][2], BY[n][4]
        el = time.time() - START
        props = a[a.index("-p") + 1].split(",")
        exec_start = {
            "nginx.service": "{ path=/usr/sbin/nginx ; argv[]=/usr/sbin/nginx -g daemon on; master_process on; ; ignore_errors=no ; start_time=[n/a] ; stop_time=[n/a] ; pid=0 ; code=(null) ; status=0/0 }",
        }.get(n, "{ path=/usr/bin/" + n.split(".")[0] + " ; argv[]=/usr/bin/" + n.split(".")[0] + " ; ignore_errors=no ; start_time=[n/a] ; stop_time=[n/a] ; pid=0 ; code=(null) ; status=0/0 }")
        vals = {
            "Description": BY[n][5], "LoadState": "loaded", "ActiveState": "active" if on else "inactive",
            "SubState": "running" if on and pid else "dead", "UnitFileState": "enabled",
            "FragmentPath": f"/lib/systemd/system/{n}", "Type": "forking" if n == "nginx.service" else "simple",
            "MainPID": str(pid), "ActiveEnterTimestamp": "Wed 2026-11-05 09:41:07 UTC", "InactiveEnterTimestamp": "",
            "TasksCurrent": "5" if on else "[not set]", "MemoryCurrent": str(rss * 1024 + int(random.random() * 300000)) if on else "[not set]",
            "CPUUsageNSec": str(int(2.481e9 + el * 3e7 + random.random() * 2e7)) if on else "[not set]",
            "IOReadBytes": str(int(1843200 + el * 4000)), "IOWriteBytes": str(int(40960 + el * 9000 + random.random() * 30000)),
            "Restart": "on-failure", "NRestarts": "0", "User": "" if n == "nginx.service" else "root",
            "ExecStart": exec_start, "WantedBy": "multi-user.target",
        }
        for p in props:
            print(f"{p}={vals.get(p, '')}")
    else:
        sys.exit(0)


def ps():
    pids = args[args.index("-p") + 1].split(",")
    for s in SVCS:
        if str(s[2]) in pids:
            print(f"{s[2]:>6} {s[3]:>4} {s[4]}")


IPS = ["192.168.1.24", "10.0.0.7", "192.168.1.31", "10.0.0.12", "172.17.0.3"]
REQS = [("GET", "/api/health", 200, 2), ("GET", "/", 200, 5120), ("GET", "/assets/app.js", 200, 48211),
        ("POST", "/api/login", 302, 0), ("GET", "/favicon.ico", 404, 153), ("GET", "/api/orders?page=2", 200, 7724)]


def journalctl():
    unit = args[args.index("-u") + 1] if "-u" in args else "nginx.service"
    pid = BY.get(unit, (0, 0, 1184))[2]
    t = 9 * 3600 + 41 * 60
    def line():
        nonlocal t
        t += random.randint(1, 3)
        q = random.choice(REQS)
        ts = f"Nov 05 {t // 3600:02}:{t // 60 % 60:02}:{t % 60:02}"
        print(f'{ts} web nginx[{pid}]: {random.choice(IPS)} "{q[0]} {q[1]} HTTP/2.0" {q[2]} {q[3]}', flush=True)
    for _ in range(30):
        line()
    while "-f" in args:
        time.sleep(0.8)
        line()


CTRS = [  # id, name, image, running, status, ports, project, cpu, mem MiB, limit, pids
    ("3f2a9c1d0b7e", "web", "nginx:1.27-alpine", 1, "Up 3 hours", "0.0.0.0:80->80/tcp", "shop", 0.4, 18, "512MiB", 3),
    ("8b41d27ce9a0", "api", "node:22-slim", 1, "Up 3 hours", "0.0.0.0:8080->8080/tcp", "shop", 12.7, 214, "1GiB", 23),
    ("c0de11aa5f42", "postgres", "postgres:16", 1, "Up 2 days", "5432/tcp", "shop", 2.1, 96, "2GiB", 9),
    ("a7e3f9b2c811", "redis", "redis:7.4", 1, "Up 2 days", "6379/tcp", "shop", 0.3, 8, "256MiB", 6),
    ("5d9e0f7a3c26", "worker", "python:3.12-slim", 1, "Up 41 minutes", "", "shop", 31.0, 388, "1GiB", 14),
    ("e1b2c3d4e5f6", "traefik", "traefik:v3.1", 1, "Up 2 days", "0.0.0.0:443->443/tcp", "", 0.6, 41, "256MiB", 11),
    ("0a1b2c3d4e5f", "minio", "minio/minio", 1, "Up 2 days", "9000-9001/tcp", "", 1.1, 152, "1GiB", 18),
    ("9f8e7d6c5b4a", "loki", "grafana/loki:3.2", 1, "Up 5 hours", "3100/tcp", "", 3.8, 121, "512MiB", 15),
    ("1122aabb3344", "grafana", "grafana/grafana", 0, "Exited (0) 2 hours ago", "", "", 0, 0, "", 0),
    ("5566ccdd7788", "mailhog", "mailhog/mailhog", 0, "Exited (2) 3 days ago", "", "", 0, 0, "", 0),
]


COMPOSE_DIR = "/tmp/shop"  # capture.py creates it with a stub compose.yaml
# Created by `compose up`; lives in the per-run config dir capture.py sets.
UP_MARKER = os.path.join(os.environ.get("XDG_CONFIG_HOME", "/tmp"), "fake-compose-up")
HASH = "5f0c" * 16
DEPENDS = {"web": "api:service_started:false", "api": "postgres:service_healthy:true,redis:service_started:false",
           "worker": "redis:service_started:false"}


def compose():
    # Global flags come before the subcommand; `logs -f` means follow.
    rest, i = [], 1
    while i < len(args):
        if not rest and args[i] in ("-p", "-f", "--project-directory"):
            i += 2
            continue
        rest.append(args[i])
        i += 1
    sub = rest[0] if rest else ""
    if sub == "version":
        print("2.29.7")
    elif sub == "config" and "--help" in rest:
        # The app checks for --hash before running drift checks.
        print("      --hash string   Print the service config hash, one per line.")
    elif sub == "config" and "--hash" in rest:
        # web's compose file changed until `up` recreates it.
        for c in CTRS:
            if c[6]:
                drifted = c[1] == "web" and not os.path.exists(UP_MARKER)
                print(f"{c[1]} {'a1b2' * 16 if drifted else HASH}")
    elif sub == "up":
        open(UP_MARKER, "w").close()
    elif sub == "config":
        print("name: shop\nservices: {}")
    elif sub == "logs":
        lines = {
            "web": lambda: f'172.18.0.1 "GET /assets/app.js HTTP/1.1" 200 {random.randint(4000, 49000)}',
            "api": lambda: f"GET /v1/orders 200 {random.randint(2, 40)}ms",
            "postgres": lambda: "LOG:  checkpoint complete: wrote 42 buffers",
            "redis": lambda: "1:M Background saving terminated with success",
            "worker": lambda: f"processed job #{random.randint(1000, 9999)} in {random.randint(20, 400)}ms",
        }
        svcs = [c[1] for c in CTRS if c[6]]
        while True:
            s = random.choice(svcs)
            print(f"{s + '-1':<11}| {lines[s]()}", flush=True)
            if "-f" not in rest:
                break
            time.sleep(0.6)


def docker():
    cmd = args[0] if args else ""
    if cmd == "version":
        print("27.3.1")
    elif cmd == "ps":
        for c in CTRS:
            labels = ["", "", "", "", "", "", ""]
            if c[6]:  # compose labels: service, number, dir, files, hash, oneoff, depends_on
                labels = [c[1], "1", COMPOSE_DIR, COMPOSE_DIR + "/compose.yaml", HASH, "False",
                          DEPENDS.get(c[1], "")]
            name = f"{c[6]}-{c[1]}-1" if c[6] else c[1]
            print("\t".join([c[0], name, c[2], "running" if c[3] else "exited", c[4], c[5], c[6]] + labels))
    elif cmd == "compose":
        compose()
    elif cmd == "stats":
        for c in CTRS:
            if c[3]:
                cpu = max(0.1, c[7] * (0.8 + random.random() * 0.4))
                print(f"{c[0]}\t{cpu:.2f}%\t{c[8]}MiB / {c[9]}\t{c[8] / 10:.2f}%\t1.2MB / 845kB\t12.3MB / 4.1MB\t{c[10]}")
    elif cmd == "inspect":
        cid = args[-1]
        c = next(x for x in CTRS if x[0].startswith(cid) or cid.startswith(x[0]))
        env = ["NODE_ENV=production", "PORT=8080", "POSTGRES_PASSWORD=s3cr3t-h0rse", "API_TOKEN=tk_live_9f2c81", "LOG_LEVEL=info"]
        out = [f"id\t{c[0]}000000", f"name\t/{c[1]}", f"image\t{c[2]}", f"state\t{'running' if c[3] else 'exited'}",
               "created\t2026-11-03T08:12:44Z", "started\t2026-11-05T06:41:07Z", "restarts\t0",
               'entrypoint\t["docker-entrypoint.sh"]', 'cmd\t["node","server.js","--port","8080"]',
               "nano_cpus\t1000000000", "memory\t1073741824", "memory_swap\t2147483648", "pids_limit\t200",
               "restart_policy\tunless-stopped", "restart_retries\t0", f"compose_project\t{c[6]}", "compose_dir\t/srv/shop"]
        out += [f"env\t{e}" for e in env]
        out += ["mount\t/srv/shop/data\t/app/data\ttrue", "port\t8080/tcp\t0.0.0.0:8080 ", "network\tshop_default\t172.18.0.4"]
        print("\n".join(out))
    elif cmd == "logs":
        t = 9 * 3600 + 42 * 60
        msgs = [("info", "GET /v1/orders 200 12ms"), ("info", "GET /v1/users/42 200 4ms"), ("info", "POST /v1/checkout 201 38ms"),
                ("warn", "slow query 412ms orders_by_user"), ("info", "GET /health 200 1ms")]
        def line():
            nonlocal t
            t += random.randint(1, 2)
            m = random.choice(msgs)
            print(f"2026-11-05T{t // 3600:02}:{t // 60 % 60:02}:{t % 60:02}Z {m[0]} {m[1]}", flush=True)
        for _ in range(20):
            line()
        while "-f" in args:
            time.sleep(0.9)
            line()


def sudo():
    if args[:2] == ["-n", "true"]:
        sys.exit(0)
    a = [x for x in args if not x.startswith("-")]
    os.execvp(a[0], a)


{"systemctl": systemctl, "ps": ps, "journalctl": journalctl, "docker": docker, "sudo": sudo}[prog]()
