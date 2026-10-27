"""Minimal streamable-HTTP MCP client for the Unreal editor plugin (toolset-search mode: list_toolsets / describe_toolset / call_tool)."""
import json, os, sys, urllib.request, urllib.error
URL = "http://localhost:8010/mcp"
SESSION_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".mcp_session")
_ID = [0]

def _post(payload, session=None):
    req = urllib.request.Request(URL, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
    if session:
        req.add_header("Mcp-Session-Id", session)
    try:
        resp = urllib.request.urlopen(req, timeout=900)
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"HTTP {e.code}: {e.read().decode(errors='replace')[:600]}")
    sid = resp.headers.get("Mcp-Session-Id")
    body = resp.read().decode(errors="replace")
    data = None
    if "text/event-stream" in (resp.headers.get("Content-Type") or ""):
        for line in body.splitlines():
            if line.startswith("data:"):
                try:
                    d = json.loads(line[5:].strip())
                    if isinstance(d, dict) and ("result" in d or "error" in d):
                        data = d
                except Exception:
                    pass
    elif body.strip():
        data = json.loads(body)
    return data, sid

def _init():
    _ID[0] += 1
    data, sid = _post({"jsonrpc": "2.0", "id": _ID[0], "method": "initialize", "params": {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "slonk-scratch", "version": "1"}}})
    if not sid:
        raise RuntimeError("no session id from initialize: %s" % str(data)[:300])
    open(SESSION_FILE, "w").write(sid)
    try:
        _post({"jsonrpc": "2.0", "method": "notifications/initialized"}, sid)
    except RuntimeError:
        pass
    return sid

def session():
    if os.path.exists(SESSION_FILE):
        s = open(SESSION_FILE).read().strip()
        if s:
            return s
    return _init()

def _rpc(method, params=None, sess=None):
    _ID[0] += 1
    data, _ = _post({"jsonrpc": "2.0", "id": _ID[0], "method": method, "params": params or {}}, sess)
    return data

def call_tool(name, args):
    sess = session()
    try:
        data = _rpc("tools/call", {"name": name, "arguments": args}, sess)
    except RuntimeError as e:
        if "session" in str(e).lower():
            sess = _init()
            data = _rpc("tools/call", {"name": name, "arguments": args}, sess)
        else:
            raise
    if isinstance(data, dict) and "error" in data and "session" in json.dumps(data["error"]).lower():
        sess = _init()
        data = _rpc("tools/call", {"name": name, "arguments": args}, sess)
    return data

def unwrap(r):
    """Reduce a tools/call response to the tool's own JSON ({"returnValue": ...}) or {"isError": True, "content": [...]}"""
    if r is None:
        return None
    if "error" in r:
        return {"isError": True, "content": [json.dumps(r["error"])]}
    res = r.get("result", r)
    texts = [c.get("text", "") for c in res.get("content", []) if c.get("type") == "text"]
    if res.get("isError"):
        return {"isError": True, "content": texts}
    if res.get("structuredContent") is not None:
        sc = res["structuredContent"]
        if isinstance(sc, dict) and ("returnValue" in sc or "isError" in sc):
            return sc
    if len(texts) == 1:
        try:
            return json.loads(texts[0])
        except Exception:
            return {"returnValue": texts[0]}
    return res

def tool(ts, name, args):
    return unwrap(call_tool("call_tool", {"toolset_name": ts, "tool_name": name, "arguments": args}))

if __name__ == "__main__":
    a = sys.argv[1:]
    if not a or a[0] == "toolsets":
        print(json.dumps(unwrap(call_tool("list_toolsets", {})), indent=1))
    elif a[0] == "describe":
        print(json.dumps(unwrap(call_tool("describe_toolset", {"toolset_name": a[1]})), indent=1))
    elif a[0] == "tool":
        print(json.dumps(tool(a[1], a[2], json.loads(a[3]) if len(a) > 3 else {}), indent=1, default=str))
