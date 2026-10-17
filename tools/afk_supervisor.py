#!/usr/bin/env python3
"""Autonomous AFK Supervisor for Marvel vs. Capcom 2 (Dreamcast NA) Decompilation.

Features:
- Completely unattended (AFK) infinite loop across the local cluster.
- Dispatches 10-function waves via tools/auto_decomp_daemon.py.
- Automated multi-channel notifications (Email via ucmail every 10 waves, Discord/Slack webhooks).
- Auto-pushes Git commits to origin/qwen every 3 waves.
- Auto-syncs and deploys Cloudflare Pages dashboard every 5 waves.
- Exponential backoff on transient cluster/network disconnects.
- Clean shutdown on creation of config/STOP.
- Persistent heartbeat logging to build/afk_supervisor.log.
"""

import argparse
import datetime
import json
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error
from pathlib import Path

SUPERVISOR_LOG = Path("build/afk_supervisor.log")
STOP_FILE = Path("config/STOP")
NOTIFY_CONFIG_FILE = Path("config/notifications.json")
MVC2_ROOT = Path(__file__).resolve().parent.parent.parent / "mvc2"


def log(msg: str):
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    formatted = f"[{now}] [Supervisor] {msg}"
    print(formatted, flush=True)
    SUPERVISOR_LOG.parent.mkdir(parents=True, exist_ok=True)
    with SUPERVISOR_LOG.open("a", encoding="utf-8") as f:
        f.write(formatted + "\n")


def get_current_progress() -> dict:
    try:
        res = subprocess.run([sys.executable, "tools/progress.py", "--json"], capture_output=True, text=True, timeout=10)
        if res.returncode == 0:
            return json.loads(res.stdout)
    except Exception as e:
        log(f"Warning: could not run tools/progress.py: {e}")

    try:
        wl_file = Path("config/worklist.json")
        if wl_file.exists():
            wl = json.loads(wl_file.read_text(encoding="utf-8"))
            matched = [f for f in wl if f.get("status") == "matched"]
            code_bytes = sum(f.get("size", 64) for f in matched)
            total_code = 1310592
            pct = (code_bytes / total_code) * 100.0 if total_code else 0.0
            return {
                "matched_funcs": len(matched),
                "total_funcs": 8240,
                "matched_code_bytes": code_bytes,
                "total_code_bytes": total_code,
                "code_percent": round(pct, 3),
            }
    except Exception:
        pass
    return {"matched_funcs": 0, "total_funcs": 8240, "matched_code_bytes": 0, "total_code_bytes": 1310592, "code_percent": 0.0}


def load_notifications_config() -> dict:
    default_cfg = {
        "email": {
            "enabled": True,
            "to": "gohan@uppercut-labs.com",
            "interval": 10,
        },
        "discord_webhook": os.environ.get("DISCORD_WEBHOOK_URL", ""),
        "slack_webhook": os.environ.get("SLACK_WEBHOOK_URL", ""),
    }
    if NOTIFY_CONFIG_FILE.exists():
        try:
            cfg = json.loads(NOTIFY_CONFIG_FILE.read_text(encoding="utf-8"))
            if "email" in cfg:
                default_cfg["email"].update(cfg["email"])
            if cfg.get("discord_webhook"):
                default_cfg["discord_webhook"] = cfg["discord_webhook"]
            if cfg.get("slack_webhook"):
                default_cfg["slack_webhook"] = cfg["slack_webhook"]
        except Exception as e:
            log(f"Warning: could not read {NOTIFY_CONFIG_FILE}: {e}")
    return default_cfg


def send_email_notification(wave: int, duration_sec: float, batch_size: int, prog: dict, cfg: dict):
    email_cfg = cfg.get("email", {})
    if not email_cfg.get("enabled", True):
        return
    interval = email_cfg.get("interval", 10)
    if wave % interval != 0 and wave != 1:
        return

    to_addr = email_cfg.get("to", "gohan@uppercut-labs.com")
    subject = f"[MvC2 DC] Wave {wave} Summary: {prog['code_percent']:.3f}% ({prog['matched_funcs']}/{prog['total_funcs']} funcs)"
    funcs_pct = (prog['matched_funcs'] / prog['total_funcs'] * 100) if prog.get('total_funcs') else 0.0

    body_text = f"""============================================================
Marvel vs. Capcom 2 (Dreamcast NA) Decompilation
Cumulative Progress Summary (through Wave {wave})
============================================================

Overall Progress:
- Total Matched Code: {prog['matched_code_bytes']:,} / {prog['total_code_bytes']:,} bytes ({prog['code_percent']:.3f}%)
- Total Functions Matched: {prog['matched_funcs']} / {prog['total_funcs']} ({funcs_pct:.1f}%)
- Initialized Data: {prog.get('matched_data_bytes', 0):,} / {prog.get('total_data_bytes', 499225):,} bytes

Wave {wave} Execution Details:
- Wave Duration: {duration_sec:.1f} seconds
- Functions Processed in this Wave: {batch_size}

Surfaces & Monitoring:
- Public Live Dashboard: https://mvc2-decomp.pages.dev/dc/
- Private Tailscale Dashboard: https://titan.takaya-lionfish.ts.net:8472/
- Git Branch: qwen (origin/qwen)
- Zero Token Cost: Running locally on research (M5 Max 36GB) + command (M5 Air 24GB)
============================================================
"""
    manifest_path = Path("build/wave_notify.json")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest = {
        "from": "antigravity@uppercut-labs.com",
        "to": to_addr,
        "replyTo": to_addr,
        "subject": subject,
        "text": body_text,
    }
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    key = f"mvc2-wave-{wave}-{int(time.time())}"
    cmd = ["ucmail.cmd" if os.name == "nt" else "ucmail", "send", "-u", "-M", str(manifest_path), "-i", key, "-j"]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        if res.returncode == 0:
            log(f"Email notification for Wave {wave} sent to {to_addr} ({prog['code_percent']:.3f}%).")
        else:
            log(f"Email notification warning: {res.stderr.strip()[:200]}")
    except Exception as e:
        log(f"Email notification error: {e}")


def send_discord_notification(webhook_url: str, wave: int, duration_sec: float, batch_size: int, prog: dict):
    if not webhook_url:
        return
    payload = {
        "embeds": [{
            "title": f"🎮 Marvel vs. Capcom 2 (Dreamcast) — Wave {wave} Complete",
            "color": 16744272,  # Dreamcast orange
            "url": "https://mvc2-decomp.pages.dev/dc/",
            "fields": [
                {
                    "name": "Total Decompiled Code",
                    "value": f"**{prog['matched_code_bytes']:,}** / {prog['total_code_bytes']:,} B (**{prog['code_percent']:.3f}%**)",
                    "inline": True
                },
                {
                    "name": "Matched Functions",
                    "value": f"**{prog['matched_funcs']}** / {prog['total_funcs']}",
                    "inline": True
                },
                {
                    "name": "Wave Duration",
                    "value": f"{duration_sec:.1f}s ({batch_size} funcs)",
                    "inline": True
                },
                {
                    "name": "Cluster",
                    "value": "`research` (M5 Max 36GB, 6 slots) + `command` (M5 Air 24GB)",
                    "inline": False
                }
            ],
            "footer": {"text": "Zero-Token Autonomous Loop • Titan Host"}
        }]
    }
    try:
        req = urllib.request.Request(
            webhook_url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json", "User-Agent": "MvC2-Decomp-Supervisor/1.0"}
        )
        with urllib.request.urlopen(req, timeout=10) as resp:
            if resp.status in (200, 204):
                log(f"Discord notification for Wave {wave} delivered.")
    except Exception as e:
        log(f"Discord notification error: {e}")


def send_slack_notification(webhook_url: str, wave: int, duration_sec: float, batch_size: int, prog: dict):
    if not webhook_url:
        return
    payload = {
        "text": f"*MvC2 (Dreamcast NA)* — Wave {wave} Complete ({prog['code_percent']:.3f}%, {prog['matched_funcs']} funcs)",
        "blocks": [
            {
                "type": "section",
                "text": {
                    "type": "mrkdwn",
                    "text": (
                        f"*🎮 Marvel vs. Capcom 2 (Dreamcast) — Wave {wave} Complete*\n"
                        f"• *Overall Progress*: {prog['matched_code_bytes']:,} / {prog['total_code_bytes']:,} bytes (*{prog['code_percent']:.3f}%*)\n"
                        f"• *Functions*: {prog['matched_funcs']} / {prog['total_funcs']} matched\n"
                        f"• *Wave*: {duration_sec:.1f}s ({batch_size} funcs) | *Cluster*: research + command\n"
                        f"• *Dashboard*: <https://mvc2-decomp.pages.dev/dc/|View Live>"
                    )
                }
            }
        ]
    }
    try:
        req = urllib.request.Request(
            webhook_url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=10) as resp:
            if resp.status == 200:
                log(f"Slack notification for Wave {wave} delivered.")
    except Exception as e:
        log(f"Slack notification error: {e}")


def notify_wave(wave: int, duration_sec: float, batch_size: int):
    prog = get_current_progress()
    cfg = load_notifications_config()
    send_email_notification(wave, duration_sec, batch_size, prog, cfg)
    if cfg.get("discord_webhook"):
        send_discord_notification(cfg["discord_webhook"], wave, duration_sec, batch_size, prog)
    if cfg.get("slack_webhook"):
        send_slack_notification(cfg["slack_webhook"], wave, duration_sec, batch_size, prog)


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
            env = os.environ.copy()
            env["CLOUDFLARE_ACCOUNT_ID"] = "f81d8a09a3b225b740c17f5a39f6cf80"
            deploy_res = subprocess.run(
                ["npx.cmd" if os.name == "nt" else "npx", "wrangler", "pages", "deploy", "pages",
                 "--project-name=mvc2-decomp", "--branch=main", "--commit-dirty=true"],
                cwd=str(MVC2_ROOT),
                env=env,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
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
    parser.add_argument("--notify-email-interval", type=int, default=10, help="Email notification every N waves")
    args = parser.parse_args()

    SUPERVISOR_LOG.parent.mkdir(parents=True, exist_ok=True)
    if STOP_FILE.exists():
        STOP_FILE.unlink()

    # Determine starting wave number from existing log
    wave = 0
    if SUPERVISOR_LOG.exists():
        try:
            for line in SUPERVISOR_LOG.read_text(encoding="utf-8", errors="replace").splitlines():
                if "Starting Wave " in line:
                    import re
                    m = re.search(r"Starting Wave (\d+)", line)
                    if m:
                        wave = max(wave, int(m.group(1)))
        except Exception:
            pass

    log("=" * 60)
    log("Starting AFK Decompilation Supervisor with Notifications")
    log(f"Resuming at Wave {wave + 1} | Batch size: {args.batch_size} | Push interval: {args.push_interval} | Pages interval: {args.pages_interval} | Email interval: {args.notify_email_interval}")
    log("=" * 60)

    backoff = 15

    while True:
        if STOP_FILE.exists():
            log("Found config/STOP sentinel. Halting supervisor gracefully.")
            STOP_FILE.unlink(missing_ok=True)
            break

        wave += 1
        wave_start = time.time()
        log(f"--- Starting Wave {wave} (batch {args.batch_size}) ---")

        cmd = [sys.executable, "-u", "tools/auto_decomp_daemon.py", "--batch", str(args.batch_size)]
        res = subprocess.run(cmd)
        duration = time.time() - wave_start

        if res.returncode == 0:
            backoff = 15  # Reset backoff on success
            log(f"Wave {wave} completed successfully in {duration:.1f}s.")

            # Trigger multi-channel notification for this wave
            notify_wave(wave, duration, args.batch_size)

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
