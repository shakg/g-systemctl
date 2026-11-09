"""Record the hero demo from the real g-systemctl binary.

Runs build/g-systemctl in a pseudo-terminal with fake systemctl, journalctl, ps,
docker and sudo on PATH (fake.py), sends the same keys a person would, and saves
every screen with its exact colors to src/data/demo.json. The landing page
replays those frames, so the demo always matches the product.

Usage (from the repo root, after building the app):
  pip install pyte
  python3 -I landing-page/tools/demo-capture/capture.py
"""
import fcntl
import json
import os
import pty
import select
import struct
import tempfile
import termios
import time

import pyte

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
BIN = os.path.join(ROOT, "build", "g-systemctl")
OUT = os.path.join(ROOT, "landing-page", "src", "data", "demo.json")
COLS, ROWS = int(os.environ.get("DEMO_COLS", 120)), int(os.environ.get("DEMO_ROWS", 30))

# Make the fake commands callable by name.
FAKE = os.path.join(HERE, "bin")
os.makedirs(FAKE, exist_ok=True)
for name in ("systemctl", "journalctl", "ps", "docker", "sudo"):
    link = os.path.join(FAKE, name)
    if not os.path.islink(link):
        os.symlink(os.path.join(HERE, "fake.py"), link)

screen = pyte.Screen(COLS, ROWS)
stream = pyte.ByteStream(screen)

pid, fd = pty.fork()
if pid == 0:
    # Fresh config dir: the app saves the compose projects it sees.
    env = dict(os.environ, PATH=FAKE + ":" + os.environ["PATH"], TERM="xterm-256color", COLORTERM="truecolor",
               XDG_CONFIG_HOME=tempfile.mkdtemp(prefix="g-systemctl-demo-"))
    # Start on Services so the demo shows both tabs (the app defaults to Containers).
    os.execve(BIN, [BIN, "--services"], env)
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))


def pump(seconds):
    end = time.time() + seconds
    while time.time() < end:
        if select.select([fd], [], [], 0.05)[0]:
            try:
                stream.feed(os.read(fd, 65536))
            except OSError:
                return


COLOR = {"default": None, "brightwhite": "ffffff", "white": "e5e5e5", "black": "000000"}
styles, style_ix, rows, row_ix, frames = [], {}, [], {}, []


def style(c):
    fg = COLOR.get(c.fg, c.fg)
    bg = COLOR.get(c.bg, c.bg)
    if c.reverse:
        fg, bg = bg or "0b0c13", fg or "b4bad0"
    key = (fg, bg, bool(c.bold))
    if key not in style_ix:
        style_ix[key] = len(styles)
        styles.append(list(key))
    return style_ix[key]


def encode_row(y):
    """A row is a list of [text, style] runs; identical rows are stored once."""
    runs = []
    for x in range(COLS):
        c = screen.buffer[y][x]
        s = style(c)
        ch = c.data or " "
        if runs and runs[-1][1] == s:
            runs[-1][0] += ch
        else:
            runs.append([ch, s])
    key = json.dumps(runs, ensure_ascii=False)
    if key not in row_ix:
        row_ix[key] = len(rows)
        rows.append(runs)
    return row_ix[key]


def snap(keys, caption, hold):
    frames.append({"k": keys, "c": caption, "h": hold, "r": [encode_row(y) for y in range(ROWS)]})


def press(seq, keys, caption, hold, settle=0.5):
    os.write(fd, seq.encode())
    pump(settle)
    snap(keys, caption, hold)


def live(n, every, keys=None, caption=None):
    """Let the app run and record what changes on its own (graphs, new log lines)."""
    for _ in range(n):
        pump(every / 1000)
        snap(keys or [], caption, every)


ESC = "\x1b"
pump(4.0)
snap([], "All units, with status, PID, CPU and memory", 2200)
for ch in "nginx":
    press(ch, [ch], "Type to filter", 280, 0.3)
pump(0.6)
press("\x1b[B", ["↓"], "Type to filter", 700)
press("\r", ["Enter"], "Detail page: summary, graphs and logs", 900, 1.5)
live(6, 800)
press(ESC + "r", ["Alt", "r"], "Restart asks for confirmation", 1700)
press("\r", ["Enter"], "Enter is ignored in the prompt", 1700)
press("y", ["y"], "y confirms", 1200, 1.0)
live(2, 800)
press("\r", ["Enter"], "Full-size logs", 800, 1.0)
live(3, 800)
press(ESC, ["Esc"], "Back one level", 1100, 0.8)
press(ESC, ["Esc"], "Back one level", 900, 0.8)
press(ESC, ["Esc"], "On the list, Esc clears the filter", 1400, 0.8)
press("\t", ["Tab"], "Containers tab, grouped by compose project", 1400, 3.0)
live(2, 900)
press("\x1b[B", ["↓"], "Select a container", 700)
press(ESC + "u", ["Alt", "u"], "Edit limits with docker update", 1200, 1.0)
press("\x7f", ["⌫"], "Edit limits with docker update", 300, 0.3)
press("2", ["2"], "Edit limits with docker update", 700, 0.3)
press("\t", ["Tab"], "Edit limits with docker update", 500, 0.4)
press("\x7f\x7f", ["⌫"], "Edit limits with docker update", 300, 0.3)
for ch in "2g":
    press(ch, [ch], "Edit limits with docker update", 300, 0.3)
pump(0.4)
press("\r", ["Enter"], "Applied while the container runs", 1400, 1.5)
live(2, 900)
os.kill(pid, 9)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w") as f:
    json.dump({"cols": COLS, "rows": ROWS, "styles": styles, "lines": rows, "frames": frames}, f, ensure_ascii=False, separators=(",", ":"))
print(f"{len(frames)} frames, {len(rows)} unique rows, {len(styles)} styles, {os.path.getsize(OUT)} bytes -> {OUT}")
