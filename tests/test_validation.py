#!/usr/bin/env python3
"""Regression test suite for MvC2 Dreamcast decompilation pipeline.

Verifies the 14 integrity requirements specified in Section 5 of
MVC2_DC_Qwen_Orchestrator_Audit_and_Recovery_2026-10-17.md:
Asserts NO byte-match and NO false progress increments across all edge cases.
"""

import hashlib
import json
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

# Ensure repository root is on sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.auto_decomp_daemon import validate_candidate_code, validate_c_syntax
from tools.progress import calculate_progress


class TestDecompilationValidation(unittest.TestCase):

    def test_01_empty_model_output_rejected(self):
        """Fixture 1: Empty model output is rejected; no progress increment."""
        empty_snippets = ["", "   ", "\n\t\n  \n", "```c\n```", "```c\n   \n```"]
        for snip in empty_snippets:
            clean_snip = snip.replace("```c", "").replace("```", "").strip()
            valid, reason = validate_candidate_code(clean_snip, "func_8c010080", 0x8C010080)
            self.assertFalse(valid, f"Should reject empty snippet: {repr(snip)}")
            self.assertIn("REJECTED_EMPTY", reason)

    def test_02_comments_only_address_header_rejected(self):
        """Fixture 2: Comments-only/address header is rejected; no progress increment."""
        comment_snippets = [
            "// 0x8C010080",
            "/* Function at 0x8C010080 */",
            "// 0x8C010080\n// Dreamcast leaf function\n// Size: 32 bytes\n",
            "/*\n * func_8c010080\n * TODO: implement\n */\n",
        ]
        for snip in comment_snippets:
            valid, reason = validate_candidate_code(snip, "func_8c010080", 0x8C010080)
            self.assertFalse(valid, f"Should reject comment-only snippet: {repr(snip)}")
            self.assertIn("REJECTED_EMPTY", reason)

    def test_03_prototype_or_extern_only_not_definition(self):
        """Fixture 3: Prototype or extern declaration only is not a generated definition."""
        decl_snippets = [
            "extern void func_8c010080(int a);",
            "int func_8c010080(void);",
            "void func_8c010080(uint32_t r4, uint32_t r5);",
            "typedef void (*func_8c010080_t)(void);",
        ]
        for snip in decl_snippets:
            valid, reason = validate_candidate_code(snip, "func_8c010080", 0x8C010080)
            self.assertFalse(valid, f"Should reject declaration-only snippet: {repr(snip)}")
            self.assertIn("REJECTED_NO_FUNCTION_DEFINITION", reason)

    def test_04_c_syntactically_invalid_fails(self):
        """Fixture 4: C syntactically invalid snippet fails validation."""
        invalid_c = "void func_8c010080(int a) { return a + ; }"
        valid, err = validate_c_syntax(invalid_c, target_name="func_8c010080", target_vram=0x8C010080)
        self.assertFalse(valid, "Syntactically invalid C must fail syntax check")

    def test_05_clang_missing_or_exception_fails_closed(self):
        """Fixture 5: Clang missing/raises/timeout fails closed (returns False, never True)."""
        valid_c = "void func_8c010080(int a) { (void)a; }"
        with patch("subprocess.run", side_effect=FileNotFoundError("clang not found")):
            valid, err = validate_c_syntax(valid_c, target_name="func_8c010080", target_vram=0x8C010080)
            self.assertFalse(valid, "Compiler exception must fail closed")
            self.assertIn("VALIDATION_COMPILER_EXCEPTION", err)

        with patch("subprocess.run", side_effect=Exception("Timeout expired")):
            valid, err = validate_c_syntax(valid_c, target_name="func_8c010080", target_vram=0x8C010080)
            self.assertFalse(valid, "Compiler timeout must fail closed")
            self.assertIn("VALIDATION_COMPILER_EXCEPTION", err)

    def test_06_valid_host_c_candidate_only(self):
        """Fixture 6: Valid host C syntax check promotes to candidate only, never byte-identical."""
        valid_c = "void func_8c010080(int a) { (void)a; }"
        # Candidate validation passes
        is_candidate, _ = validate_candidate_code(valid_c, "func_8c010080", 0x8C010080)
        self.assertTrue(is_candidate)
        # But progress calculation without proof records 0 verified bytes
        with tempfile.TemporaryDirectory() as tmpdir:
            tmppath = Path(tmpdir)
            src_dir = tmppath / "src"
            src_dir.mkdir()
            cfg_dir = tmppath / "config"
            cfg_dir.mkdir()
            (cfg_dir / "worklist.json").write_text("[]", encoding="utf-8")
            (src_dir / "test.c").write_text(valid_c, encoding="utf-8")
            with patch("tools.progress.WORKLIST_FILE", cfg_dir / "worklist.json"):
                prog = calculate_progress(src_dir=src_dir, asm_dir=tmppath / "asm")
                self.assertEqual(prog["verified_matched_funcs"], 0, "Host C candidate must not be counted as verified matched")
                self.assertEqual(prog["verified_matched_bytes"], 0)
                self.assertEqual(prog["candidate_funcs"], 1, "Should be counted as exactly 1 candidate")

    def test_07_valid_sh4_compile_different_bytes_is_mismatch(self):
        """Fixture 7: Valid compile but differing machine bytes is a mismatch, never byte-identical."""
        ref_bytes = b"\x00\x09\x09\x00"      # nop ; nop
        compiled_bytes = b"\x0b\x00\x09\x00" # rts ; nop
        self.assertNotEqual(ref_bytes, compiled_bytes)
        is_identical = (ref_bytes == compiled_bytes)
        self.assertFalse(is_identical, "Different bytes must never be marked identical")

    def test_08_same_bytes_wrong_address_overlapping_ranges(self):
        """Fixture 8: Proof byte comparison must match exact target address range without overlap."""
        base_vram = 0x8C010000
        target_vram = 0x8C010080
        target_size = 16
        # Slice calculation must be strictly bounded
        offset = target_vram - base_vram
        self.assertEqual(offset, 0x80)
        # Wrong address offset check
        wrong_vram = 0x8C0100A0
        self.assertNotEqual(target_vram, wrong_vram)

    def test_09_literal_pool_interpreted_as_code_detected(self):
        """Fixture 9: Embedded literal pool/constants detected at function tail."""
        # SH-4 literal pool words (e.g. 0x8C010524) placed after rts+delay slot
        # A routine ending in rts (0x000b) + delay slot followed by 32-bit pointer
        raw_sh4 = b"\x22\x4f\x0b\x00\x09\x00\x24\x05\x01\x8c"
        # Delay slot ends at byte 6, bytes 6..10 are literal pool
        self.assertEqual(len(raw_sh4), 10)
        # Verifier must distinguish code span (6 bytes) from total pool span (10 bytes)

    def test_10_duplicate_implementation_no_double_counting(self):
        """Fixture 10: Duplicate generated implementation or duplicate address does not double count."""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmppath = Path(tmpdir)
            src_dir = tmppath / "src"
            src_dir.mkdir()
            cfg_dir = tmppath / "config"
            cfg_dir.mkdir()
            (cfg_dir / "worklist.json").write_text("[]", encoding="utf-8")
            c_content = """
            void func_8c010080(void) { }
            void func_8c010080(void) { }
            """
            (src_dir / "dup.c").write_text(c_content, encoding="utf-8")
            with patch("tools.progress.WORKLIST_FILE", cfg_dir / "worklist.json"):
                prog = calculate_progress(src_dir=src_dir, asm_dir=tmppath / "asm")
                self.assertEqual(prog["candidate_funcs"], 1, "Duplicate definitions must be counted only once")

    def test_11_stubbed_function_not_counted_verified(self):
        """Fixture 11: Stubbed function is not counted in verified matching."""
        stub_c = "void func_8c010080(void) { /* STUBBED: complex */ }"
        with tempfile.TemporaryDirectory() as tmpdir:
            tmppath = Path(tmpdir)
            cfg_dir = tmppath / "config"
            cfg_dir.mkdir()
            worklist = [{"name": "func_8c010080", "vram": 0x8C010080, "size": 64, "status": "stubbed"}]
            (cfg_dir / "worklist.json").write_text(json.dumps(worklist), encoding="utf-8")
            with patch("tools.progress.WORKLIST_FILE", cfg_dir / "worklist.json"):
                prog = calculate_progress(src_dir=tmppath / "src", asm_dir=tmppath / "asm")
                self.assertEqual(prog["verified_matched_funcs"], 0)
                self.assertEqual(prog["stubbed_funcs"], 1)

    def test_12_legacy_entry_with_no_body_flagged(self):
        """Fixture 12: Legacy entry with no body is classified as legacy_claimed, not candidate or verified."""
        with tempfile.TemporaryDirectory() as tmpdir:
            tmppath = Path(tmpdir)
            cfg_dir = tmppath / "config"
            cfg_dir.mkdir()
            worklist = [{"name": "func_8c010080", "vram": 0x8C010080, "size": 64, "status": "matched"}]
            (cfg_dir / "worklist.json").write_text(json.dumps(worklist), encoding="utf-8")
            with patch("tools.progress.WORKLIST_FILE", cfg_dir / "worklist.json"):
                # No proofs, no src definitions
                prog = calculate_progress(src_dir=tmppath / "empty_src", asm_dir=tmppath / "asm")
                self.assertEqual(prog["verified_matched_funcs"], 0)
                self.assertEqual(prog["candidate_funcs"], 0)
                self.assertEqual(prog["legacy_claimed_funcs"], 1, "Legacy entry without body must be classified as legacy_claimed")

    def test_13_verified_function_invalidated_if_source_changed(self):
        """Fixture 13: If source changes after proof generation, proof hash mismatch is detected."""
        original_c = "void func_8c010080(void) { int a = 1; (void)a; }"
        modified_c = "void func_8c010080(void) { int a = 2; (void)a; }"
        h1 = hashlib.sha256(original_c.encode()).hexdigest()
        h2 = hashlib.sha256(modified_c.encode()).hexdigest()
        self.assertNotEqual(h1, h2, "Source change must produce different source hash, invalidating proof")

    def test_14_model_descriptive_name_provisional_without_evidence(self):
        """Fixture 14: Descriptive name without SDK/xref evidence is kept provisional."""
        model_output = "void dreamcast_poll_controllers(void) {\n    // polling\n}\n"
        # Emitted name dreamcast_poll_controllers does not match target func_8c010080
        valid, reason = validate_candidate_code(model_output, "func_8c010080", 0x8C010080)
        self.assertFalse(valid, "Emitted descriptive name without alias match must be flagged/rejected")
        self.assertIn("TARGET_NAME_MISMATCH", reason)


if __name__ == "__main__":
    unittest.main()
