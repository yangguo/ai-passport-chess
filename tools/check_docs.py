#!/usr/bin/env python3
"""Validate a design repository; this does NOT run chess or firmware tests."""
import json
import re
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    'README.md', 'AGENTS.md', 'CONTRIBUTING.md', 'LICENSE',
    'docs/architecture.md', 'docs/hardware.md', 'docs/ui-interaction.md',
    'docs/chess-core.md', 'docs/testing.md', 'docs/ai-integration.md',
    'docs/storage.md', 'docs/performance.md', 'docs/build-ci-flash.md',
    'docs/acceptance.md', 'docs/roadmap.md', 'docs/sources.md',
    'docs/plans/2026-10-06-chess-design.md',
    'docs/plans/2026-10-06-implementation-plan.md', 'tests/perft/cases.json',
]
errors = []
for name in REQUIRED:
    if not (ROOT / name).is_file():
        errors.append(f'missing: {name}')
markdown = sorted(ROOT.glob('*.md')) + sorted((ROOT / 'docs').rglob('*.md'))
for path in markdown:
    body = path.read_text(encoding='utf-8')
    if len(re.findall(r'^```', body, re.M)) % 2:
        errors.append(f'unclosed code fence: {path.relative_to(ROOT)}')
    for target in re.findall(r'\[[^\]]*\]\(([^\s)]+)\)', body):
        if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', target) or target.startswith('#'):
            continue
        dest = (path.parent / unquote(target.split('#')[0])).resolve()
        if not dest.is_relative_to(ROOT) or not dest.exists():
            errors.append(f'broken local link: {path.relative_to(ROOT)} -> {target}')
try:
    data = json.loads((ROOT / 'tests/perft/cases.json').read_text())
    assert data['schema_version'] == 1
    assert len(data['cases']) == 5
    assert len({case['id'] for case in data['cases']}) == 5
    for case in data['cases']:
        fields = case['fen'].split()
        assert len(fields) == 6 and fields[1] in ('w', 'b')
        ranks = fields[0].split('/')
        assert len(ranks) == 8
        for rank in ranks:
            assert all(c in '12345678PNBRQKpnbrqk' for c in rank)
            assert sum(int(c) if c.isdigit() else 1 for c in rank) == 8
        assert case['pr_depth'] == 4
        assert set(case['nodes']) == {'1', '2', '3', '4', '5'}
        assert all(isinstance(n, int) and n > 0 for n in case['nodes'].values())
except (ValueError, KeyError, TypeError, AssertionError, OSError) as exc:
    errors.append(f'invalid perft fixture structure: {exc}')
if errors:
    raise SystemExit('\n'.join(errors))
print(f'PASS: {len(markdown)} Markdown files, local links and 5 perft fixture structures.')
print('Not run: chess rules, perft execution, firmware build or device acceptance.')
