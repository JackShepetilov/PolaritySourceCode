# Local layout-editor server for the Biome1 island (author tool).
#
#   python layout_editor_server.py        -> http://127.0.0.1:8765 (opens browser)
#
# Serves layout_editor.html and a small JSON API around the existing pipeline:
# layouts CRUD (Island/layouts/*.json + the ACTIVE biome1_island_layout.json),
# fast/full generation (make_biome1_heightmap.py), gates (analyze_island_
# terrain.py --json) and one-click Apply into the open UE editor (REST port
# 3000: save_testlevel fix + build_biome_island.py), mirroring the manual
# workflow from Handoff_BiomeIsland.md. Generation is serialized by a lock.
#
# The tool only ever WRITES: Island/layouts/*, Island/editor_work/* and - on
# Apply - the ACTIVE layout + biome1_heightmap_2017.png (with timestamped
# backups under Island/editor_work/backups/).

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

TOOLS = os.path.dirname(os.path.abspath(__file__))
ISLAND = os.path.join(TOOLS, "Island")
LAYOUTS_DIR = os.path.join(ISLAND, "layouts")
WORK = os.path.join(ISLAND, "editor_work")
BACKUPS = os.path.join(WORK, "backups")
ACTIVE_PATH = os.path.join(ISLAND, "biome1_island_layout.json")
ACTIVE_NAME = "ACTIVE"
ARENAS_DIR = os.path.join(TOOLS, "Arenas")
PAGE = os.path.join(TOOLS, "layout_editor.html")
PY = sys.executable
UE_URL = "http://127.0.0.1:3000"
FULL_RES = 2017
FAST_RES = 505

gen_lock = threading.Lock()

sys.path.append(TOOLS)
import make_biome1_heightmap as mh  # noqa: E402


# ------------------------------------------------------------ helpers ----
def safe_name(name):
    if not re.fullmatch(r"[A-Za-z0-9_\-]{1,48}", name or ""):
        raise ValueError("bad layout name (A-Za-z0-9_- only)")
    return name


def layout_path(name):
    if name == ACTIVE_NAME:
        return ACTIVE_PATH
    return os.path.join(LAYOUTS_DIR, safe_name(name) + ".json")


def work_paths(name):
    base = os.path.join(WORK, name)
    return {"hm_fast": base + "_hm_fast.png", "prev_fast": base + "_prev_fast.png",
            "hm_full": base + "_hm_full.png", "prev_full": base + "_prev_full.png"}


def backup(path, tag):
    if os.path.isfile(path):
        os.makedirs(BACKUPS, exist_ok=True)
        dst = os.path.join(BACKUPS, "{}_{}{}".format(
            tag, time.strftime("%Y%m%d_%H%M%S"), os.path.splitext(path)[1]))
        shutil.copy2(path, dst)


def run_tool(args, timeout=900):
    p = subprocess.run([PY] + args, cwd=TOOLS, capture_output=True,
                       text=True, timeout=timeout)
    return p.returncode, (p.stdout or "") + (p.stderr or "")


def parse_gates(text):
    m = re.search(r"GATES_JSON: (.+)", text)
    return json.loads(m.group(1)) if m else None


def ue_call(tool, payload, timeout=600):
    req = urllib.request.Request(
        UE_URL + "/mcp/tool/" + tool, method="POST",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def ue_bridge_up():
    try:
        req = urllib.request.Request(UE_URL + "/mcp/tools")
        with urllib.request.urlopen(req, timeout=3):
            return True
    except Exception:
        return False


def ue_run_py(script, timeout=600):
    return ue_call("run_console_command",
                   {"command": 'py "{}"'.format(script.replace("\\", "/"))},
                   timeout=timeout)


def ue_log(filter_, lines=60):
    r = ue_call("get_output_log", {"lines": lines, "filter": filter_}, timeout=30)
    return (r.get("data") or {}).get("content", "")


def arena_meta():
    out = {}
    for f in os.listdir(ARENAS_DIR):
        if f.endswith(".json"):
            name = f[:-5]
            try:
                out[name] = mh.load_footprint(name)
            except Exception:
                pass
    return out


# ------------------------------------------------------------ handler ----
class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *a):
        pass    # keep the console readable

    def _send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else \
            json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _err(self, msg, code=400):
        self._send(code, {"ok": False, "error": str(msg)})

    def _qs(self):
        return {k: v[0] for k, v in parse_qs(urlparse(self.path).query).items()}

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n).decode("utf-8")) if n else None

    # ---------------- GET ----------------
    def do_GET(self):
        path = urlparse(self.path).path
        q = self._qs()
        try:
            if path == "/" or path == "/index.html":
                with open(PAGE, "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            elif path == "/api/meta":
                self._send(200, {"ok": True, "extent": mh.EXTENT,
                                 "active_name": ACTIVE_NAME,
                                 "fast_res": FAST_RES, "full_res": FULL_RES,
                                 "arenas": arena_meta()})
            elif path == "/api/layouts":
                names = [ACTIVE_NAME] + sorted(
                    f[:-5] for f in os.listdir(LAYOUTS_DIR) if f.endswith(".json"))
                self._send(200, {"ok": True, "layouts": names})
            elif path == "/api/layout":
                with open(layout_path(q["name"]), encoding="utf-8") as f:
                    self._send(200, {"ok": True, "layout": json.load(f)})
            elif path == "/api/preview":
                wp = work_paths(safe_name(q["name"]))
                p = wp["prev_full"] if q.get("kind") == "full" else wp["prev_fast"]
                if not os.path.isfile(p):
                    return self._err("no preview yet", 404)
                with open(p, "rb") as f:
                    self._send(200, f.read(), "image/png")
            elif path == "/api/editor_status":
                up = ue_bridge_up()
                last = ""
                if up:
                    try:
                        last = ue_log("Cmd:", 6).strip().splitlines()[-1:]
                        last = last[0][-160:] if last else ""
                    except Exception:
                        last = ""
                self._send(200, {"ok": True, "bridge": up, "last_cmd": last})
            else:
                self._err("not found", 404)
        except Exception as e:
            self._err(e, 500)

    # ---------------- POST ----------------
    def do_POST(self):
        path = urlparse(self.path).path
        q = self._qs()
        try:
            if path == "/api/layout":
                name = q["name"]
                p = layout_path(name)
                backup(p, "layout_" + name)
                data = self._body()
                with open(p, "w", encoding="utf-8") as f:
                    json.dump(data, f, ensure_ascii=False, indent=2)
                self._send(200, {"ok": True})
            elif path == "/api/new":
                src = layout_path(q.get("src", ACTIVE_NAME))
                dst = layout_path(safe_name(q["name"]))
                if os.path.exists(dst):
                    return self._err("layout exists")
                os.makedirs(LAYOUTS_DIR, exist_ok=True)
                shutil.copy2(src, dst)
                self._send(200, {"ok": True})
            elif path == "/api/delete":
                name = q["name"]
                if name == ACTIVE_NAME:
                    return self._err("cannot delete the ACTIVE layout")
                p = layout_path(name)
                backup(p, "deleted_" + name)
                os.remove(p)
                self._send(200, {"ok": True})
            elif path == "/api/snap":
                # debug aid: the page posts {"dataUrl": "data:image/png;base64,..."}
                # and we drop it into editor_work for inspection
                import base64
                data = self._body()["dataUrl"].split(",", 1)[1]
                os.makedirs(WORK, exist_ok=True)
                p = os.path.join(WORK, "ui_snap.png")
                with open(p, "wb") as f:
                    f.write(base64.b64decode(data))
                self._send(200, {"ok": True, "path": p})
            elif path == "/api/generate":
                self.handle_generate(q)
            elif path == "/api/apply":
                self.handle_apply(q)
            elif path == "/api/sync":
                self.handle_sync()
            else:
                self._err("not found", 404)
        except Exception as e:
            self._err(e, 500)

    def handle_generate(self, q):
        name = q["name"]
        full = q.get("kind") == "full"
        lp = layout_path(name)
        wp = work_paths(name if name != ACTIVE_NAME else ACTIVE_NAME)
        os.makedirs(WORK, exist_ok=True)
        hm = wp["hm_full" if full else "hm_fast"]
        prev = wp["prev_full" if full else "prev_fast"]
        res = FULL_RES if full else FAST_RES
        with gen_lock:
            code, out = run_tool(["make_biome1_heightmap.py", "--layout", lp,
                                  "--res", str(res), "--out", hm, "--preview", prev])
            if code != 0:
                return self._send(200, {"ok": False, "log": out[-4000:]})
            code2, out2 = run_tool(["analyze_island_terrain.py", hm,
                                    "--layout", lp, "--json"])
        gates = parse_gates(out2)
        self._send(200, {"ok": True, "log": (out + "\n" + out2)[-8000:],
                         "gates": gates, "approx": not full,
                         "preview": "/api/preview?name={}&kind={}&ts={}".format(
                             name, "full" if full else "fast", int(time.time()))})

    def handle_apply(self, q):
        name = q["name"]
        if not ue_bridge_up():
            return self._err("UE editor bridge (port 3000) is down - open the "
                             "editor first", 503)
        steps = []
        with gen_lock:
            # 1. activate: the chosen layout becomes the pipeline source of truth
            if name != ACTIVE_NAME:
                backup(ACTIVE_PATH, "active_pre_apply")
                shutil.copy2(layout_path(name), ACTIVE_PATH)
                steps.append("activated layout '{}'".format(name))
            # 2. full-res heightmap into the standard pipeline filename
            backup(os.path.join(ISLAND, "biome1_heightmap_2017.png"), "hm_pre_apply")
            code, out = run_tool(["make_biome1_heightmap.py"])
            steps.append("full-res generate: " + ("OK" if code == 0 else "FAILED"))
            if code != 0:
                return self._send(200, {"ok": False, "steps": steps,
                                        "log": out[-4000:]})
            code2, out2 = run_tool(["analyze_island_terrain.py", "--json"])
            gates = parse_gates(out2)
        # 3. into the editor (mirrors the manual workflow)
        try:
            ue_run_py(os.path.join(TOOLS, "save_testlevel_if_only_dirty.py"),
                      timeout=120)
            steps.append("dirty-guard fix ran")
            ue_run_py(os.path.join(TOOLS, "build_biome_island.py"), timeout=590)
            steps.append("build_biome_island ran")
            time.sleep(1.5)
            log = ue_log("BIOME_ISLAND", 40)
            ok = "RESULT: SUCCESS" in log
            steps.append("build result: " + ("SUCCESS" if ok else "CHECK LOG"))
        except Exception as e:
            return self._send(200, {"ok": False, "steps": steps, "gates": gates,
                                    "error": str(e)})
        tail = "\n".join(log.strip().splitlines()[-18:])
        self._send(200, {"ok": ok, "steps": steps, "gates": gates, "build_log": tail})

    def handle_sync(self):
        if not ue_bridge_up():
            return self._err("UE editor bridge (port 3000) is down", 503)
        ue_run_py(os.path.join(TOOLS, "sync_island_slots.py"), timeout=120)
        time.sleep(1.0)
        log = ue_log("SLOT_SYNC", 25)
        with open(ACTIVE_PATH, encoding="utf-8") as f:
            layout = json.load(f)
        self._send(200, {"ok": True, "layout": layout,
                         "log": "\n".join(log.strip().splitlines()[-10:])})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()
    os.makedirs(LAYOUTS_DIR, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = "http://127.0.0.1:{}/".format(args.port)
    print("Island layout editor: " + url)
    if not args.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
