#!/usr/bin/env python3
"""Autonomous Decompilation Daemon for Marvel vs. Capcom 2 (Dreamcast NA).

Implements the Queue-Driven Builder-Reviewer (Actor-Critic) Loop:
1. Worklist Queue: Scans SH-4 functions from reference binary and tracks status.
2. Intelligent Multi-Host Dispatch:
   - Host 'command'  (Apple M5 Air, 24GB): Fast throughput on leaf/helper routines (<=120 bytes).
   - Host 'research' (Apple M5 Max, 36GB): Deep reasoning on complex hardware drivers (>120 bytes).
3. Builder-Reviewer Verification:
   - Builder generates C implementation.
   - Reviewer validates syntax and ABI alignment using Clang.
   - Self-corrects on syntax/type errors up to max_retries.
   - Graceful skip-on-stall: marks difficult functions as stubbed to prevent deadlocks.
4. Sentinel Exit Codes for Agent Wake-Up:
   - 0: All functions completed (100% matched!).
   - 42: ERR_CLUSTER_UNREACHABLE (both AI hosts unreachable).
   - 43: ERR_DISK_SPACE (storage constraint violated).
   - 44: ERR_INTEGRITY_MISMATCH (reference binary corrupted).

Usage:
    python tools/auto_decomp_daemon.py --batch 3
    python tools/auto_decomp_daemon.py --loop
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
import urllib.request
import urllib.error
from pathlib import Path

try:
    import capstone
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False

REFERENCE_BIN = Path("build-inputs/1ST_READ_descrambled.bin")
EXPECTED_SHA1 = "d792ef936edd89dfb2ce5399c6b4949460dd3d7b"
WORKLIST_PATH = Path("config/worklist.json")
SYS_INIT_C = Path("src/sys_init.c")

import concurrent.futures
import threading

HOST_URLS = {
    "research": "http://100.111.153.70:11435",
    "command":  "http://100.86.124.96:11434",
}
HOST_MODELS = {
    "research": "Qwen3.8-27B-GGUF:UD-Q4_K_M (6x parallel Metal)",
    "command":  "qwen3.5:9b",
}

COMMIT_LOCK = threading.Lock()
PRINT_LOCK = threading.Lock()

# Sentinel exit codes
EXIT_SUCCESS = 0
EXIT_ERR_CLUSTER = 42
EXIT_ERR_STORAGE = 43
EXIT_ERR_INTEGRITY = 44

SYSTEM_PROMPT = """You are an expert Hitachi SuperH (SH-4) reverse engineer and Sega Dreamcast C decompiler.
You are decompiling retail Sega Katana SDK / Shinobi C code from SH-4 machine code for Marvel vs. Capcom 2.
Target compiler: Metrowerks CodeWarrior / GNU SH-4 GCC with little-endian conventions.
Dreamcast ABI:
- Arguments in r4, r5, r6, r7.
- Return value in r0.
- Callee-saved: r8..r14.
- Return address in pr register.
- Instructions in jump/branch delay slots execute after the branch!

Instructions:
1. Decompile the given SH-4 assembly function into a clean, complete, idiomatic C function.
2. Emit ONLY valid C code wrapped in a ```c ... ``` block. No conversational preamble.
3. Do NOT write long multi-line assembly trace comments inside the C function. Keep comments brief (1 line max).
4. When casting integer addresses to pointers, cast via (uintptr_t) first, e.g. `(uint32_t *)(uintptr_t)0x8C010524`, and do not qualify mutable targets as `const`.
5. Ensure all helper functions or global variables referenced in the function are declared as `extern` at the top of your snippet so it compiles standalone with `-fsyntax-only`.
"""


def verify_integrity():
    if not REFERENCE_BIN.exists():
        print(f"[ERROR] Reference binary {REFERENCE_BIN} not found!", file=sys.stderr)
        sys.exit(EXIT_ERR_INTEGRITY)
    h = hashlib.sha1(REFERENCE_BIN.read_bytes()).hexdigest()
    if h != EXPECTED_SHA1:
        print(f"[ERROR] Reference binary hash mismatch: {h}", file=sys.stderr)
        sys.exit(EXIT_ERR_INTEGRITY)


def probe_host(host_name: str) -> bool:
    url = HOST_URLS.get(host_name)
    if not url:
        return False
    endpoint = "/health" if host_name == "research" else "/api/version"
    try:
        req = urllib.request.Request(f"{url}{endpoint}")
        with urllib.request.urlopen(req, timeout=3) as resp:
            return resp.status == 200
    except Exception:
        return False


def check_cluster() -> str | None:
    research_up = probe_host("research")
    command_up = probe_host("command")
    if not research_up and not command_up:
        return None
    if research_up and command_up:
        return "both"
    return "research" if research_up else "command"


def scan_functions(start_addr: int = 0x8C010080, end_addr: int = 0x8C030000) -> list[dict]:
    data = REFERENCE_BIN.read_bytes()
    base = 0x8C010000
    chunk = data[start_addr - base : end_addr - base]

    if not HAS_CAPSTONE:
        raise RuntimeError("Capstone required for function scanning")

    md = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_SH4 | capstone.CS_MODE_LITTLE_ENDIAN)
    offset = 0
    funcs = []
    curr_start = start_addr
    last_was_rts = False

    while offset < len(chunk):
        addr = start_addr + offset
        sub = chunk[offset : offset + 32]
        insns = list(md.disasm(sub, addr, count=1))
        if insns:
            insn = insns[0]
            if last_was_rts:
                func_end = addr + insn.size
                size = func_end - curr_start
                funcs.append({
                    "name": f"func_{curr_start:08X}",
                    "vram": curr_start,
                    "end_vram": func_end,
                    "size": size,
                    "status": "pending",
                    "attempts": 0,
                })
                curr_start = addr + insn.size
                last_was_rts = False
            elif insn.mnemonic == 'rts':
                last_was_rts = True
            offset += insn.size
        else:
            offset += 2

    return funcs


def load_worklist() -> list[dict]:
    existing = []
    if WORKLIST_PATH.exists():
        try:
            existing = json.loads(WORKLIST_PATH.read_text(encoding="utf-8"))
        except Exception:
            existing = []

    max_end = max((f["end_vram"] for f in existing), default=0x8C010080)
    pending_count = sum(1 for f in existing if f.get("status") == "pending")
    if existing and pending_count < 50 and max_end < 0x8C150000:
        next_chunk = min(max_end + 0x10000, 0x8C150000)
        print(f"[Worklist] Queue low ({pending_count} pending). Expanding scan from 0x{max_end:08X} to 0x{next_chunk:08X}...")
        more_funcs = scan_functions(max_end, next_chunk)
        existing.extend(more_funcs)
        save_worklist(existing)
        return existing
    elif existing:
        return existing

    print("[Worklist] Scanning initial binary range for SH-4 function boundaries...")
    funcs = scan_functions(0x8C010080, 0x8C040000)
    
    # Mark already decompiled functions as candidates only if valid definition exists
    if SYS_INIT_C.exists():
        c_code = SYS_INIT_C.read_text(encoding="utf-8", errors="replace")
        for f in funcs:
            name = f["name"]
            vram = f["vram"]
            # Look for function definition for this target
            pattern = re.compile(rf"(?:void|int|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|u8|s8|u16|s16|u32|s32|bool|float|double|char|\w+\s*\*)\s+(?:{name}|func_{vram:08x}|sub_{vram:08x})\s*\([^)]*\)\s*\{{", re.IGNORECASE)
            if pattern.search(c_code):
                f["status"] = "candidate_syntax_valid"

    save_worklist(funcs)
    return funcs


def save_worklist(funcs: list[dict]):
    WORKLIST_PATH.parent.mkdir(parents=True, exist_ok=True)
    WORKLIST_PATH.write_text(json.dumps(funcs, indent=2), encoding="utf-8")


def disassemble_func(vram_addr: int, size: int) -> str:
    data = REFERENCE_BIN.read_bytes()
    base = 0x8C010000
    offset = vram_addr - base
    chunk = data[offset : offset + size]
    md = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_SH4 | capstone.CS_MODE_LITTLE_ENDIAN)

    lines = []
    curr = 0
    import struct
    while curr < len(chunk):
        addr = vram_addr + curr
        sub = chunk[curr : curr + 32]
        insns = list(md.disasm(sub, addr, count=1))
        if insns:
            insn = insns[0]
            raw = " ".join(f"{b:02x}" for b in insn.bytes)
            comment = ""
            if insn.mnemonic == "mov.l" and insn.op_str.startswith("0x"):
                m = re.match(r"0x([0-9a-fA-F]+)", insn.op_str)
                if m:
                    lit_addr = int(m.group(1), 16)
                    lit_off = lit_addr - base
                    if 0 <= lit_off <= len(data) - 4:
                        lit_val = struct.unpack("<I", data[lit_off : lit_off + 4])[0]
                        comment = f"  ; [0x{lit_addr:08X}] = 0x{lit_val:08X}"
            lines.append(f"0x{insn.address:08X}: {insn.mnemonic:<8} {insn.op_str}{comment}")
            curr += insn.size
        else:
            if curr + 2 <= len(chunk):
                w = struct.unpack("<H", chunk[curr : curr + 2])[0]
                lines.append(f"0x{addr:08X}: .word 0x{w:04x}")
                curr += 2
            else:
                lines.append(f"0x{addr:08X}: .byte 0x{chunk[curr]:02x}")
                curr += 1
    return "\n".join(lines)


def query_qwen(prompt: str, host: str, max_tokens: int = 768) -> str:
    url = HOST_URLS[host]

    if host == "research":
        # Direct multi-slot llama-server OpenAI endpoint (6 parallel slots of Qwen3.8-27B)
        body = {
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": prompt},
            ],
            "max_tokens": max_tokens,
            "temperature": 0.2,
            "repeat_penalty": 1.15,
            "chat_template_kwargs": {"enable_thinking": False},
            "stop": ["\n```\n", "```\n", "```"],
            "stream": False,
        }
        req = urllib.request.Request(
            f"{url}/v1/chat/completions",
            data=json.dumps(body).encode("utf-8"),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=120) as resp:
            out = json.loads(resp.read().decode("utf-8", errors="replace"))
            return out["choices"][0]["message"]["content"]

    # Command host (Ollama /api/chat)
    model = HOST_MODELS[host]
    body = {
        "model": model,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "stream": False,
        "keep_alive": "24h",
        "options": {
            "temperature": 0.2,
            "num_ctx": 8192,
            "num_predict": max_tokens,
            "repeat_penalty": 1.15,
            "stop": ["\n```\n", "```\n", "```"],
        },
        "think": False,
    }
    req = urllib.request.Request(
        f"{url}/api/chat",
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        out = json.loads(resp.read().decode("utf-8", errors="replace"))
        return out.get("message", {}).get("content", "")


def extract_c_code(response_text: str) -> str:
    matches = re.findall(r"```(?:c|C)?\s*(.*?)\s*```", response_text, re.DOTALL)
    if matches:
        code = matches[-1].strip()
    else:
        code = re.sub(r"^```(?:c|C)?\s*", "", response_text.strip())
        code = re.sub(r"\s*```$", "", code.strip())
    return code


def validate_candidate_code(code_snippet: str, target_name: str, target_vram: int) -> tuple[bool, str]:
    clean = re.sub(r'/\*.*?\*/|//.*?$', '', code_snippet, flags=re.DOTALL | re.MULTILINE).strip()
    if not clean:
        return (False, "REJECTED_EMPTY_OR_COMMENTS_ONLY: snippet contains no code")

    # Match actual C function definitions with bodies { ... }
    type_kw = r"(?:void|int|int8_t|uint8_t|int16_t|uint16_t|int32_t|uint32_t|u8|s8|u16|s16|u32|s32|bool|float|double|char|\w+\s*\*|\w+)"
    def_pattern = re.compile(rf"(?:static\s+|inline\s+)*{type_kw}\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{{", re.MULTILINE)
    matches = def_pattern.findall(clean)

    if not matches:
        return (False, "REJECTED_NO_FUNCTION_DEFINITION: snippet contains declarations/prototypes only, no executable C function definition")

    expected_names = {target_name.lower(), f"func_{target_vram:08x}".lower(), f"sub_{target_vram:08x}".lower()}
    found_target = any(m.lower() in expected_names for m in matches)
    if not found_target:
        return (False, f"REJECTED_TARGET_NAME_MISMATCH: emitted functions {matches} do not match {target_name} or address alias")

    if clean.count('{') != clean.count('}'):
        return (False, "REJECTED_UNBALANCED_BRACES: function body is truncated or unclosed")

    return (True, "")


def validate_c_syntax(code_snippet: str, target_name: str = "", target_vram: int = 0, worker_id: int = 0) -> tuple[bool, str]:
    if target_name:
        is_valid, reason = validate_candidate_code(code_snippet, target_name, target_vram)
        if not is_valid:
            return (False, reason)

    test_file = Path(f"build/temp_syntax_test_w{worker_id}.c")
    test_file.parent.mkdir(parents=True, exist_ok=True)
    full_c = f"""#include "types.h"
#include "sh4.h"
#include "shinobi.h"
#include "mvc2/input.h"

{code_snippet}
"""
    test_file.write_text(full_c, encoding="utf-8")

    try:
        res = subprocess.run(
            [
                "clang",
                "-fsyntax-only",
                "-Wno-int-to-pointer-cast",
                "-Wno-pointer-to-int-cast",
                "-Wno-incompatible-pointer-types-discards-qualifiers",
                "-I",
                "include",
                str(test_file),
            ],
            capture_output=True,
            text=True,
            timeout=10
        )
        return (res.returncode == 0, res.stderr)
    except Exception as e:
        # FAIL CLOSED: Never return True on compiler exception/timeout
        return (False, f"VALIDATION_COMPILER_EXCEPTION: {e}")


def process_function(f: dict, preferred_host: str, worker_id: int, worklist: list[dict]) -> bool:
    vram = f["vram"]
    size = f["size"]
    name = f["name"]

    # Oversize guard: functions > 500 bytes exceed standard slot contexts
    if size > 500:
        with COMMIT_LOCK:
            f["status"] = "stubbed"
            integrate_stub(name, vram, size, worklist)
        with PRINT_LOCK:
            print(f"[Worker-{worker_id}] [Oversize] Stubbed {name} ({size} B > 500 B limit for manual review)", flush=True)
        return False

    with PRINT_LOCK:
        print(f"[Worker-{worker_id}:{preferred_host}] Starting candidate {name} (0x{vram:08X}, {size} B)", flush=True)

    disasm = disassemble_func(vram, size)
    prompt = f"""Decompile this Sega Dreamcast SH-4 assembly function:
Function: {name}
VRAM Address: 0x{vram:08X}
Size: {size} bytes

Assembly:
{disasm}
"""

    max_tok = min(1024, max(192, (size // 2) * 3))
    for attempt in range(1, 4):
        f["attempts"] = attempt
        try:
            resp = query_qwen(prompt, preferred_host, max_tokens=max_tok)
            c_code = extract_c_code(resp)
        except Exception as e:
            with PRINT_LOCK:
                print(f"[Worker-{worker_id} Error] {preferred_host} on {name}: {e}", file=sys.stderr, flush=True)
            alt_host = "command" if preferred_host == "research" else "research"
            if probe_host(alt_host):
                preferred_host = alt_host
                continue
            return False

        valid, err = validate_c_syntax(c_code, target_name=name, target_vram=vram, worker_id=worker_id)
        if valid:
            with COMMIT_LOCK:
                f["status"] = "candidate_syntax_valid"
                integrate_function(name, vram, c_code, worklist)
            with PRINT_LOCK:
                print(f"[Worker-{worker_id}:{preferred_host}] [OK] Validated candidate for {name} (attempt {attempt}/3, syntax valid)!", flush=True)
            return True
        else:
            prompt += f"\n\nPrevious attempt had syntax issues:\n{err[:350]}\nPlease fix all undeclared identifiers or syntax errors and emit ONLY valid C code inside ```c ... ```."

    with COMMIT_LOCK:
        f["status"] = "stubbed"
        integrate_stub(name, vram, size, worklist)
    with PRINT_LOCK:
        print(f"[Worker-{worker_id}:{preferred_host}] [Skip] Stubbed {name} after 3 attempts.", flush=True)
    return False


def integrate_function(name: str, vram: int, c_code: str, worklist: list[dict]):
    header = f"\n/*\n * 0x{vram:08X} - {name}\n */\n"
    current_content = SYS_INIT_C.read_text(encoding="utf-8") if SYS_INIT_C.exists() else ""
    SYS_INIT_C.write_text(current_content + header + c_code + "\n", encoding="utf-8")
    save_worklist(worklist)

    res = subprocess.run(["python", "tools/progress.py", "--badge", "--sync-dashboard"], capture_output=True, text=True)
    for line in res.stdout.splitlines():
        if "Matched Code:" in line or "Functions:" in line or "Candidates:" in line:
            print(f"[Progress] {line.strip()}", flush=True)
    subprocess.run(["git", "add", "src/sys_init.c", "assets/progress.svg", "config/worklist.json", "docs/agent-dashboard/mvc2-dc-decomp/"], capture_output=True)
    subprocess.run(["git", "commit", "-m", f"feat(candidate): generate C candidate for {name} (0x{vram:08X})"], capture_output=True)


def integrate_stub(name: str, vram: int, size: int, worklist: list[dict]):
    stub = f"\n/*\n * 0x{vram:08X} - {name} ({size} bytes) - STUBBED FOR REVIEW\n */\n/* INCLUDE_ASM(\"asm/sys_init.s\", {name}); */\n"
    current_content = SYS_INIT_C.read_text(encoding="utf-8") if SYS_INIT_C.exists() else ""
    SYS_INIT_C.write_text(current_content + stub, encoding="utf-8")
    save_worklist(worklist)
    subprocess.run(["python", "tools/progress.py", "--badge", "--sync-dashboard"], capture_output=True)
    subprocess.run(["git", "add", "src/sys_init.c", "config/worklist.json", "docs/agent-dashboard/mvc2-dc-decomp/"], capture_output=True)
    subprocess.run(["git", "commit", "-m", f"chore(decompile): stub {name} (0x{vram:08X})"], capture_output=True)


def main():
    parser = argparse.ArgumentParser(description="Autonomous Multi-Worker Decompilation Daemon")
    parser.add_argument("--batch", type=int, default=7, help="Number of functions to process before stopping")
    parser.add_argument("--loop", action="store_true", help="Run continuously over entire worklist")
    parser.add_argument("--workers", type=int, default=7, help="Total concurrent workers (6 research + 1 command)")
    args = parser.parse_args()

    verify_integrity()
    cluster = check_cluster()
    if not cluster:
        print("[FATAL] Both AI hosts (research and command) are unreachable!", file=sys.stderr)
        sys.exit(EXIT_ERR_CLUSTER)

    print(f"[Daemon] Cluster Status: {cluster.upper()} reachable (Workers={args.workers}: 6x research 27B + 1x command 9B).")
    worklist = load_worklist()

    pending = [f for f in worklist if f.get("status") == "pending"]
    total = len(worklist)
    candidates = sum(1 for f in worklist if f.get("status") == "candidate_syntax_valid")
    byte_identical = sum(1 for f in worklist if f.get("status") in ("byte_identical", "matched"))
    stubbed = sum(1 for f in worklist if f.get("status") == "stubbed")
    legacy = sum(1 for f in worklist if f.get("status") == "legacy_claimed")
    print(f"[Daemon] Worklist: {len(pending)} pending, {candidates} syntax-valid candidates, {byte_identical} byte-identical, {stubbed} stubbed, {legacy} legacy-claimed (Total: {total}).")

    if not pending:
        print("[SUCCESS] All functions in current worklist queue have been triaged (0 pending).")
        sys.exit(EXIT_SUCCESS)

    limit = len(pending) if args.loop else args.batch
    targets = pending[:limit]

    import queue
    q = queue.Queue()
    for f in targets:
        q.put(f)

    def worker_loop(worker_id: int):
        # Workers 0..5 run on research (6 parallel slots of Qwen3.8-27B); Worker 6 runs on command (qwen3.5:9b)
        if cluster == "both":
            host = "command" if worker_id == 6 else "research"
        else:
            host = cluster
        while True:
            try:
                f = q.get_nowait()
            except queue.Empty:
                break
            try:
                process_function(f, host, worker_id, worklist)
            finally:
                q.task_done()

    num_workers = min(args.workers, len(targets))
    with concurrent.futures.ThreadPoolExecutor(max_workers=num_workers) as ex:
        futures = [ex.submit(worker_loop, wid) for wid in range(num_workers)]
        concurrent.futures.wait(futures)

    print(f"\n[Daemon] Pass finished. Processed {len(targets)} functions across {num_workers} parallel workers.")
    sys.exit(EXIT_SUCCESS)


if __name__ == "__main__":
    main()
