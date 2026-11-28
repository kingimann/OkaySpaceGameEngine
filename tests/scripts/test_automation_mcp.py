#!/usr/bin/env python3
"""End-to-end check of the engine's MCP server: a real `okay mcp` process on stdin / stdout, as an AI agent starts it.

    python3 -I tests/scripts/test_automation_mcp.py <path to the okay program>

Makes an empty project (okay new), then speaks JSON-RPC 2.0 to `okay mcp --headless`: initialize, tools/list, and
tools/call for a scene round trip (create an object, add a component, set a property, inspect, undo, redo, save,
reopen, compare), a screenshot (the PNG exists, has the asked size and is not empty), a play-mode step and error cases.
Then `okay automation link` (a windowless host with the editor's automation link on) and `okay mcp --editor` in front
of it: the bridge a running editor uses, a wrong token refused, the link file readable by this user only.
Python standard library only. Exit code 0 = every check passed.
"""
import base64
import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

FAILS = []


def check(cond, what):
    if not cond:
        FAILS.append(what)
        print("FAIL:", what, flush=True)


class Mcp:
    """An MCP client over a child process's stdin / stdout (one JSON-RPC message per line)."""

    def __init__(self, args, env):
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        self.next = 1

    def send(self, msg):
        self.p.stdin.write((json.dumps(msg) + "\n").encode())
        self.p.stdin.flush()

    def request(self, method, params=None):
        rid = self.next
        self.next += 1
        msg = {"jsonrpc": "2.0", "id": rid, "method": method}
        if params is not None:
            msg["params"] = params
        self.send(msg)
        line = self.p.stdout.readline()
        if not line:
            raise RuntimeError("okay mcp closed: " + self.p.stderr.read().decode(errors="replace"))
        reply = json.loads(line)
        check(reply.get("id") == rid and reply.get("jsonrpc") == "2.0", "reply id and jsonrpc for " + method)
        return reply

    def call(self, tool, args=None):
        r = self.request("tools/call", {"name": tool, "arguments": args or {}})
        res = r.get("result")
        if res is None:
            return r, None
        return res, res.get("structuredContent")

    def ok(self, tool, args=None):
        res, sc = self.call(tool, args)
        good = sc is not None and sc.get("ok") is True and res.get("isError") is False
        check(good, tool + " " + json.dumps(args or {}) + " -> " + json.dumps(sc)[:300])
        return (sc or {}).get("data", {})

    def close(self):
        self.p.stdin.close()
        try:
            return self.p.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.p.kill()
            return -1


def scene_shape(m):
    data = m.ok("scene_hierarchy", {"deep": True})

    def walk(nodes):
        return sorted((n["id"], n["name"], tuple(n["components"]), walk(n.get("children", []))) for n in nodes)
    return walk(data.get("objects", []))


def scene_names(m):
    out = []

    def walk(nodes):
        for n in nodes:
            out.append(n["name"])
            walk(n.get("children", []))
    walk(m.ok("scene_hierarchy").get("objects", []))
    return out


def main():
    okay = os.path.abspath(sys.argv[1])
    tmp = tempfile.mkdtemp(prefix="okay_mcp_e2e_")
    env = dict(os.environ)
    env["OKAY_AUTOMATION_LINK_FILE"] = os.path.join(tmp, "link", "editor_link.json")   # never a real editor's
    r = subprocess.run([okay, "new", "McpGame", "--in", tmp, "--no-git"], env=env, capture_output=True, timeout=120)
    check(r.returncode == 0, "okay new: " + r.stderr.decode(errors="replace"))
    project = os.path.join(tmp, "McpGame")

    # ---- headless MCP ---------------------------------------------------------------------------------------------
    m = Mcp([okay, "--project", project, "mcp", "--headless"], env)
    init = m.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "e2e", "version": "1"}})
    check(init.get("result", {}).get("protocolVersion") == "2025-06-18", "initialize agrees the version")
    check("tools" in init.get("result", {}).get("capabilities", {}), "initialize offers tools")
    m.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
    tools = m.request("tools/list").get("result", {}).get("tools", [])
    names = {t["name"] for t in tools}
    check(len(tools) >= 70, "tools/list has the engine, editor and okay commands (%d)" % len(tools))
    for need in ["scene_hierarchy", "object_create", "component_set", "undo_undo", "viewport_screenshot", "play_step",
                 "asset_list", "project_info", "build_run", "cli_run", "vs_connect", "model_apply"]:
        check(need in names, "tool " + need)
    check(all("inputSchema" in t and t["inputSchema"].get("type") == "object" for t in tools), "every tool has an input schema")

    host = m.ok("automation_host")
    check(host.get("kind") == "headless" and host.get("project", "").endswith("McpGame"), "headless host on the project")
    info = m.ok("project_info")
    check(info.get("exitCode") == 0, "project_info through okay project info")

    m.ok("scene_new", {"template": "3d", "discard": True})
    box = m.ok("object_create", {"kind": "cube", "name": "Box", "position": {"x": 2, "y": 1, "z": 0}})
    check(box.get("name") == "Box" and "MeshRenderer" in box.get("components", []), "object_create made a cube")
    m.ok("component_add", {"object": box["id"], "type": "Light"})
    m.ok("component_set", {"object": "Box", "path": "Light.intensity", "value": 4})
    got = m.ok("component_get", {"object": "Box", "path": "Light.intensity"})
    check(got.get("value") == 4, "component_get reads 4")
    inspected = m.ok("object_inspect", {"object": "Box"})
    lights = [c for c in inspected.get("components", []) if c.get("type") == "Light"]
    check(lights and lights[0]["properties"].get("intensity") == 4, "object_inspect shows the light")
    hist = m.ok("undo_history")
    check(len(hist.get("undo", [])) >= 3, "three undo steps")
    m.ok("undo_undo")
    check(m.ok("component_get", {"object": "Box", "path": "Light.intensity"}).get("value") != 4, "undo took the intensity back")
    m.ok("undo_redo")
    check(m.ok("component_get", {"object": "Box", "path": "Light.intensity"}).get("value") == 4, "redo brought it again")
    before = scene_shape(m)
    path = os.path.join(project, "Assets", "E2E.okayscene")
    m.ok("scene_save", {"path": path})
    check(os.path.isfile(path), "scene_save wrote the file")
    m.ok("scene_new", {"template": "empty"})
    check(scene_shape(m) != before, "a new scene is different")
    m.ok("scene_open", {"path": "E2E.okayscene"})
    check(scene_shape(m) == before, "the reopened scene has the same objects, ids and components")
    listed = m.ok("scene_list")
    check("E2E" in json.dumps(listed), "scene_list sees the saved scene")

    shot = os.path.join(tmp, "shot.png")
    res, sc = m.call("viewport_screenshot", {"width": 200, "height": 120, "path": shot, "inline": True})
    data = (sc or {}).get("data", {})
    check(sc and sc.get("ok") and data.get("width") == 200 and data.get("height") == 120, "screenshot size")
    check(data.get("drawnPixels", 0) > 100, "screenshot is not empty")
    with open(shot, "rb") as f:
        png = f.read()
    check(png[:8] == b"\x89PNG\r\n\x1a\n" and int.from_bytes(png[16:20], "big") == 200 and int.from_bytes(png[20:24], "big") == 120, "PNG header and size")
    images = [c for c in (res or {}).get("content", []) if c.get("type") == "image"]
    check(images and base64.b64decode(images[0]["data"]) == png and images[0]["mimeType"] == "image/png", "inline image is the same PNG")

    m.ok("component_add", {"object": "Box", "type": "Rigidbody3D"})
    m.ok("transform_set", {"object": "Box", "position": {"x": 2, "y": 8, "z": 0}})
    m.ok("play_start")
    res, sc = m.call("component_set", {"object": "Box", "path": "Light.intensity", "value": 1})
    check(sc and not sc["ok"] and sc["error"]["code"] == "refused" and res.get("isError") is True, "edits refused while playing")
    stepped = m.ok("play_step", {"frames": 30, "keys": ["d"]})
    check(stepped.get("frames") == 30, "play_step ran 30 frames")
    bodies = m.ok("physics_bodies").get("bodies", [])
    ys = [b["position"]["y"] for b in bodies if b["name"] == "Box"]
    check(ys and ys[0] < 7.5, "the box fell while playing (%s)" % ys)
    m.ok("play_stop")
    check(abs(m.ok("transform_get", {"object": "Box"})["position"]["y"] - 8) < 1e-4, "Stop put the scene back")

    # Errors: a structured tool error, an unknown tool, bad parameters, a bad message.
    res, sc = m.call("object_inspect", {"object": "Nobody"})
    check(res.get("isError") is True and sc["error"]["code"] == "not_found", "unknown object is not_found")
    res, sc = m.call("component_set", {"object": "Box"})
    check(sc and sc["error"]["code"] == "invalid_params", "missing parameters are invalid_params")
    r = m.request("tools/call", {"name": "no_such_tool", "arguments": {}})
    check(r.get("error", {}).get("code") == -32602, "unknown tool is JSON-RPC -32602")
    m.p.stdin.write(b"{broken\n")
    m.p.stdin.flush()
    bad = json.loads(m.p.stdout.readline())
    check(bad.get("error", {}).get("code") == -32700 and bad.get("id") is None, "parse error -32700")

    # Ops (every menu item) and script.run through MCP.
    names = [t["name"] for t in m.request("tools/list")["result"]["tools"]]
    check("op_edit_duplicate" in names and "op_h_tf_resetpos" in names and "op_list" in names and "script_run" in names, "ops and script.run are tools")
    check(len([n for n in names if n.startswith("op_")]) > 250, "every menu item is an op tool")
    m.ok("editor_select", {"objects": ["Box"]})
    m.ok("op_edit_duplicate")
    check(len([n for n in scene_names(m) if n.startswith("Box")]) == 2, "op_edit_duplicate made a copy")
    m.ok("undo_undo")
    res, sc = m.call("op_file_open")
    check(sc and sc["error"]["code"] == "needs_ui" and sc["error"]["details"]["param"] == "path", "a dialog op answers needs_ui")
    ran = m.ok("script_run", {"language": "lua", "source": "print('from lua')\nlocal r = automation_call('object.create', {kind = 'cube', name = 'ScriptBox'})\nreturn r.data.name"})
    check(ran.get("value") == "ScriptBox" and any("from lua" in x for x in ran.get("output", [])), "script_run: output, value, automation_call")
    res, sc = m.call("script_run", {"language": "javascript", "source": "let a = 1;\nnull.x;"})
    check(sc and not sc["ok"] and sc["error"]["details"]["line"] == 2, "script_run: an error with its line")
    res, sc = m.call("script_run", {"source": "while (true) { }", "budget": 10000})
    check(sc and not sc["ok"] and sc["error"]["details"]["stopped"] is True, "script_run: a runaway script stops")
    m.ok("undo_undo")
    check("ScriptBox" not in scene_names(m), "script_run is one undo step")
    check(m.close() == 0, "okay mcp ends cleanly when stdin closes")

    # ---- a scripted list of commands -------------------------------------------------------------------------------
    lines = os.path.join(tmp, "cmds.jsonl")
    with open(lines, "w") as f:
        f.write(json.dumps({"command": "object.create", "params": {"kind": "sphere", "name": "Ball"}}) + "\n")
        f.write(json.dumps({"command": "object.inspect", "params": {"object": "Ball"}}) + "\n")
    r = subprocess.run([okay, "--project", project, "automation", "run", lines], env=env, capture_output=True, timeout=120)
    outs = [json.loads(x) for x in r.stdout.decode().splitlines() if x.strip()]
    check(r.returncode == 0 and len(outs) == 2 and outs[1]["data"]["name"] == "Ball", "okay automation run")
    r = subprocess.run([okay, "automation", "list", "--json"], env=env, capture_output=True, timeout=60)
    listed = json.loads(r.stdout.decode())
    check(r.returncode == 0 and listed["count"] >= 330 and listed["commands"][0]["inputSchema"], "okay automation list --json")

    # ---- the editor link: okay mcp --editor in front of a host with the link on -------------------------------------
    link = subprocess.Popen([okay, "--project", project, "automation", "link", "--seconds", "60"], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    info_path = env["OKAY_AUTOMATION_LINK_FILE"]
    for _ in range(600):
        if os.path.isfile(info_path):
            break
        time.sleep(0.05)
    check(os.path.isfile(info_path), "the link wrote its file")
    if os.path.isfile(info_path):
        if os.name == "posix":
            check(stat.S_IMODE(os.stat(info_path).st_mode) == 0o600, "the link file is for this user only")
        with open(info_path) as f:
            li = json.load(f)
        check(len(li.get("token", "")) == 64, "a 32-byte token")
        s = socket.create_connection(("127.0.0.1", li["port"]), timeout=10)
        s.sendall(b'{"okay_link":1,"token":"' + b"0" * 64 + b'"}\n')
        answer = s.makefile().readline()
        check('"ok":false' in answer, "a wrong token is refused")
        s.close()
        e = Mcp([okay, "--project", project, "mcp", "--editor"], env)
        init = e.request("initialize", {"protocolVersion": "2025-03-26", "capabilities": {}})
        check(init.get("result", {}).get("protocolVersion") == "2025-03-26", "initialize through the link")
        e.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        made = e.ok("object_create", {"kind": "empty", "name": "FromLink"})
        e2 = Mcp([okay, "--project", project, "mcp", "--editor"], env)   # a second agent sees the same scene
        e2.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}})
        check(e2.ok("object_inspect", {"object": made.get("id", "?")}).get("name") == "FromLink", "two clients share the linked scene")
        check(e.ok("automation_host").get("kind") == "headless", "the link answers from its host")
        check(e.close() == 0 and e2.close() == 0, "okay mcp --editor ends cleanly")
        # One config entry for every project: okay mcp with no --project finds the open editor from its link file.
        a = Mcp([okay, "mcp"], env)
        a.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}})
        check(a.ok("object_inspect", {"object": made.get("id", "?")}).get("name") == "FromLink", "okay mcp with no project finds the open editor")
        check(a.close() == 0, "okay mcp (found editor) ends cleanly")
    link.terminate()
    try:
        link.wait(timeout=20)
    except subprocess.TimeoutExpired:
        link.kill()
    r = subprocess.run([okay, "--project", project, "mcp", "--editor"], env=dict(env, OKAY_AUTOMATION_LINK_FILE=os.path.join(tmp, "none.json")),
                       input=b"", capture_output=True, timeout=60)
    check(r.returncode == 1, "okay mcp --editor with no editor fails plainly")
    r = subprocess.run([okay, "mcp"], env=dict(env, OKAY_AUTOMATION_LINK_FILE=os.path.join(tmp, "none.json")), cwd=tmp,
                       input=b"", capture_output=True, timeout=60)
    said = r.stderr.decode(errors="replace") + r.stdout.decode(errors="replace")
    check(r.returncode == 1 and "No OkaySpace editor is open" in said and "Automation Link" in said, "okay mcp with no editor and no project says so plainly")

    shutil.rmtree(tmp, ignore_errors=True)
    if FAILS:
        print("%d check(s) failed" % len(FAILS))
        return 1
    print("all automation MCP checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
