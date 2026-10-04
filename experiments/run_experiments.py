#!/usr/bin/env python3
"""Mini-SNMP experimental test suite.

The C Agent/Manager remain the system under test. Python is used only as a
small automation helper to start processes, create temporary configurations,
and collect results.
"""

import argparse
import csv
import json
import os
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "bin"
LOGS = ROOT / "logs"
RESULTS = ROOT / "experiments" / "results"

HEADER = "timestamp,agent,host,status,cpu,memory,uptime,rx,tx,latency_us"


def run(cmd, **kwargs):
    return subprocess.run(cmd, cwd=ROOT, text=True, **kwargs)


def ensure_dirs():
    LOGS.mkdir(exist_ok=True)
    RESULTS.mkdir(parents=True, exist_ok=True)


def ensure_binaries():
    if not (BIN / "agent").exists() or not (BIN / "manager").exists():
        print("[INFO] Binários não encontrados. Executando make...")
        result = run(["make"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(result.stdout)
        if result.returncode != 0:
            raise RuntimeError("Falha na compilação com make.")


def generate_config(path, n, base_port=5001):
    with path.open("w", encoding="utf-8") as f:
        f.write("# host porta nome\n")
        for i in range(n):
            f.write(f"127.0.0.1 {base_port + i} Agent{i + 1}\n")


def clean_logs():
    for name in ("metrics.csv", "alerts.log"):
        p = LOGS / name
        if p.exists():
            p.unlink()


def ensure_log_header():
    p = LOGS / "metrics.csv"
    if not p.exists() or p.stat().st_size == 0:
        p.write_text(HEADER + "\n", encoding="utf-8")


def start_agents(n, tls=False, base_port=5001, trap_port=6000):
    procs = []
    for i in range(n):
        cmd = [str(BIN / "agent"), str(base_port + i), f"Agent{i + 1}",
               "127.0.0.1", str(trap_port)]
        if tls:
            cmd.append("--tls")
        p = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        procs.append(p)
    time.sleep(1.0)
    return procs


def start_manager(config, interval=2, tls=False, trap_port=6000):
    cmd = [str(BIN / "manager"), str(config), str(interval)]
    if tls:
        cmd.append("--tls")
    return subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)


def stop_process(p):
    if p is None:
        return
    if p.poll() is None:
        try:
            p.send_signal(signal.SIGINT)
            p.wait(timeout=3)
        except Exception:
            p.kill()
            try:
                p.wait(timeout=2)
            except Exception:
                pass


def stop_all(processes):
    for p in reversed(processes):
        stop_process(p)


def read_metrics():
    p = LOGS / "metrics.csv"
    if not p.exists():
        return []
    with p.open("r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def numeric(rows, key):
    values = []
    for r in rows:
        try:
            values.append(float(r[key]))
        except (KeyError, TypeError, ValueError):
            pass
    return values


def summarize(rows):
    online = [r for r in rows if r.get("status") == "ONLINE"]
    lat = numeric(online, "latency_us")
    agents = sorted({r.get("agent") for r in rows if r.get("agent")})
    return {
        "agents": len(agents),
        "samples": len(rows),
        "online_samples": len(online),
        "success_rate_pct": round(100 * len(online) / len(rows), 2) if rows else 0,
        "latency_avg_us": round(statistics.mean(lat), 2) if lat else 0,
        "latency_min_us": round(min(lat), 2) if lat else 0,
        "latency_max_us": round(max(lat), 2) if lat else 0,
    }


def wait_for_samples(n, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        rows = read_metrics()
        online_agents = {r.get("agent") for r in rows if r.get("status") == "ONLINE"}
        if len(online_agents) >= n:
            return rows
        time.sleep(0.25)
    return read_metrics()


def run_scalability(tls, counts, duration):
    print("\n=== E1/E2/E3/E5: operação, latência e escalabilidade ===")
    out = []
    with tempfile.TemporaryDirectory(prefix="mini_snmp_exp_") as td:
        for n in counts:
            print(f"[TESTE] {n} Agents | TLS={'SIM' if tls else 'NÃO'}")
            config = Path(td) / f"agents_{n}.conf"
            generate_config(config, n)
            clean_logs()
            ensure_log_header()
            agents = start_agents(n, tls=tls)
            manager = None
            started = time.time()
            baseline = 0
            try:
                manager = start_manager(config, interval=2, tls=tls)
                warmup_rows = wait_for_samples(n, timeout=max(15, min(60, n // 2 + 10)))
                # Ignora a janela de inicialização para não penalizar o teste
                # por uma consulta que ocorreu enquanto os Agents ainda subiam.
                baseline = len(warmup_rows)
                time.sleep(max(1, duration))
            finally:
                stop_process(manager)
                stop_all(agents)
            elapsed = time.time() - started
            all_rows = read_metrics()
            measured_rows = all_rows[baseline:] if baseline < len(all_rows) else all_rows
            summary = summarize(measured_rows)
            summary.update({
                "experiment": "scalability",
                "tls": int(tls),
                "configured_agents": n,
                "duration_sec": round(elapsed, 2),
            })
            out.append(summary)
            print(f"        sucesso={summary['success_rate_pct']:.2f}% "
                  f"latência média={summary['latency_avg_us']:.0f} us")
    return out


def run_failure_test(tls=False):
    print("\n=== E4: falha e detecção de indisponibilidade ===")
    with tempfile.TemporaryDirectory(prefix="mini_snmp_fail_") as td:
        config = Path(td) / "agents.conf"
        generate_config(config, 3)
        clean_logs()
        ensure_log_header()
        agents = start_agents(3, tls=tls)
        manager = None
        try:
            manager = start_manager(config, interval=1, tls=tls)
            wait_for_samples(3, timeout=8)
            time.sleep(2)
            victim = agents[1]
            kill_time = time.time()
            stop_process(victim)
            detected = None
            deadline = time.time() + 8
            while time.time() < deadline:
                rows = read_metrics()
                for row in reversed(rows):
                    if row.get("agent") == "Agent2" and row.get("status") in ("OFFLINE", "TIMEOUT", "AUTH_ERR"):
                        detected = time.time() - kill_time
                        break
                if detected is not None:
                    break
                time.sleep(0.2)
            result = {
                "experiment": "failure_detection",
                "tls": int(tls),
                "failed_agent": "Agent2",
                "detected": int(detected is not None),
                "detection_time_sec": round(detected, 3) if detected is not None else None,
            }
            print("        detectado=SIM" if detected is not None else "        detectado=NÃO")
            if detected is not None:
                print(f"        tempo de detecção ≈ {detected:.2f} s")
            return result
        finally:
            stop_process(manager)
            stop_all(agents)


def query_plain_agent(port, request, timeout=2):
    s = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s.settimeout(timeout)
    try:
        def exchange(msg):
            s.sendall(msg.encode())
            data = s.recv(1024).decode(errors="replace")
            return data
        auth_req = "AUTH miniSNMP2026\n"
        auth_resp = exchange(auth_req)
        req_bytes = len(auth_req.encode()) + len(auth_resp.encode())
        start = time.perf_counter_ns()
        response = exchange(request)
        latency_us = (time.perf_counter_ns() - start) / 1000.0
        return auth_resp, response, req_bytes + len(request.encode()) + len(response.encode()), latency_us
    finally:
        s.close()


def run_protocol_tests():
    print("\n=== E6: protocolo, autenticação, PING e OID inválido ===")
    proc = start_agents(1, tls=False)
    result = {}
    try:
        s = socket.create_connection(("127.0.0.1", 5001), timeout=2)
        s.settimeout(2)
        try:
            s.sendall(b"GET 1.1\n")
            unauth = s.recv(1024).decode(errors="replace").strip()
            s.sendall(b"AUTH miniSNMP2026\n")
            auth_ok = s.recv(1024).decode(errors="replace").strip()
            s.sendall(b"PING\n")
            pong = s.recv(1024).decode(errors="replace").strip()
            s.sendall(b"GET 9.9\n")
            unknown = s.recv(1024).decode(errors="replace").strip()
            result = {
                "unauthorized": unauth == "ERROR UNAUTHORIZED",
                "auth_ok": auth_ok == "AUTH_OK",
                "ping_pong": pong == "PONG",
                "unknown_oid": "UNKNOWN_OID" in unknown,
            }
        finally:
            s.close()
    finally:
        stop_all(proc)
    print("        ", result)
    return {"experiment": "protocol", **{k: int(v) for k, v in result.items()}}


def run_overhead_test():
    print("\n=== E7: overhead de aplicação do protocolo ===")
    agents = start_agents(1, tls=False)
    try:
        auth_resp, response, total_bytes, latency_us = query_plain_agent(5001, "GET 1.1\n")
        useful = response.split()[-1].encode()
        useful_bytes = max(1, len(useful))
        overhead_bytes = total_bytes - useful_bytes
        ratio = overhead_bytes / useful_bytes
        result = {
            "experiment": "protocol_overhead",
            "request_response_bytes": total_bytes,
            "useful_payload_bytes": useful_bytes,
            "overhead_bytes": overhead_bytes,
            "overhead_ratio": round(ratio, 4),
            "latency_us": round(latency_us, 2),
            "note": "Overhead no nível da aplicação; não inclui cabeçalhos TCP/IP nem registros TLS.",
        }
        print(f"        bytes totais={total_bytes}, payload útil={useful_bytes}, "
              f"overhead={overhead_bytes} ({ratio*100:.1f}%)")
        return result
    finally:
        stop_all(agents)


def run_tls_comparison(duration):
    print("\n=== E8: comparação sem TLS x TLS ===")
    plain = run_scalability(False, [3], duration)[0]
    secure = run_scalability(True, [3], duration)[0]
    p = plain["latency_avg_us"]
    t = secure["latency_avg_us"]
    result = {
        "experiment": "tls_comparison",
        "plain_latency_avg_us": p,
        "tls_latency_avg_us": t,
        "tls_overhead_pct": round(((t - p) / p) * 100, 2) if p else None,
    }
    print(f"        sem TLS={p:.0f} us | TLS={t:.0f} us | "
          f"diferença={result['tls_overhead_pct']}%")
    return result


def direct_set_plain(port, oid, value):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as s:
        s.settimeout(2)
        s.sendall(b"AUTH miniSNMP2026\n")
        auth = s.recv(1024).decode(errors="replace").strip()
        if auth != "AUTH_OK":
            return False, auth
        s.sendall(f"SET {oid} {value}\n".encode())
        resp = s.recv(1024).decode(errors="replace").strip()
        return resp.startswith("SET_OK"), resp


def run_remote_set_test():
    print("\n=== E8: configuração remota (SET) ===")
    agents = start_agents(1, tls=False)
    try:
        ok, response = direct_set_plain(5001, "3.1", 50)
        result = {"experiment": "remote_set", "set_ok": int(ok), "response": response}
        print(f"        resposta={response}")
        return result
    finally:
        stop_all(agents)


def run_trap_test():
    print("\n=== E9: traps simplificados ===")
    agents = start_agents(1, tls=False)
    manager = None
    with tempfile.TemporaryDirectory(prefix="mini_snmp_trap_") as td:
        config = Path(td) / "agents.conf"
        generate_config(config, 1)
        clean_logs()
        ensure_log_header()
        try:
            ok, response = direct_set_plain(5001, "3.2", 0.1)
            manager = start_manager(config, interval=1, tls=False)
            deadline = time.time() + 8
            found = False
            while time.time() < deadline:
                p = LOGS / "alerts.log"
                if p.exists() and p.read_text(encoding="utf-8").strip():
                    found = True
                    break
                time.sleep(0.2)
            result = {"experiment": "trap", "threshold_set": int(ok), "trap_received": int(found), "set_response": response}
            print(f"        SET={response} | trap_recebido={'SIM' if found else 'NÃO'}")
            return result
        finally:
            stop_process(manager)
            stop_all(agents)


def run_dashboard_test():
    print("\n=== E10: dashboard/API ===")
    url = "http://127.0.0.1:8080/api/status"
    server = None
    try:
        try:
            with urllib.request.urlopen(url, timeout=1) as r:
                body = r.read().decode(errors="replace")
                if r.status == 200 and body.startswith("["):
                    print("        API /api/status: OK")
                    return {"experiment": "dashboard", "api_status": 1, "started_by_test": 0}
        except Exception:
            pass

        server = subprocess.Popen([sys.executable, "dashboard/server.py"], cwd=ROOT,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                with urllib.request.urlopen(url, timeout=0.5) as r:
                    body = r.read().decode(errors="replace")
                    if r.status == 200 and body.startswith("["):
                        print("        API /api/status: OK")
                        return {"experiment": "dashboard", "api_status": 1, "started_by_test": 1}
            except Exception:
                pass
            time.sleep(0.2)
        print("        API /api/status: FALHA")
        return {"experiment": "dashboard", "api_status": 0, "started_by_test": 1}
    finally:
        stop_process(server)


def run_mib_test():
    print("\n=== E11: MIB simplificada ===")
    mib = ROOT / "common" / "mib.h"
    text = mib.read_text(encoding="utf-8")
    required = ["OID_CPU", "OID_MEMORY", "OID_UPTIME", "OID_CONNECTIONS", "OID_NET_RX", "OID_NET_TX"]
    present = {name: int(name in text) for name in required}
    result = {"experiment": "mib", "objects_present": sum(present.values()), "objects_expected": len(required), **present}
    print(f"        objetos principais: {sum(present.values())}/{len(required)}")
    return result




def run_multiple_managers_test(tls=False):
    print("\n=== E12: múltiplos gerentes ===")
    agents = start_agents(2, tls=tls)
    managers = []
    with tempfile.TemporaryDirectory(prefix="mini_snmp_multi_mgr_") as td:
        config = Path(td) / "agents.conf"
        generate_config(config, 2)
        clean_logs()
        ensure_log_header()
        try:
            managers.append(start_manager(config, interval=1, tls=tls, trap_port=6000))
            managers.append(start_manager(config, interval=1, tls=tls, trap_port=6001))
            time.sleep(5)
            alive = [int(p.poll() is None) for p in managers]
            rows = read_metrics()
            agent_names = {r.get("agent") for r in rows if r.get("status") == "ONLINE"}
            result = {"experiment": "multiple_managers", "managers_alive": sum(alive), "expected_managers": 2,
                      "agents_seen": len(agent_names), "tls": int(tls)}
            print(f"        gerentes ativos={sum(alive)}/2 | agents observados={len(agent_names)}")
            return result
        finally:
            for m in reversed(managers):
                stop_process(m)
            stop_all(agents)

def write_csv(rows, path):
    if not rows:
        return
    keys = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description="Testes automatizados do Mini-SNMP")
    parser.add_argument("--tls", action="store_true", help="executa os testes de monitoramento com TLS")
    parser.add_argument("--duration", type=int, default=5, help="segundos adicionais por cenário")
    parser.add_argument("--counts", default="1,3,5,10", help="quantidades de Agents")
    parser.add_argument("--only-scalability", action="store_true", help="executa apenas o teste de escalabilidade com as quantidades informadas")
    parser.add_argument("--compare-tls", action="store_true", help="executa a mesma escalabilidade sem TLS e com TLS")
    args = parser.parse_args()

    ensure_dirs()
    ensure_binaries()

    counts = [int(x) for x in args.counts.split(",") if x.strip()]
    all_results = []

    if args.compare_tls:
        print(f"[COMPARAÇÃO] Cenários: {counts} | duração={args.duration}s")
        plain_results = run_scalability(False, counts, args.duration)
        all_results.extend(plain_results)
        secure_results = run_scalability(True, counts, args.duration)
        all_results.extend(secure_results)

        write_csv(plain_results, RESULTS / "results_plain.csv")
        (RESULTS / "results_plain.json").write_text(
            json.dumps(plain_results, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        write_csv(secure_results, RESULTS / "results_tls.csv")
        (RESULTS / "results_tls.json").write_text(
            json.dumps(secure_results, indent=2, ensure_ascii=False), encoding="utf-8"
        )

        comparison = []
        plain_by_n = {r["configured_agents"]: r for r in plain_results}
        tls_by_n = {r["configured_agents"]: r for r in secure_results}
        for n in counts:
            p = plain_by_n.get(n, {})
            t = tls_by_n.get(n, {})
            p_lat = p.get("latency_avg_us")
            t_lat = t.get("latency_avg_us")
            comparison.append({
                "agents": n,
                "plain_success_pct": p.get("success_rate_pct"),
                "tls_success_pct": t.get("success_rate_pct"),
                "plain_latency_avg_us": p_lat,
                "tls_latency_avg_us": t_lat,
                "tls_difference_pct": round(((t_lat - p_lat) / p_lat) * 100, 2) if p_lat else None,
            })
        write_csv(comparison, RESULTS / "scalability_tls_comparison.csv")
        (RESULTS / "scalability_tls_comparison.json").write_text(
            json.dumps(comparison, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        print(f"CSV comparação: {RESULTS / 'scalability_tls_comparison.csv'}")
        return

    if args.only_scalability:
        all_results.extend(run_scalability(args.tls, counts, args.duration))
    else:
        all_results.append(run_protocol_tests())
        all_results.append(run_mib_test())
        all_results.append(run_remote_set_test())
        all_results.append(run_trap_test())
        all_results.append(run_overhead_test())
        all_results.extend(run_scalability(args.tls, counts, args.duration))
        all_results.append(run_failure_test(args.tls))
        all_results.append(run_dashboard_test())
        all_results.append(run_multiple_managers_test(args.tls))

        # A comparação TLS exige os dois modos e certificados válidos para o modo seguro.
        if args.tls:
            all_results.append(run_tls_comparison(args.duration))

    result_file = RESULTS / ("results_tls.csv" if args.tls else "results_plain.csv")
    write_csv(all_results, result_file)

    json_file = RESULTS / ("results_tls.json" if args.tls else "results_plain.json")
    json_file.write_text(json.dumps(all_results, indent=2, ensure_ascii=False), encoding="utf-8")

    print("\n==============================================")
    print("TESTES FINALIZADOS")
    print(f"CSV : {result_file}")
    print(f"JSON: {json_file}")
    print("==============================================")


if __name__ == "__main__":
    main()
