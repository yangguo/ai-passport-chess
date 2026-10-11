"""Oracle checks for Elo intervals and the pentanomial LLR.

Expected numbers are literals. The 60%/200-game interval is the
bound transformation of a trinomial (here Bernoulli) score interval
with z=1.96, not a call back into the helper under test.
"""
import math
import os
import tempfile
import unittest

from tools.elo_stats import (
    DIFF_NOTE,
    arm_elo,
    cli_ai_command,
    elo_difference,
    fresh_state,
    llr_alt2,
    llr_logistic,
    logistic_elo,
    match_config,
    missing_pair_indices,
    pair_bin,
    record_pair,
    resume_mismatches,
    pentanomial_summary,
    read_state,
    same_config,
    sprt_bounds,
    sprt_decision,
    write_state,
)


def _alt2_from_the_pdf(counts, elo0, elo1):
    """Fishtest LLR_alt2 written out here, not imported.

    N * (s1-s0) * (2s - s0 - s1) / (2 var) on the empirical pair scores
    {0, 1/4, 1/2, 3/4, 1}. No zero bins, so there is nothing to regularize.
    """
    n = float(sum(counts))
    values = [i / 4.0 for i in range(5)]
    probs = [c / n for c in counts]
    score = sum(p * v for p, v in zip(probs, values))
    var = sum(p * (v - score) ** 2 for p, v in zip(probs, values))
    s0 = 1.0 / (1.0 + 10.0 ** (-elo0 / 400.0))
    s1 = 1.0 / (1.0 + 10.0 ** (-elo1 / 400.0))
    return n * (s1 - s0) * (2.0 * score - s0 - s1) / var / 2.0


class EloIntervalTests(unittest.TestCase):
    def test_sixty_percent_of_two_hundred_is_ordered(self):
        # 120 wins, 0 draws, 80 losses. Trinomial variance equals
        # p(1-p) = 0.24. se = sqrt(0.24/200). z = 1.96.
        # score interval about [0.53210, 0.66790]
        # logistic Elo about [22.34, 121.37], which prints as [22, 121].
        arm = arm_elo(120, 0, 80)
        lo, hi = arm["ci"]
        self.assertEqual("[%.0f, %.0f]" % (lo, hi), "[22, 121]")
        self.assertAlmostEqual(lo, 22.338603205020267, places=6)
        self.assertAlmostEqual(hi, 121.37420115636705, places=6)
        self.assertAlmostEqual(arm["elo"], 70.43650362227247, places=6)
        self.assertAlmostEqual(arm["var"], 0.24, places=12)
        self.assertAlmostEqual(arm["se_elo"], 25.07400360344115, places=6)
        self.assertLess(lo, arm["elo"])
        self.assertLess(arm["elo"], hi)

    def test_draws_shrink_the_variance(self):
        # 20 wins, 60 draws, 20 losses over 100 games. Score 0.5.
        # E[X^2] = (20 + 0.25*60)/100 = 0.35, var = 0.35 - 0.25 = 0.10.
        # Bernoulli variance at the same score is 0.25.
        arm = arm_elo(20, 60, 20)
        self.assertAlmostEqual(arm["score"], 0.5, places=12)
        self.assertAlmostEqual(arm["var"], 0.10, places=12)
        self.assertLess(arm["var"], 0.5 * 0.5)
        self.assertAlmostEqual(arm["se_score"], math.sqrt(0.10 / 100.0), places=12)
        self.assertAlmostEqual(arm["elo"], 0.0, places=9)
        self.assertAlmostEqual(arm["ci"][0], -43.29122081748649, places=6)
        self.assertAlmostEqual(arm["ci"][1], 43.29122081748646, places=6)
        self.assertLess(arm["ci"][0], arm["ci"][1])

    def test_difference_combines_independent_standard_errors(self):
        # Hand SE values for 120/0/80 and 100/0/100, each over 200 games.
        se_a = 25.07400360344115
        se_b = 24.567405854855018
        pst = arm_elo(120, 0, 80)
        main = arm_elo(100, 0, 100)
        self.assertAlmostEqual(pst["se_elo"], se_a, places=6)
        self.assertAlmostEqual(main["se_elo"], se_b, places=6)
        diff = elo_difference(pst, main)
        se = math.sqrt(se_a * se_a + se_b * se_b)
        self.assertAlmostEqual(diff["se"], se, places=6)
        self.assertAlmostEqual(diff["diff"], logistic_elo(0.6) - 0.0, places=6)
        self.assertAlmostEqual(diff["ci"][0], diff["diff"] - 1.96 * se, places=6)
        self.assertAlmostEqual(diff["ci"][1], diff["diff"] + 1.96 * se, places=6)
        self.assertAlmostEqual(diff["ci"][0], 1.6334377344299469, places=5)
        self.assertAlmostEqual(diff["ci"][1], 139.239569510115, places=5)
        self.assertIn("conservative", DIFF_NOTE)
        self.assertIn("0%", DIFF_NOTE)
        self.assertIn("100%", DIFF_NOTE)

    def test_extreme_score_difference_is_not_an_interval(self):
        blown = elo_difference(arm_elo(8, 0, 0), arm_elo(1, 0, 1))
        self.assertFalse(math.isfinite(blown["ci"][0]))
        self.assertFalse(math.isfinite(blown["ci"][1]))


class PentanomialTests(unittest.TestCase):
    def test_pair_bins(self):
        self.assertEqual(pair_bin(0.0, 0.0), 0)
        self.assertEqual(pair_bin(0.0, 0.5), 1)
        self.assertEqual(pair_bin(0.5, 0.0), 1)
        self.assertEqual(pair_bin(1.0, 0.0), 2)
        self.assertEqual(pair_bin(0.0, 1.0), 2)
        self.assertEqual(pair_bin(0.5, 0.5), 2)
        self.assertEqual(pair_bin(1.0, 0.5), 3)
        self.assertEqual(pair_bin(0.5, 1.0), 3)
        self.assertEqual(pair_bin(1.0, 1.0), 4)

    def test_wald_bounds(self):
        lower, upper = sprt_bounds(0.05, 0.05)
        self.assertAlmostEqual(lower, math.log(0.05 / 0.95), places=12)
        self.assertAlmostEqual(upper, math.log(0.95 / 0.05), places=12)
        self.assertEqual(sprt_decision(0.0), "continue")
        self.assertEqual(sprt_decision(upper), "accept_h1")
        self.assertEqual(sprt_decision(lower), "accept_h0")

    def test_logistic_llr_matches_a_frozen_fishtest_value(self):
        # No empty bins, so Fishtest's 1e-3 regularize() does not move
        # the pdf. LLR_logistic(elo0=0, elo1=10) on these five counts.
        counts = [10, 20, 40, 25, 5]
        self.assertAlmostEqual(
            llr_logistic(0.0, 10.0, counts), -0.43748623289103494, places=9
        )
        # Independent closed form (LLR_alt2) for the same counts.
        self.assertAlmostEqual(
            _alt2_from_the_pdf(counts, 0.0, 10.0), -0.43277951112273455, places=9
        )
        self.assertAlmostEqual(
            llr_alt2(0.0, 10.0, counts), _alt2_from_the_pdf(counts, 0.0, 10.0), places=9
        )
        # The exact GSPRT and the closed form agree on this mild sample.
        self.assertLess(
            abs(llr_logistic(0.0, 10.0, counts) - _alt2_from_the_pdf(counts, 0.0, 10.0)),
            0.01,
        )

    def test_lopsided_pairs_cross_the_wald_bounds(self):
        # 120 whitewash pairs. Zeros are regularized, so this literal
        # includes that 1e-3 fill.
        win = llr_logistic(0.0, 10.0, [0, 0, 0, 0, 120])
        loss = llr_logistic(0.0, 10.0, [120, 0, 0, 0, 0])
        self.assertAlmostEqual(win, 3.404235111885967, places=9)
        self.assertAlmostEqual(loss, -3.503635292191127, places=9)
        self.assertEqual(sprt_decision(win), "accept_h1")
        self.assertEqual(sprt_decision(loss), "accept_h0")

    def test_normalized_elo_se_depends_only_on_the_pair_count(self):
        # 100 symmetric pairs. Normalized Elo is 0.
        # se = (800/ln(10)) / sqrt(2 * 100).
        summary = pentanomial_summary([10, 20, 40, 20, 10])
        se = (800.0 / math.log(10.0)) / math.sqrt(200.0)
        self.assertAlmostEqual(summary["score"], 0.5, places=12)
        self.assertAlmostEqual(summary["normalized_elo"], 0.0, places=9)
        self.assertAlmostEqual(summary["normalized_se"], se, places=9)
        self.assertAlmostEqual(summary["normalized_ci"][0], -1.96 * se, places=6)
        self.assertAlmostEqual(summary["normalized_ci"][1], 1.96 * se, places=6)
        self.assertAlmostEqual(summary["logistic_ci"][0], -37.44275309413898, places=5)
        self.assertLess(summary["logistic_ci"][0], summary["logistic_ci"][1])

    def test_all_draw_pairs_keep_the_plug_in_normalized_error(self):
        # Four DD/WL pairs. Sample variance is 0, so the logistic
        # interval is the point 0. Normalized Elo stays 0 and its SE
        # is still (800/ln(10)) / sqrt(8 games).
        summary = pentanomial_summary([0, 0, 4, 0, 0])
        se = (800.0 / math.log(10.0)) / math.sqrt(8.0)
        self.assertEqual(summary["score"], 0.5)
        self.assertEqual(summary["var_pair"], 0.0)
        self.assertEqual(summary["logistic_elo"], 0.0)
        self.assertEqual(math.copysign(1.0, summary["logistic_elo"]), 1.0)
        self.assertEqual(summary["logistic_ci"], (0.0, 0.0))
        self.assertEqual(math.copysign(1.0, summary["logistic_ci"][0]), 1.0)
        self.assertAlmostEqual(summary["normalized_elo"], 0.0, places=12)
        self.assertAlmostEqual(summary["normalized_se"], se, places=9)
        self.assertAlmostEqual(summary["normalized_ci"][0], -1.96 * se, places=6)
        self.assertAlmostEqual(summary["normalized_ci"][1], 1.96 * se, places=6)
        swept = pentanomial_summary([0, 0, 0, 0, 3])
        self.assertTrue(math.isinf(swept["normalized_elo"]))
        self.assertGreater(swept["normalized_elo"], 0.0)


class HarnessTests(unittest.TestCase):
    def test_budget_lines(self):
        self.assertEqual(cli_ai_command(1500, 1, 1), "ai movetime 1500")
        self.assertEqual(cli_ai_command(None, 200000, 4), "ai 200000 4")
        with self.assertRaises(ValueError):
            cli_ai_command(0, 200000, 4)

    def test_state_roundtrip_rejects_a_different_hypothesis(self):
        config = match_config(
            "/tmp/candidate",
            "/tmp/baseline",
            "ai movetime 1500",
            ["rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"],
            0.0,
            10.0,
            0.05,
            0.05,
            200,
            "a" * 64,
            "b" * 64,
            "c" * 64,
        )
        state = fresh_state(config)
        record_pair(state, 0, 1)
        record_pair(state, 1, 2)
        record_pair(state, 2, 2)
        with tempfile.TemporaryDirectory() as temp:
            path = os.path.join(temp, "sprt.json")
            write_state(path, state)
            loaded = read_state(path)
            self.assertFalse(os.path.exists(path + ".tmp"))
        self.assertTrue(same_config(loaded, config))
        self.assertEqual(loaded["results"], {"0": 1, "1": 2, "2": 2})
        self.assertEqual(loaded["counts"], [0, 1, 2, 0, 0])
        other = match_config(
            config["candidate"],
            config["baseline"],
            config["budget"],
            config["openings"],
            0.0,
            5.0,
            0.05,
            0.05,
            200,
            config["candidate_sha256"],
            config["baseline_sha256"],
            config["openings_sha256"],
        )
        self.assertFalse(same_config(loaded, other))
        self.assertTrue(
            any("elo1 changed" in reason for reason in resume_mismatches(loaded, other))
        )

    def test_missing_indices_retry_the_hole_and_not_the_saved_sibling(self):
        # Pair index 1 is saved and pair index 0 is not. The next batch
        # is index 0 only: not index 1 again, and not a new index 2.
        self.assertEqual(missing_pair_indices({1}, 10, 2), [0])
        self.assertEqual(missing_pair_indices({0, 1}, 10, 2), [2, 3])
        self.assertEqual(missing_pair_indices(set(), 4, 2), [0, 1])
        with self.assertRaises(ValueError):
            record_pair({"results": {"1": 4}, "counts": [0, 0, 0, 0, 1]}, 1, 4)


if __name__ == "__main__":
    unittest.main()
