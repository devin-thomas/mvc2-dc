#!/usr/bin/env python3
"""Autonomous AFK Supervisor for Marvel vs. Capcom 2 (Dreamcast NA) Decompilation.

Features:
- Completely unattended (AFK) infinite loop across the local cluster.
- Dispatches 10-function waves via tools/auto_decomp_daemon.py.
- Auto-pushes Git commits to origin/qwen every 3 waves.
- Auto-syncs and deploys Cloudflare Pages dashboard every 5 waves.
- Exponential backoff on transient cluster/network disconnects.
- Clean shutdown on creation of config/STOP.
- Persistent heartbeat logging to build/afk_supervisor.log.
"""

import argparse
import datetime
import os
import subprocess
import sys
import time
from pathlib import Path

SUPERVISOR_LOG = Path("build/afk_supervisor.log")
STOP_FILE = Path("config/STOP")
MVC2_ROOT = Path(__file__).resolve().parent.parent.parent / "mvc2"


def log(msg: str):
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    formatted = f"[{now}] [Supervisor] {msg}"
    print(formatted, flush=True)
    SUPERVISOR_LOG.parent.mkdir(parents=True, exist_ok=True)
    with SUPERVISOR_LOG.open("a", encoding="utf-8") as f:
        f.write(formatted + "\n")


def git_push():
    log("Pushing committed progress to origin/qwen...")
    try:
        res = subprocess.run(["git", "push", "origin", "qwen"], capture_output=True, text=True, timeout=60)
        if res.returncode == 0:
            log("Git push succeeded.")
        else:
            log(f"Git push warning: {res.stderr.strip()[:200]}")
    except Exception as e:
        log(f"Git push error: {e}")


def sync_cloudflare_pages():
    log("Synchronizing and deploying Cloudflare Pages...")
    sync_py = MVC2_ROOT / "tools" / "sync_pages.py"
    if sync_py.exists():
        try:
            subprocess.run([sys.executable, str(sync_py)], cwd=str(MVC2_ROOT), capture_output=True, timeout=30)
            deploy_res = subprocess.run(
                ["npx.cmd" if os.name == "nt" else "npx", "wrangler", "pages", "deploy", "pages",
                 "--project-name=mvc2-decomp", "--branch=main", "--commit-dirty=true"],
                cwd=str(MVC2_ROOT),
                capture_output=True,
                text=True,
                timeout=120
            )
            if deploy_res.returncode == 0:
                log("Cloudflare Pages deployment succeeded.")
            else:
                log(f"Cloudflare Pages deploy warning: {deploy_res.stderr.strip()[:200]}")
        except Exception as e:
            log(f"Pages sync error: {e}")


def main():
    parser = argparse.ArgumentParser(description="AFK Decompilation Supervisor")
    parser.add_argument("--batch-size", type=int, default=10, help="Number of functions per daemon wave")
    parser.add_argument("--push-interval", type=int, default=3, help="Push to git every N waves")
    parser.add_argument("--pages-interval", type=int, default=5, help="Deploy Cloudflare Pages every N waves")
    args = parser.parse_args()

    SUPERVISOR_LOG.parent.mkdir(parents=True, exist_ok=True)
    if STOP_FILE.exists():
        STOP_FILE.unlink()

    log("=" * 60)
    log("Starting AFK Decompilation Supervisor")
    log(f"Batch size: {args.batch_size} | Push interval: {args.push_interval} | Pages interval: {args.pages_interval}")
    log("=" * 60)

    wave = 0
    backoff = 15

    while True:
        if STOP_FILE.exists():
            log("Found config/STOP sentinel. Halting supervisor gracefully.")
            STOP_FILE.unlink(missing_ok=True)
            break

        wave += 1
        log(f"--- Starting Wave {wave} (batch {args.batch_size}) ---")

        cmd = [sys.executable, "-u", "tools/auto_decomp_daemon.py", "--batch", str(args.batch_size)]
        res = subprocess.run(cmd)

        if res.returncode == 0:
            backoff = 15  # Reset backoff on success
            log(f"Wave {wave} completed successfully.")

            # Periodic Git Push
            if wave % args.push_interval == 0:
                git_push()

            # Periodic Pages Sync
            if wave % args.pages_interval == 0:
                sync_cloudflare_pages()

            # Brief pause between waves
            time.sleep(2)

        elif res.returncode == 42:  # Cluster unreachable
            log(f"Cluster unreachable (code 42). Sleeping {backoff}s before retry...")
            time.sleep(backoff)
            backoff = min(backoff * 2, 300)

        elif res.returncode == 43:  # Storage error
            log("Storage limit or disk error (code 43). Halting.")
            break

        elif res.returncode == 44:  # Integrity mismatch
            log("Reference binary integrity mismatch (code 44). Halting.")
            break

        else:
            log(f"Daemon exited with unexpected code {res.returncode}. Retrying in 10s...")
            time.sleep(10)

    log("AFK Decompilation Supervisor terminated.")


if __name__ == "__main__":
    main()
