"""Elo intervals and pentanomial GSPRT used by the match harness.

Per-arm interval
    One game is a trinomial score X in {0, 1/2, 1}. The variance is
    E[X^2] - (E[X])^2 from the win/draw/loss counts, which is smaller
    than the Bernoulli variance p(1-p) whenever there are draws. The
    reported 95% Elo interval maps the score interval
    p ± z * se through the logistic Elo function (bound transformation).
    z is 1.96. A separate delta-method standard error,
    se_elo = |dElo/dp| * se_p, is kept for combining arms.

Two arms versus one opponent
    Each arm's Elo and delta-method SE are computed on their own games.
    The difference uses se = sqrt(se_a^2 + se_b^2). That treats the
    samples as independent, so noise from the shared opponent is counted
    twice and the interval is conservative. The logistic derivative
    diverges as the score approaches 0 or 1, so the interval is not
    usable there and is not an acceptance test.

Pentanomial GSPRT
    A color-swapped pair is one of LL, LD+DL, DD+WL+LW, WD+DW, WW.
    The stopping LLR is Fishtest's logistic generalized SPRT: the
    empirical distribution on {0, 1/4, 1/2, 3/4, 1}, maximum-likelihood
    distributions constrained to the two expected scores, and
    N * E[log(p1/p0)]. Empty bins are filled with 1e-3 before that
    step, matching Fishtest's regularize(). Wald bounds are
    log(beta/(1-alpha)) and log((1-beta)/alpha). There is no dynamic
    overshoot correction.
"""
from __future__ import annotations

import hashlib
import json
import math
import os

Z95 = 1.96
# Fishtest LLRcalc.regularize fills a zero bin with this mass.
REGULARIZE_EPS = 1e-3
# 800 / ln(10). Fishtest LLRcalc.nelo_divided_by_nt.
NELO_PER_NT = 800.0 / math.log(10.0)

STATE_VERSION = 2

DIFF_NOTE = (
    "Separate matches versus a common opponent combine independent "
    "delta-method standard errors as sqrt(se_a^2+se_b^2). This is "
    "conservative (the shared opponent's sampling noise is counted in "
    "both arms) and unreliable near 0% or 100% scores. It is not an "
    "acceptance test; use a direct head-to-head SPRT."
)


def cli_ai_command(movetime: int | None, nodes: int, depth: int) -> str:
    """chess-cli search line. movetime and a node budget are not combined."""
    if movetime is not None:
        if movetime <= 0 or movetime > 600000:
            raise ValueError("movetime must be 1..600000 milliseconds")
        return "ai movetime %d" % movetime
    if nodes <= 0 or nodes > 100000000 or depth <= 0 or depth > 64:
        raise ValueError("nodes/depth out of the CLI range")
    return "ai %d %d" % (nodes, depth)


def sha256_file(path: str) -> str:
    """SHA256 hex digest of a file's bytes."""
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            chunk = fh.read(65536)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def match_config(
    candidate: str,
    baseline: str,
    budget: str,
    openings: list[str],
    elo0: float,
    elo1: float,
    alpha: float,
    beta: float,
    plies: int,
    candidate_sha256: str,
    baseline_sha256: str,
    openings_sha256: str,
) -> dict:
    """Fingerprint for a resumable head-to-head run.

    Paths are recorded for the error text. Identity of an engine is the
    SHA256 of the binary, so a rebuild at the same path does not match.
    openings_sha256 is the raw openings file, or the built-in list
    written one FEN per line. `openings` is the index order: pair i
    plays openings[i % len(openings)].
    """
    return {
        "version": STATE_VERSION,
        "candidate": candidate,
        "baseline": baseline,
        "candidate_sha256": candidate_sha256,
        "baseline_sha256": baseline_sha256,
        "budget": budget,
        "openings_sha256": openings_sha256,
        "openings": list(openings),
        "elo0": elo0,
        "elo1": elo1,
        "alpha": alpha,
        "beta": beta,
        "plies": plies,
    }


def fresh_state(config: dict) -> dict:
    state = dict(config)
    # Pair index -> pentanomial bin (0 LL .. 4 WW). JSON object keys
    # are strings. Counts are derived from this map.
    state["results"] = {}
    state["counts"] = [0, 0, 0, 0, 0]
    return state


def counts_from_results(results: dict) -> list[int]:
    counts = [0, 0, 0, 0, 0]
    for bin_id in results.values():
        counts[int(bin_id)] += 1
    return counts


def completed_indices(state: dict) -> set[int]:
    return {int(key) for key in state["results"]}


def record_pair(state: dict, index: int, bin_id: int) -> None:
    """Record one finished pair. A second write of the same index raises."""
    if isinstance(index, bool) or not isinstance(index, int) or index < 0:
        raise ValueError("pair index must be a non-negative integer")
    if isinstance(bin_id, bool) or not isinstance(bin_id, int) or not 0 <= bin_id <= 4:
        raise ValueError("pentanomial bin must be an integer 0..4")
    results = state.setdefault("results", {})
    key = str(index)
    if key in results:
        raise ValueError("pair index %d is already recorded" % index)
    results[key] = bin_id
    state["counts"] = counts_from_results(results)


def missing_pair_indices(completed: set[int], limit: int, count: int) -> list[int]:
    """Next pair indices to play. Holes come before any new index.

    A hole is an index below the highest completed index with no
    result. The returned batch is only holes when any exist, so a
    later success is not replayed and an earlier failure is not
    skipped. Otherwise it is the next `count` indices at or above the
    high-water mark, still below `limit`.
    """
    if count <= 0 or limit <= 0:
        return []
    high = max(completed) if completed else -1
    if high >= 0:
        holes = [i for i in range(min(high + 1, limit)) if i not in completed]
        if holes:
            return holes[:count]
    start = high + 1
    return list(range(start, min(limit, start + count)))


_FINGERPRINT = (
    "version",
    "candidate_sha256",
    "baseline_sha256",
    "budget",
    "openings_sha256",
    "openings",
    "elo0",
    "elo1",
    "alpha",
    "beta",
    "plies",
)


def same_config(saved: dict, config: dict) -> bool:
    return all(saved.get(key) == config.get(key) for key in _FINGERPRINT)


def resume_mismatches(saved: dict, config: dict) -> list[str]:
    """Human-readable reasons to refuse a resume. Empty means it matches."""
    reasons = []
    if saved.get("version") != config.get("version"):
        reasons.append(
            "state version %s is not %s"
            % (saved.get("version"), config.get("version"))
        )
    if saved.get("candidate_sha256") != config.get("candidate_sha256"):
        reasons.append(
            "candidate binary SHA256 changed (state %s, now %s). "
            "A recompile at the same path is a different engine."
            % (saved.get("candidate_sha256"), config.get("candidate_sha256"))
        )
    if saved.get("baseline_sha256") != config.get("baseline_sha256"):
        reasons.append(
            "baseline binary SHA256 changed (state %s, now %s). "
            "A recompile at the same path is a different engine."
            % (saved.get("baseline_sha256"), config.get("baseline_sha256"))
        )
    if saved.get("budget") != config.get("budget"):
        reasons.append(
            "budget changed (state %r, now %r)"
            % (saved.get("budget"), config.get("budget"))
        )
    if saved.get("openings_sha256") != config.get("openings_sha256"):
        reasons.append(
            "openings file hash changed (state %s, now %s)"
            % (saved.get("openings_sha256"), config.get("openings_sha256"))
        )
    if list(saved.get("openings") or []) != list(config.get("openings") or []):
        reasons.append("opening index order changed")
    for key in ("elo0", "elo1", "alpha", "beta", "plies"):
        if saved.get(key) != config.get(key):
            reasons.append(
                "%s changed (state %r, now %r)" % (key, saved.get(key), config.get(key))
            )
    return reasons


def write_state(path: str, state: dict) -> None:
    """Atomically replace `path` (write a temp file, fsync, rename)."""
    tmp = path + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(state, fh, indent=2, sort_keys=True)
            fh.write("\n")
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, path)
    except Exception:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def read_state(path: str) -> dict:
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    if not isinstance(data, dict) or data.get("version") != STATE_VERSION:
        found = data.get("version") if isinstance(data, dict) else None
        raise ValueError("state version %s is not %s" % (found, STATE_VERSION))
    results = data.get("results")
    if not isinstance(results, dict):
        raise ValueError("SPRT state has no pair-index map")
    normalized = {}
    for key, bin_id in results.items():
        if not isinstance(key, str) or not key.isdigit() or str(int(key)) != key:
            raise ValueError("pair index %r is not a canonical integer" % (key,))
        if isinstance(bin_id, bool) or not isinstance(bin_id, int) or not 0 <= bin_id <= 4:
            raise ValueError("pair %s result is not a pentanomial bin 0..4" % key)
        normalized[key] = bin_id
    data["results"] = normalized
    derived = counts_from_results(normalized)
    counts = data.get("counts")
    if counts is not None and counts != derived:
        raise ValueError("SPRT state counts do not match recorded pair indices")
    data["counts"] = derived
    for hex_key in ("candidate_sha256", "baseline_sha256", "openings_sha256"):
        value = data.get(hex_key)
        if not isinstance(value, str) or len(value) != 64:
            raise ValueError("SPRT state %s is not a SHA256 hex digest" % hex_key)
    return data


def logistic_elo(score: float) -> float:
    """Map a (0, 1) score to logistic Elo. Outside that range is undefined."""
    if not math.isfinite(score) or score <= 0.0 or score >= 1.0:
        return float("nan")
    elo = -400.0 * math.log10(1.0 / score - 1.0)
    # log10(1) is +0, and a negative scale turns that into a signed zero.
    if elo == 0.0:
        return 0.0
    return elo


def logistic_score(elo: float) -> float:
    return 1.0 / (1.0 + 10.0 ** (-elo / 400.0))


def trinomial_moments(wins: int, draws: int, losses: int) -> tuple[float, float, int]:
    """Return (score, variance of one game, n).

    Var(X) = (W + D/4) / n - score^2, with X in {0, 1/2, 1}.
    """
    if wins < 0 or draws < 0 or losses < 0:
        raise ValueError("negative counts")
    n = wins + draws + losses
    if n <= 0:
        raise ValueError("no games")
    score = (wins + 0.5 * draws) / n
    ex2 = (wins + 0.25 * draws) / n
    var = ex2 - score * score
    if var < 0.0:
        if var > -1e-12:
            var = 0.0
        else:
            raise ValueError("negative variance")
    return score, var, n


def elo_se_delta(score: float, se_score: float) -> float:
    """Delta method: se_elo = |dElo/dp| * se_p.

    dElo/dp = 400 / (ln(10) * p * (1-p)).
    """
    if se_score < 0.0 or not math.isfinite(se_score):
        return float("nan")
    if se_score == 0.0:
        return 0.0
    if score <= 0.0 or score >= 1.0:
        return float("inf")
    deriv = 400.0 / (math.log(10.0) * score * (1.0 - score))
    return abs(deriv) * se_score


def _elo_of_score_bound(score: float) -> float:
    if score <= 0.0:
        return float("-inf")
    if score >= 1.0:
        return float("inf")
    return logistic_elo(score)


def arm_elo(wins: int, draws: int, losses: int, z: float = Z95) -> dict:
    """Per-arm Elo, delta-method SE, and bound-transformed 95% interval.

    The interval is (elo(p - z*se), elo(p + z*se)), low end first.
    """
    score, var, n = trinomial_moments(wins, draws, losses)
    se_score = math.sqrt(var / n)
    se_elo = elo_se_delta(score, se_score)
    lo_s = score - z * se_score
    hi_s = score + z * se_score
    return {
        "wins": wins,
        "draws": draws,
        "losses": losses,
        "n": n,
        "score": score,
        "var": var,
        "se_score": se_score,
        "elo": logistic_elo(score),
        "se_elo": se_elo,
        "ci": (_elo_of_score_bound(lo_s), _elo_of_score_bound(hi_s)),
    }


def elo_difference(arm_a: dict, arm_b: dict, z: float = Z95) -> dict:
    """Elo_a - Elo_b with se = sqrt(se_a^2 + se_b^2).

    See DIFF_NOTE. Returns a non-finite interval when either SE is infinite.
    """
    se_a = arm_a["se_elo"]
    se_b = arm_b["se_elo"]
    diff = arm_a["elo"] - arm_b["elo"]
    if not all(math.isfinite(v) for v in (diff, se_a, se_b)):
        return {
            "diff": diff,
            "se": float("inf"),
            "ci": (float("nan"), float("nan")),
        }
    se = math.sqrt(se_a * se_a + se_b * se_b)
    return {"diff": diff, "se": se, "ci": (diff - z * se, diff + z * se)}


def pair_bin(score_as_white: float, score_as_black: float) -> int:
    """Candidate-POV scores of one color-swapped pair -> pentanomial index.

    0 LL, 1 LD+DL, 2 DD+WL+LW, 3 WD+DW, 4 WW.
    """
    allowed = (0.0, 0.5, 1.0)
    if score_as_white not in allowed or score_as_black not in allowed:
        raise ValueError("pair scores must be 0, 0.5, or 1")
    return int(round((score_as_white + score_as_black) * 2.0))


def sprt_bounds(alpha: float = 0.05, beta: float = 0.05) -> tuple[float, float]:
    """Classical Wald bounds (lower, upper), natural log."""
    if not (0.0 < alpha < 1.0 and 0.0 < beta < 1.0):
        raise ValueError("alpha and beta must lie in (0, 1)")
    lower = math.log(beta / (1.0 - alpha))
    upper = math.log((1.0 - beta) / alpha)
    return lower, upper


def sprt_decision(llr: float, alpha: float = 0.05, beta: float = 0.05) -> str:
    """'accept_h1', 'accept_h0', or 'continue'. Crossing includes equality."""
    lower, upper = sprt_bounds(alpha, beta)
    if llr >= upper:
        return "accept_h1"
    if llr <= lower:
        return "accept_h0"
    return "continue"


def _regularize(counts: list[float], eps: float = REGULARIZE_EPS) -> list[float]:
    if any(c < 0 for c in counts):
        raise ValueError("negative counts")
    return [float(c) if c > 0 else eps for c in counts]


def _secular(shifted: list[tuple[float, float]]) -> float:
    """Root of sum p_i a_i / (1 + x a_i) = 0. Support of a must straddle 0."""
    nonzero = [a for a, _p in shifted if a != 0.0]
    if not nonzero:
        raise ValueError("secular equation has no variation")
    v = min(nonzero)
    w = max(nonzero)
    if v >= 0.0 or w <= 0.0:
        raise ValueError("secular support must straddle 0")
    eps = 1e-12
    lo = -1.0 / w + eps
    hi = -1.0 / v - eps

    def f(x: float) -> float:
        total = 0.0
        for a, p in shifted:
            denom = 1.0 + x * a
            if denom <= 0.0:
                raise ValueError("secular iterate left the legal interval")
            total += p * a / denom
        return total

    flo = f(lo)
    fhi = f(hi)
    if flo == 0.0:
        return lo
    if fhi == 0.0:
        return hi
    if flo * fhi > 0.0:
        raise ValueError("secular root is not bracketed")
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        fm = f(mid)
        if abs(fm) < 1e-14 or (hi - lo) < 1e-14:
            return mid
        if flo * fm <= 0.0:
            hi = mid
            fhi = fm
        else:
            lo = mid
            flo = fm
    return 0.5 * (lo + hi)


def _mle_expected(pdf: list[tuple[float, float]], target: float) -> list[tuple[float, float]]:
    """Discrete MLE with a prescribed expectation. Fishtest MLE_expected."""
    shifted = [(a - target, p) for a, p in pdf]
    # Already at the target: x = 0 is the root and the MLE is pdf itself.
    mean = sum(p * a for a, p in shifted)
    if abs(mean) < 1e-12:
        return list(pdf)
    x = _secular(shifted)
    fitted = []
    for a, p in pdf:
        denom = 1.0 + x * (a - target)
        if denom <= 0.0 or p < 0.0:
            raise ValueError("MLE probability left the simplex")
        fitted.append((a, p / denom))
    got = sum(p * a for a, p in fitted)
    if abs(got - target) > 1e-6:
        raise ValueError("MLE expectation missed the constraint")
    return fitted


def llr_logistic(elo0: float, elo1: float, counts: list[float]) -> float:
    """Fishtest LLR_logistic: N * E_hat[log(p1/p0)] on the regularized pdf.

    counts has length 3 (L, D, W) or 5 (LL .. WW). elo0 and elo1 are
    logistic Elo. The returned LLR is for H1 (elo1) against H0 (elo0).
    """
    if len(counts) not in (3, 5):
        raise ValueError("counts must have length 3 or 5")
    reg = _regularize(list(counts))
    n = sum(reg)
    k = len(reg)
    pdf = [(i / (k - 1), reg[i] / n) for i in range(k)]
    s0 = logistic_score(elo0)
    s1 = logistic_score(elo1)
    pdf0 = _mle_expected(pdf, s0)
    pdf1 = _mle_expected(pdf, s1)
    llr = 0.0
    for i in range(k):
        llr += n * pdf[i][1] * math.log(pdf1[i][1] / pdf0[i][1])
    return llr


def llr_alt2(elo0: float, elo1: float, counts: list[float]) -> float:
    """Fishtest LLR_alt2 closed form, for checking the exact LLR.

    N * (s1 - s0) * (2 s - s0 - s1) / var / 2, on the regularized pdf.
    """
    reg = _regularize(list(counts))
    n = sum(reg)
    k = len(reg)
    values = [i / (k - 1) for i in range(k)]
    probs = [c / n for c in reg]
    score = sum(p * v for p, v in zip(probs, values))
    var = sum(p * (v - score) ** 2 for p, v in zip(probs, values))
    if var <= 0.0:
        return float("nan")
    s0 = logistic_score(elo0)
    s1 = logistic_score(elo1)
    return n * (s1 - s0) * (2.0 * score - s0 - s1) / var / 2.0


def pentanomial_summary(counts: list[int], z: float = Z95) -> dict:
    """Logistic and normalized Elo for pentanomial pair counts, plus LLR helpers' inputs.

    Logistic interval: bound transformation of the pair-mean ± z * se.
    A zero sample variance collapses that interval to the point estimate.
    Normalized Elo uses Fishtest's per-game sigma sqrt(2 * var_pair).
    Its SE is the plug-in (800/ln(10)) / sqrt(2 N), which does not depend
    on the draw rate, including when every pair scored 1/2. N is the
    number of pairs. A 0% or 100% sample has no finite normalized Elo.
    """
    if len(counts) != 5:
        raise ValueError("pentanomial counts have length 5")
    if any(c < 0 for c in counts):
        raise ValueError("negative counts")
    n = sum(counts)
    if n <= 0:
        raise ValueError("no pairs")
    values = (0.0, 0.25, 0.5, 0.75, 1.0)
    mu = sum(c * v for c, v in zip(counts, values)) / n
    var = sum(c * (v - mu) ** 2 for c, v in zip(counts, values)) / n
    if var < 0.0 and var > -1e-12:
        var = 0.0
    se_mu = math.sqrt(var / n) if var > 0.0 else 0.0
    logistic = logistic_elo(mu)
    logistic_ci = (
        _elo_of_score_bound(mu - z * se_mu),
        _elo_of_score_bound(mu + z * se_mu),
    )
    # Plug-in SE of normalized Elo. It does not use the sample variance.
    nelo_se = NELO_PER_NT / math.sqrt(2.0 * n)
    if var > 0.0:
        sigma_pg = math.sqrt(2.0 * var)
        nelo = ((mu - 0.5) / sigma_pg) * NELO_PER_NT
        nelo_ci = (nelo - z * nelo_se, nelo + z * nelo_se)
    elif abs(mu - 0.5) <= 1e-15:
        nelo = 0.0
        nelo_ci = (-z * nelo_se, z * nelo_se)
    else:
        nelo = float("inf") if mu > 0.5 else float("-inf")
        nelo_ci = (nelo, nelo)
    return {
        "counts": list(counts),
        "pairs": n,
        "score": mu,
        "var_pair": var,
        "se_score": se_mu,
        "logistic_elo": logistic,
        "logistic_ci": logistic_ci,
        "normalized_elo": nelo,
        "normalized_se": nelo_se,
        "normalized_ci": nelo_ci,
    }
