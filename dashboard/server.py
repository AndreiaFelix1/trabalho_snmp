#!/usr/bin/env python3
import csv
import json
import os
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent
PROJECT = ROOT.parent
LOGS = PROJECT / "logs"
PORT = 8080

def latest_status():
    path = LOGS / "metrics.csv"
    latest = {}

    if not path.exists():
        return []

    with path.open("r", encoding="utf-8", newline="") as f:
        rows = csv.DictReader(f)
        for row in rows:
            agent = row.get("agent", "")
            if agent:
                latest[agent] = row

    result = []
    for row in latest.values():
        result.append({
            "timestamp": row.get("timestamp", ""),
            "agent": row.get("agent", ""),
            "host": row.get("host", ""),
            "status": row.get("status", ""),
            "cpu": float(row.get("cpu") or 0),
            "memory": float(row.get("memory") or 0),
            "uptime": int(float(row.get("uptime") or 0)),
            "rx": int(float(row.get("rx") or 0)),
            "tx": int(float(row.get("tx") or 0)),
            "latency_us": int(float(row.get("latency_us") or 0)),
        })

    result.sort(key=lambda x: x["agent"])
    return result

def alerts():
    path = LOGS / "alerts.log"
    if not path.exists():
        return []

    result = []
    with path.open("r", encoding="utf-8") as f:
        for line in f.readlines()[-20:]:
            parts = line.strip().split(",")
            if len(parts) == 5:
                result.append({
                    "timestamp": parts[0],
                    "agent": parts[1],
                    "oid": parts[2],
                    "value": parts[3],
                    "threshold": parts[4],
                })
    return result[::-1]

class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        path = urlparse(self.path).path

        if path == "/api/status":
            data = json.dumps(latest_status()).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        if path == "/api/alerts":
            data = json.dumps(alerts()).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        if path == "/":
            self.path = "/index.html"

        return super().do_GET()

    def log_message(self, fmt, *args):
        # Deixa o terminal do dashboard mais limpo.
        if "/api/" not in self.path:
            super().log_message(fmt, *args)

if __name__ == "__main__":
    os.chdir(ROOT)
    print(f"Dashboard: http://127.0.0.1:{PORT}")
    print("Pressione Ctrl+C para encerrar.")
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
