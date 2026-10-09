"""Shared opening-book helpers (host-only; python-chess)."""
from __future__ import annotations

import csv
import json
import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

import chess

ROOT = Path(__file__).resolve().parents[1]
CHESS_OPENINGS_DIR = ROOT / "tools/data/chess-openings"
PIN_FILE = CHESS_OPENINGS_DIR / "PIN"

FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211


def piece_code(p: chess.Piece | None) -> int:
    if p is None:
        return 0
    base = {
        chess.PAWN: 1,
        chess.KNIGHT: 2,
        chess.BISHOP: 3,
        chess.ROOK: 4,
        chess.QUEEN: 5,
        chess.KING: 6,
    }[p.piece_type]
    return base + (6 if p.color == chess.BLACK else 0)


def has_legal_ep_capture(board: chess.Board) -> bool:
    if board.ep_square is None:
        return False
    for mv in board.generate_legal_moves():
        if (
            mv.to_square == board.ep_square
            and board.piece_at(mv.from_square).piece_type == chess.PAWN
        ):
            return True
    return False


def position_key(board: chess.Board) -> bytes:
    """34-byte key matching chess_position_key() in chess_game.c."""
    key = bytearray(34)
    for i in range(32):
        lo = piece_code(board.piece_at(2 * i))
        hi = piece_code(board.piece_at(2 * i + 1))
        key[i] = (hi << 4) | lo
    castling = 0
    if board.has_kingside_castling_rights(chess.WHITE):
        castling |= 0x01
    if board.has_queenside_castling_rights(chess.WHITE):
        castling |= 0x02
    if board.has_kingside_castling_rights(chess.BLACK):
        castling |= 0x04
    if board.has_queenside_castling_rights(chess.BLACK):
        castling |= 0x08
    key[32] = castling | (0x10 if board.turn == chess.BLACK else 0)
    if has_legal_ep_capture(board):
        key[33] = board.ep_square % 8
    else:
        key[33] = 0xFF
    return bytes(key)


def fnv1a64(data: bytes) -> int:
    h = FNV_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def fen_key(board: chess.Board) -> str:
    """Stable FEN for cache lookup (full move numbers)."""
    return board.fen()


def pgn_to_uci_moves(pgn: str) -> list[str]:
    board = chess.Board()
    moves: list[str] = []
    for token in pgn.split():
        if token in ("1-0", "0-1", "1/2-1/2", "*"):
            break
        if re.match(r"^\d+\.+$", token):
            continue
        mv = board.parse_san(token)
        moves.append(mv.uci())
        board.push(mv)
    return moves


@dataclass
class LineSpec:
    weight: int
    moves: list[str]
    prefix: list[str] | None
    source: str  # eco + name for comments


def uci_to_move(board: chess.Board, uci: str) -> chess.Move:
    mv = chess.Move.from_uci(uci)
    if mv not in board.legal_moves:
        raise ValueError("illegal move %s in %s" % (uci, board.fen()))
    return mv


def walk_line(
    spec: LineSpec,
) -> list[tuple[bytes, chess.Move, int]]:
    """Yield (position_key_bytes, move, weight) for each booked move."""
    board = chess.Board()
    out: list[tuple[bytes, chess.Move, int]] = []
    if spec.prefix:
        for uci in spec.prefix:
            board.push(uci_to_move(board, uci))
    for uci in spec.moves:
        pk = position_key(board)
        mv = uci_to_move(board, uci)
        out.append((pk, mv, spec.weight))
        board.push(mv)
    return out


def build_table_from_specs(
    lines: list[LineSpec],
) -> dict[int, dict[tuple[int, int], int]]:
    """key_hash -> {(from,to): weight} using max weight per move."""
    table: dict[int, dict[tuple[int, int], int]] = defaultdict(dict)
    for spec in lines:
        for pk, mv, w in walk_line(spec):
            k = fnv1a64(pk)
            fr, to = mv.from_square, mv.to_square
            prev = table[k].get((fr, to), 0)
            table[k][(fr, to)] = max(prev, w)
    return table


def load_chess_openings_rows() -> list[tuple[str, str, str]]:
    rows: list[tuple[str, str, str]] = []
    for vol in ("a", "b", "c", "d", "e"):
        path = CHESS_OPENINGS_DIR / ("%s.tsv" % vol)
        with path.open(encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f, delimiter="\t")
            for row in reader:
                rows.append((row["eco"], row["name"], row["pgn"]))
    return rows


# Popular mainlines: pick shortest TSV row whose name contains the needle (case-sensitive).
SKIP_NAME_FRAGMENTS = (
    "Gambit",
    "Ware Opening",
    "Amar",
    "Barnes",
    "Wing Gambit",
    "Grob",
    "Orangutan",
    "Unnamed",
    "Traxler",
    "Fried Liver",
)


def mainstream_name(name: str) -> bool:
    return not any(fragment in name for fragment in SKIP_NAME_FRAGMENTS)


OPENING_NEEDLES: list[tuple[str, str]] = [
    ("B22", "Sicilian Defense: Alapin Variation"),
    ("B30", "Sicilian Defense: Rossolimo Variation"),
    ("B50", "Sicilian Defense"),
    ("B90", "Sicilian Defense: Najdorf Variation"),
    ("B40", "Sicilian Defense: French Variation"),
    ("C00", "French Defense"),
    ("C10", "French Defense: Rubinstein Variation"),
    ("C20", "King's Pawn Game"),
    ("C42", "Petrov's Defense"),
    ("C50", "Italian Game"),
    ("C60", "Ruy Lopez"),
    ("C65", "Ruy Lopez: Berlin Defense"),
    ("C70", "Ruy Lopez"),
    ("D00", "Queen's Pawn Game"),
    ("D02", "Queen's Pawn Game: London System"),
    ("D06", "Queen's Gambit"),
    ("D10", "Queen's Gambit Declined"),
    ("D20", "Queen's Gambit Accepted"),
    ("D30", "Queen's Gambit Declined"),
    ("D35", "Queen's Gambit Declined: Exchange Variation"),
    ("D43", "Queen's Gambit Declined: Semi-Slav Defense"),
    ("D50", "Queen's Gambit Declined"),
    ("D60", "Queen's Gambit Declined: Orthodox Defense"),
    ("D70", "Neo-Grünfeld Defense"),
    ("D80", "Grünfeld Defense"),
    ("E00", "Queen's Pawn Game"),
    ("E04", "Catalan Opening"),
    ("E10", "Queen's Pawn Game"),
    ("E20", "Nimzo-Indian Defense"),
    ("E32", "Nimzo-Indian Defense"),
    ("E60", "King's Indian Defense"),
    ("E90", "King's Indian Defense"),
    ("B10", "Caro-Kann Defense"),
    ("B12", "Caro-Kann Defense"),
    ("B18", "Caro-Kann Defense"),
    ("A40", "Queen's Pawn Game"),
    ("A45", "Indian Defense"),
    ("A48", "Indian Defense"),
    ("A57", "Benko Gambit"),
    ("C11", "French Defense: Classical Variation"),
    ("C43", "Petrov's Defense: Modern Attack"),
    ("C44", "King's Pawn Game"),
    ("C47", "Four Knights Game"),
    ("C53", "Italian Game: Giuoco Piano"),
    ("C54", "Italian Game"),
    ("C55", "Italian Game: Two Knights Defense"),
    ("C78", "Ruy Lopez: Morphy Defense"),
    ("D02", "London System"),
    ("E15", "Queen's Indian Defense"),
    ("E94", "King's Indian Defense"),
]

MIN_PLIES = 6
MAX_PLIES = 10

RESP_PREFIXES: list[tuple[str, list[str]]] = [
    ("c2c4", ["c2c4"]),
    ("g1f3", ["g1f3"]),
    ("e2e3", ["e2e3"]),
    ("c2c3", ["c2c3"]),
]

BLACK_REPLY_DEPTH = 1
WHITE_ROOT_EXTRA = True


def validate_uci_line(moves: list[str]) -> bool:
    board = chess.Board()
    try:
        for uci in moves:
            mv = chess.Move.from_uci(uci)
            if mv not in board.legal_moves:
                return False
            board.push(mv)
    except ValueError:
        return False
    return True


def _pick_row(
    rows: list[tuple[str, str, str]], eco: str, needle: str
) -> tuple[str, str, list[str]] | None:
    candidates = [r for r in rows if r[0].startswith(eco[:3]) and needle in r[1]]
    if not candidates:
        candidates = [r for r in rows if needle in r[1]]
    if not candidates:
        return None
    best: tuple[str, str, list[str]] | None = None
    for eco_name, name, pgn in sorted(candidates, key=lambda r: (len(pgn_to_uci_moves(r[2])), r[1])):
        if not mainstream_name(name):
            continue
        moves = pgn_to_uci_moves(pgn)
        if MIN_PLIES <= len(moves) <= MAX_PLIES and validate_uci_line(moves):
            best = (eco_name, name, moves)
            break
    return best


def select_opening_lines(
    rows: list[tuple[str, str, str]],
) -> list[tuple[str, str, list[str]]]:
    """Return (eco, name, uci_moves) for each selected mainline."""
    seen: set[tuple[str, ...]] = set()
    out: list[tuple[str, str, list[str]]] = []
    for eco, needle in OPENING_NEEDLES:
        picked = _pick_row(rows, eco, needle)
        if picked is None:
            continue
        eco_name, name, moves = picked
        key = tuple(moves)
        if key in seen:
            continue
        seen.add(key)
        out.append((eco_name, name, moves))
    return out


def build_position_tree(
    line_moves: list[list[str]],
    prefixes: list[list[str]] | None = None,
) -> dict[str, set[str]]:
    """fen -> legal UCI continuations present in the book."""
    tree: dict[str, set[str]] = defaultdict(set)
    all_prefixes = prefixes or []
    for moves in line_moves:
        board = chess.Board()
        for uci in moves:
            tree[fen_key(board)].add(uci)
            board.push(chess.Move.from_uci(uci))
    for prefix in all_prefixes:
        board = chess.Board()
        for uci in prefix:
            board.push(chess.Move.from_uci(uci))
        tree[fen_key(board)]  # ensure node exists
    return tree


def scale_weights(counts: dict[str, int], allowed: set[str]) -> dict[str, int]:
    filtered = {m: counts[m] for m in allowed if m in counts and counts[m] > 0}
    if not filtered:
        return {m: 50 for m in allowed}
    top = max(filtered.values())
    out: dict[str, int] = {}
    for m in allowed:
        c = filtered.get(m, 0)
        if c <= 0:
            continue
        out[m] = max(1, min(100, round(100 * c / top)))
    return out


def build_table_from_tree(
    tree: dict[str, set[str]],
    cache: dict[str, dict[str, int]],
) -> dict[int, dict[tuple[int, int], int]]:
    table: dict[int, dict[tuple[int, int], int]] = {}
    for fen, children in sorted(tree.items()):
        if not children:
            continue
        board = chess.Board(fen)
        counts = cache.get(fen, {})
        weighted = scale_weights(counts, children)
        if len(weighted) > 4:
            weighted = dict(
                sorted(weighted.items(), key=lambda x: (-x[1], x[0]))[:4]
            )
        pk = fnv1a64(position_key(board))
        moves_map: dict[tuple[int, int], int] = {}
        for uci, w in weighted.items():
            mv = chess.Move.from_uci(uci)
            moves_map[(mv.from_square, mv.to_square)] = w
        table[pk] = moves_map
    return table


def load_explorer_cache(path: Path) -> tuple[dict[str, dict[str, int]], dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    cache: dict[str, dict[str, int]] = {}
    for fen, entry in data.get("positions", {}).items():
        cache[fen] = dict(entry.get("moves", {}))
    return cache, data


def emit_lines_txt(
    path: Path,
    specs: list[LineSpec],
    meta: dict,
) -> None:
    lines: list[str] = [
        "# Generated opening lines — do not edit by hand.",
        "# Source: lichess-org/chess-openings (CC0) + Lichess explorer weights.",
        "# Regenerate: python3 tools/gen_opening_book.py",
        "# Cache: %s (%s)" % (meta.get("cache_path"), meta.get("primary_source")),
        "",
    ]
    for spec in specs:
        src = spec.source
        if spec.prefix:
            for uci in spec.moves:
                lines.append(
                    "RESP %s %d %s  # %s"
                    % (spec.prefix[0], spec.weight, uci, src)
                )
        else:
            lines.append("%d %s  # %s" % (spec.weight, " ".join(spec.moves), src))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
