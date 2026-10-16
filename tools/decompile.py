#!/usr/bin/env python3
"""Interactive SH-4 Decompiler using aipipe and local Qwen models.

Features:
- Disassembles SH-4 machine code ranges with Capstone.
- Resolves PC-relative literal pool constant references.
- Connects directly to aipipe on remote/local hosts.
- Streams Qwen thoughts and generated C code in real time.
- Optionally writes live streaming progress to a file for easy monitoring.

Usage:
    python tools/decompile.py --vram 0x8C0102C0 --size 100 --show-thinking
    python tools/decompile.py --vram 0x8C0102C0 --size 100 --live-file build/qwen_live.md
"""

import argparse
import json
import os
import struct
import sys
import time
import urllib.request
from pathlib import Path

try:
    import capstone
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False

RESEARCH_URL = "http://100.111.153.70:11434"
DEFAULT_MODEL = "hf.co/unsloth/Qwen3.8-27B-GGUF:UD-Q4_K_M"

SYSTEM_PROMPT = """You are an expert Hitachi SuperH (SH-4) reverse engineer and Sega Dreamcast C decompiler.
You are decompiling retail Sega Katana SDK / Shinobi C code from SH-4 machine code.
Target compiler: Metrowerks CodeWarrior / GNU SH-4 GCC with little-endian conventions.
Dreamcast ABI:
- Arguments in r4, r5, r6, r7.
- Return value in r0.
- Callee-saved: r8..r14.
- Return address in pr register (via sts.l pr,@-r15 / lds.l @r15+,pr).
- Delay slots execute after jumps/branches.

Instructions:
1. Analyze the disassembled SH-4 instructions, register flow, memory offsets, and PC-relative literal pools.
2. Emit clean, idiomatic, typed C code with proper struct members and descriptive variable names.
3. Wrap your code inside a ```c ... ``` block.
"""


def disassemble(bin_path: Path, vram_addr: int, byte_count: int) -> str:
    data = bin_path.read_bytes()
    offset = vram_addr - 0x8C010000
    if offset < 0 or offset + byte_count > len(data):
        raise ValueError(f"Offset 0x{offset:x} out of bounds")

    chunk = data[offset : offset + byte_count]
    lines = []

    if HAS_CAPSTONE:
        md = capstone.Cs(capstone.CS_ARCH_SH, capstone.CS_MODE_SH4 | capstone.CS_MODE_LITTLE_ENDIAN)
        curr = 0
        while curr < len(chunk):
            addr = vram_addr + curr
            sub = chunk[curr : curr + 32]
            insns = list(md.disasm(sub, addr, count=1))
            if insns:
                insn = insns[0]
                raw = " ".join(f"{b:02x}" for b in insn.bytes)
                lines.append(f"0x{insn.address:08X}:  {raw:<6}  {insn.mnemonic:<8} {insn.op_str}")
                curr += insn.size
            else:
                if curr + 2 <= len(chunk):
                    w = struct.unpack("<H", chunk[curr : curr + 2])[0]
                    lines.append(f"0x{addr:08X}:  {chunk[curr]:02x} {chunk[curr+1]:02x}    .word    0x{w:04x}")
                    curr += 2
                else:
                    lines.append(f"0x{addr:08X}:  {chunk[curr]:02x}       .byte    0x{chunk[curr]:02x}")
                    curr += 1
    else:
        for i in range(0, len(chunk), 2):
            w = struct.unpack("<H", chunk[i : i + 2])[0]
            lines.append(f"0x{vram_addr + i:08X}:  0x{w:04x}")

    return "\n".join(lines)


def run_decompile_stream(prompt: str, show_thinking: bool = True, live_file: Path | None = None,
                         model: str = DEFAULT_MODEL, no_think: bool = False):
    body = {
        "model": model,
        "messages": [
            {"role": "system", "content": SYSTEM_PROMPT},
            {"role": "user", "content": prompt},
        ],
        "stream": True,
        "options": {
            "temperature": 0.2,
            "num_ctx": 4096,
        }
    }
    if no_think:
        body["think"] = False

    req = urllib.request.Request(
        f"{RESEARCH_URL}/api/chat",
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"}
    )

    lf = None
    if live_file:
        live_file.parent.mkdir(parents=True, exist_ok=True)
        lf = live_file.open("w", encoding="utf-8")
        lf.write(f"# Qwen Live Decompilation Stream\n**Model**: `{model}`\n\n")
        lf.flush()

    in_thinking = False
    thinking_buffer = []
    code_buffer = []

    print(f"\n[aipipe] Querying {model} on research (M5 Max)...", flush=True)

    try:
        with urllib.request.urlopen(req, timeout=900) as resp:
            buf = b""
            while True:
                chunk = resp.read(128)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    raw_line, buf = buf.split(b"\n", 1)
                    raw_line = raw_line.strip()
                    if not raw_line:
                        continue
                    ev = json.loads(raw_line.decode("utf-8", errors="replace"))
                    msg = ev.get("message", {})
                    think_chunk = msg.get("thinking", "")
                    text_chunk = msg.get("content", "")

                    if think_chunk:
                        thinking_buffer.append(think_chunk)
                        if show_thinking:
                            if not in_thinking:
                                sys.stderr.write("\n=== [Qwen Thinking Phase] ===\n")
                                sys.stderr.flush()
                                in_thinking = True
                            sys.stderr.write(think_chunk)
                            sys.stderr.flush()
                        if lf:
                            if not in_thinking:
                                lf.write("## 💭 Reasoning Chain\n```text\n")
                                in_thinking = True
                            lf.write(think_chunk)
                            lf.flush()

                    if text_chunk:
                        if in_thinking:
                            if show_thinking:
                                sys.stderr.write("\n=== [Decompiled Output] ===\n")
                                sys.stderr.flush()
                            if lf:
                                lf.write("\n```\n\n## 💻 Decompiled Output\n")
                                lf.flush()
                            in_thinking = False

                        code_buffer.append(text_chunk)
                        sys.stdout.write(text_chunk)
                        sys.stdout.flush()
                        if lf:
                            lf.write(text_chunk)
                            lf.flush()

                    if ev.get("done"):
                        eval_count = ev.get("eval_count", 0)
                        eval_duration = ev.get("eval_duration", 1) / 1e9
                        tok_s = eval_count / eval_duration if eval_duration > 0 else 0
                        print(f"\n\n[Done] Generated {eval_count} tokens in {eval_duration:.2f}s ({tok_s:.1f} tok/s)", flush=True)
                        if lf:
                            lf.write(f"\n\n---\n*Generated {eval_count} tokens ({tok_s:.1f} tok/s)*\n")
                            lf.flush()

    finally:
        if lf:
            lf.close()

    return "".join(code_buffer)


def main():
    parser = argparse.ArgumentParser(description="SH-4 AI Decompiler")
    parser.add_argument("--bin", type=Path, default=Path("build-inputs/1ST_READ_descrambled.bin"))
    parser.add_argument("--vram", type=lambda x: int(x, 0), default=0x8C0102C0)
    parser.add_argument("--size", type=lambda x: int(x, 0), default=100)
    parser.add_argument("--name", type=str, default="controller_update_repeat")
    parser.add_argument("--show-thinking", action="store_true", default=True, help="Display live reasoning")
    parser.add_argument("--no-think", action="store_true", help="Skip thinking phase for instant code")
    parser.add_argument("--live-file", type=Path, default=Path("build/qwen_live.md"), help="File to write live stream to")
    parser.add_argument("--model", type=str, default=DEFAULT_MODEL)
    args = parser.parse_args()

    disasm_text = disassemble(args.bin, args.vram, args.size)
    print(f"=== Disassembly for {args.name} (0x{args.vram:08X}, {args.size} bytes) ===")
    print(disasm_text[:500] + ("\n..." if len(disasm_text) > 500 else ""))

    prompt = f"""Please decompile the following Hitachi SH-4 assembly function into a clean C implementation:
Function Name: {args.name}
VRAM Address: 0x{args.vram:08X}
Size: {args.size} bytes

Assembly:
{disasm_text}
"""
    run_decompile_stream(
        prompt=prompt,
        show_thinking=args.show_thinking,
        live_file=args.live_file,
        model=args.model,
        no_think=args.no_think
    )


if __name__ == "__main__":
    main()
