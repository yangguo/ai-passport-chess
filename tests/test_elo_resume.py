"""Resume regressions for head-to-head --state.

Fake binaries and a fake pair runner. No chess engine is started.
Pair 1 is index 0 and pair 2 is index 1. Two pairs share a batch when
--concurrency is 4 (two games in flight per pair).
"""
import hashlib
import io
import json
import os
import tempfile
import threading
import unittest
from contextlib import redirect_stderr
from types import SimpleNamespace

from tools.elo_compare_engines import run_h2h
from tools.elo_stats import read_state

START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
ITALIAN = "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _args(candidate, baseline, openings, state, **overrides):
    values = dict(
        candidate=candidate,
        baseline=baseline,
        openings=openings,
        state=state,
        elo0=0.0,
        elo1=10.0,
        alpha=0.05,
        beta=0.05,
        plies=6,
        pairs=2,
        concurrency=4,
        no_sprt=True,
    )
    values.update(overrides)
    return SimpleNamespace(**values)


class ResumeRegressionTests(unittest.TestCase):
    def test_recompiled_binary_at_the_same_path_is_rejected(self):
        calls = []

        def play(index, fen, candidate, baseline, budget, plies):
            calls.append(index)
            self.assertEqual(fen, START)
            return 2

        with tempfile.TemporaryDirectory() as temp:
            candidate = os.path.join(temp, "candidate")
            baseline = os.path.join(temp, "baseline")
            openings = os.path.join(temp, "openings.fen")
            state = os.path.join(temp, "sprt.json")
            with open(candidate, "wb") as fh:
                fh.write(b"candidate-v1")
            with open(baseline, "wb") as fh:
                fh.write(b"baseline-v1")
            with open(openings, "w", encoding="utf-8") as fh:
                fh.write(START + "\n")
            args = _args(candidate, baseline, openings, state, pairs=1, concurrency=1)
            code = run_h2h(args, "ai 1000 1", play_pair=play)
            self.assertEqual(code, 0)
            self.assertEqual(calls, [0])
            with open(state, encoding="utf-8") as fh:
                saved = json.load(fh)
            self.assertEqual(saved["candidate_sha256"], _sha256(b"candidate-v1"))
            self.assertEqual(saved["baseline_sha256"], _sha256(b"baseline-v1"))
            self.assertEqual(
                saved["openings_sha256"], _sha256((START + "\n").encode("utf-8"))
            )
            self.assertEqual(saved["results"], {"0": 2})
            self.assertEqual(saved["budget"], "ai 1000 1")
            self.assertEqual(saved["elo0"], 0.0)
            self.assertEqual(saved["elo1"], 10.0)

            # Identical bytes at the same path still match, and the saved
            # pair is not played again.
            with open(candidate, "wb") as fh:
                fh.write(b"candidate-v1")
            again = run_h2h(args, "ai 1000 1", play_pair=play)
            self.assertEqual(again, 0)
            self.assertEqual(calls, [0])

            with open(candidate, "wb") as fh:
                fh.write(b"candidate-v2")
            err = io.StringIO()
            with redirect_stderr(err):
                rejected = run_h2h(args, "ai 1000 1", play_pair=play)
            self.assertEqual(rejected, 1)
            self.assertEqual(calls, [0])
            text = err.getvalue()
            self.assertIn("refusing to resume", text)
            self.assertIn("candidate binary SHA256 changed", text)
            self.assertIn(_sha256(b"candidate-v1"), text)
            self.assertIn(_sha256(b"candidate-v2"), text)
            with open(state, encoding="utf-8") as fh:
                untouched = json.load(fh)
            self.assertEqual(untouched["results"], {"0": 2})
            self.assertEqual(untouched["candidate_sha256"], _sha256(b"candidate-v1"))

            with open(candidate, "wb") as fh:
                fh.write(b"candidate-v1")
            with open(baseline, "wb") as fh:
                fh.write(b"baseline-v2")
            err = io.StringIO()
            with redirect_stderr(err):
                rejected = run_h2h(args, "ai 1000 1", play_pair=play)
            self.assertEqual(rejected, 1)
            self.assertEqual(calls, [0])
            self.assertIn("baseline binary SHA256 changed", err.getvalue())
            self.assertIn(_sha256(b"baseline-v1"), err.getvalue())
            self.assertIn(_sha256(b"baseline-v2"), err.getvalue())

    def test_openings_file_hash_and_index_order_are_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            candidate = os.path.join(temp, "candidate")
            baseline = os.path.join(temp, "baseline")
            openings = os.path.join(temp, "openings.fen")
            state = os.path.join(temp, "sprt.json")
            for path, blob in (candidate, b"cand"), (baseline, b"base"):
                with open(path, "wb") as fh:
                    fh.write(blob)
            with open(openings, "w", encoding="utf-8") as fh:
                fh.write(START + "\n")
                fh.write(ITALIAN + "\n")

            def play(index, fen, candidate, baseline, budget, plies):
                return 2

            args = _args(candidate, baseline, openings, state, pairs=1, concurrency=1)
            self.assertEqual(run_h2h(args, "ai 1000 1", play_pair=play), 0)

            # A comment does not change the parsed FEN order. The file
            # hash still changes, and resume must refuse.
            with open(openings, "w", encoding="utf-8") as fh:
                fh.write("# note\n")
                fh.write(START + "\n")
                fh.write(ITALIAN + "\n")
            err = io.StringIO()
            with redirect_stderr(err):
                code = run_h2h(args, "ai 1000 1", play_pair=play)
            self.assertEqual(code, 1)
            self.assertIn("openings file hash changed", err.getvalue())

            with open(openings, "w", encoding="utf-8") as fh:
                fh.write(START + "\n")
                fh.write(ITALIAN + "\n")
            loaded = read_state(state)
            loaded["openings"] = [ITALIAN, START]
            # Keep the old file hash so this check is the order, not the hash.
            from tools.elo_stats import write_state

            write_state(state, loaded)
            err = io.StringIO()
            with redirect_stderr(err):
                code = run_h2h(args, "ai 1000 1", play_pair=play)
            self.assertEqual(code, 1)
            self.assertIn("opening index order changed", err.getvalue())

    def test_pair1_fails_and_pair2_succeeds_under_concurrency(self):
        """Pair 1 fails, pair 2 succeeds, both in one batch.

        --concurrency 4 runs two pairs (four games) together. The saved
        map must be {1: WW}. Resume plays index 0 and does not play
        index 1 again.
        """
        calls = []
        fens = {}
        lock = threading.Lock()

        def play(index, fen, candidate, baseline, budget, plies):
            with lock:
                calls.append(index)
                fens[index] = fen
                first = calls.count(index) == 1
            if index == 0 and first:
                return None
            if index == 0:
                return 0
            if index == 1:
                return 4
            raise AssertionError("unexpected pair index %s" % index)

        with tempfile.TemporaryDirectory() as temp:
            candidate = os.path.join(temp, "candidate")
            baseline = os.path.join(temp, "baseline")
            openings = os.path.join(temp, "openings.fen")
            state = os.path.join(temp, "sprt.json")
            for path, blob in (candidate, b"cand-a"), (baseline, b"base-b"):
                with open(path, "wb") as fh:
                    fh.write(blob)
            with open(openings, "w", encoding="utf-8") as fh:
                fh.write(START + "\n")
                fh.write(ITALIAN + "\n")
            args = _args(candidate, baseline, openings, state)
            err = io.StringIO()
            with redirect_stderr(err):
                failed = run_h2h(args, "ai 1000 1", play_pair=play)
            self.assertEqual(failed, 1)
            self.assertEqual(set(calls), {0, 1})
            self.assertEqual(calls.count(1), 1)
            self.assertIn("aborted pair index [0]", err.getvalue())
            self.assertIn("saved indices [1]", err.getvalue())
            saved = read_state(state)
            self.assertEqual(saved["results"], {"1": 4})
            self.assertEqual(saved["counts"], [0, 0, 0, 0, 1])
            self.assertEqual(fens[0], START)
            self.assertEqual(fens[1], ITALIAN)
            self.assertFalse(os.path.exists(state + ".tmp"))

            resumed = run_h2h(args, "ai 1000 1", play_pair=play)
            self.assertEqual(resumed, 0)
            self.assertEqual(calls.count(0), 2)
            self.assertEqual(calls.count(1), 1)
            finished = read_state(state)
            self.assertEqual(finished["results"], {"0": 0, "1": 4})
            self.assertEqual(finished["counts"], [1, 0, 0, 0, 1])

    def test_resume_fills_a_hole_before_an_sprt_stop(self):
        # 120 whitewash pairs at indices 1..120 already cross elo1.
        # Index 0 is missing. Resume must play 0 and must not replay 1.
        calls = []

        def play(index, fen, candidate, baseline, budget, plies):
            calls.append(index)
            return 4

        with tempfile.TemporaryDirectory() as temp:
            candidate = os.path.join(temp, "candidate")
            baseline = os.path.join(temp, "baseline")
            openings = os.path.join(temp, "openings.fen")
            state = os.path.join(temp, "sprt.json")
            for path, blob in (candidate, b"cand"), (baseline, b"base"):
                with open(path, "wb") as fh:
                    fh.write(blob)
            with open(openings, "w", encoding="utf-8") as fh:
                fh.write(START + "\n")
            # One pair writes a matching fingerprint. The test then
            # deletes index 0 and records whitewashes at 1..120, which
            # already cross the SPRT bound. pairs=200 would extend past
            # that sample if the hole were not filled first.
            args = _args(
                candidate,
                baseline,
                openings,
                state,
                pairs=1,
                concurrency=1,
                no_sprt=True,
            )

            def once(index, fen, candidate, baseline, budget, plies):
                return 4

            self.assertEqual(run_h2h(args, "ai 1000 1", play_pair=once), 0)
            saved = read_state(state)
            saved["results"] = {str(index): 4 for index in range(1, 121)}
            saved["counts"] = [0, 0, 0, 0, 120]
            from tools.elo_stats import write_state

            write_state(state, saved)
            sprt_args = _args(
                candidate,
                baseline,
                openings,
                state,
                pairs=200,
                concurrency=4,
                no_sprt=False,
            )
            code = run_h2h(sprt_args, "ai 1000 1", play_pair=play)
            self.assertEqual(code, 0)
            self.assertEqual(calls, [0])
            finished = read_state(state)
            self.assertEqual(finished["results"]["0"], 4)
            self.assertEqual(finished["counts"], [0, 0, 0, 0, 121])
            self.assertNotIn("121", finished["results"])


if __name__ == "__main__":
    unittest.main()
