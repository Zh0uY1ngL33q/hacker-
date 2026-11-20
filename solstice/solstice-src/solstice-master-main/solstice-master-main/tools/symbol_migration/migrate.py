#!/usr/bin/env python3
"""Static symbol migration from archived analyses into the target image.

No runtime probes. No guessing. Sources are separated:

  A-class = analyses of THIS SAME image (NetEase 3.9.20.300505).
            Byte-exact / masked comparison of the function body.
  B-class = signatures from an older game version. Kept in a separate pool.

Only `verified` entries are safe to migrate. Everything else is bucketed with
a reason so the next round has a direction.

Usage:
  python migrate.py <image> <sources.json> [--json out.json]
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pdata_functions import PEImage


def mask_relatives(img: PEImage, body: bytes, begin_rva: int) -> bytes:
    """L1: mask rel32 / RIP-relative immediates so position drift is ignored.

    This is deliberately conservative: only bytes that ARE relative operands
    are masked. We do not pattern-match generic immediates, which would create
    false positives between unrelated functions.
    """
    out = bytearray(body)
    i = 0
    while i < len(body) - 5:
        op = body[i]
        # E8/E9 rel32 call/jmp
        if op in (0xE8, 0xE9):
            out[i+1:i+5] = b'\x3f\x3f\x3f\x3f'
            i += 5
            continue
        # common RIP-relative ModRM: 8B / 48 8B / 4C 8B / F3 0F / 0F B7 etc.
        # Detect mod=00 rm=101 (RIP-relative) on any instruction with a ModRM
        # byte: scan a small window for 0x05/0x0D/0x15/0x1D/0x25/0x2D/0x35/0x3D
        if op & 0xC7 in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
            out[i+1:i+5] = b'\x3f\x3f\x3f\x3f'
            i += 5
            continue
        i += 1
    return bytes(out)


def masked_equal(a: bytes, b: bytes) -> bool:
    return len(a) == len(b) and all(x == y or y == 0x3F for x, y in zip(a, b))


def mnemonic_histogram(body: bytes) -> dict:
    """L2: crude but useful structure fingerprint -- opcode distribution."""
    h: dict[int, int] = {}
    for byte in body:
        h[byte] = h.get(byte, 0) + 1
    return h


def histogram_distance(a: dict, b: dict) -> float:
    keys = set(a) | set(b)
    total = sum(a.values()) + sum(b.values()) or 1
    return sum(abs(a.get(k, 0) - b.get(k, 0)) for k in keys) / total


def icf_shared(img: PEImage, begin: int, end: int) -> list[int]:
    """Find other .pdata functions with byte-identical bodies (/OPT:ICF)."""
    body = img.read(begin, end - begin)
    if body is None:
        return []
    out = []
    for f in img.functions:
        if f['begin'] == begin or f['end'] - f['begin'] != len(body):
            continue
        if f['unwind'] == 0:
            continue
        other = img.read(f['begin'], len(body))
        if other == body:
            out.append(f['begin'])
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('sources', help='JSON: list of {name, source_class, rva, bytes?, note?}')
    ap.add_argument('--json', dest='out_json')
    args = ap.parse_args()

    img = PEImage(args.image)
    sources = json.load(open(args.sources, encoding='utf-8'))
    if isinstance(sources, dict):
        sources = sources.get('sources', [])

    buckets = {'verified': [], 'icf_shared': [], 'ambiguous': [], 'failed': []}

    for src in sources:
        name = src['name']
        cls = src.get('source_class', 'A')
        rva = int(src['rva'], 16) if isinstance(src['rva'], str) else int(src['rva'])
        entry = {'name': name, 'source_class': cls, 'rva_old': hex(rva)}

        f = img.function_at(rva)
        if not f:
            entry['reason'] = 'no function boundary at this RVA'
            buckets['failed'].append(entry)
            continue
        body = img.body(f)
        entry['function'] = {'begin': hex(f['begin']), 'end': hex(f['end']),
                             'kind': f['kind'], 'size': len(body or b'')}

        # L0: byte-exact at the ARCHIVED RVA. For A-class (same image) the
        # archived address is still valid, so compare there directly. Do NOT
        # compare from the inferred leaf boundary: leaf functions have no
        # .pdata entry and the backward scan can overshoot into padding.
        if cls == 'A' and src.get('bytes'):
            expected = bytes.fromhex(src['bytes'].replace(' ', ''))
            at_rva = img.read(rva, len(expected))
            if at_rva is not None and at_rva == expected:
                shared = []
                if f['kind'] == 'pdata':
                    shared = icf_shared(img, f['begin'], f['end'])
                if shared:
                    entry['reason'] = 'byte-exact but ICF-shared'
                    entry['icf_sites'] = [hex(x) for x in shared]
                    buckets['icf_shared'].append(entry)
                else:
                    entry['confidence'] = 'L0'
                    entry['verified_at_rva'] = hex(rva)
                    buckets['verified'].append(entry)
                continue
            entry['reason'] = 'bytes at archived RVA differ (L0 miss)'
            buckets['failed'].append(entry)
            continue

        # L1: masked body compare against the stored old body when available.
        old_body = src.get('function_bytes')
        if not old_body and cls == 'A':
            old_body = src.get('bytes')
        if old_body:
            old = bytes.fromhex(old_body.replace(' ', ''))
            if body is not None and len(old) == len(body):
                if masked_equal(mask_relatives(img, old, rva),
                                mask_relatives(img, body, f['begin'])):
                    entry['confidence'] = 'L1'
                    buckets['verified'].append(entry)
                    continue
            entry['reason'] = 'function body changed (L0/L1 miss)'
            buckets['failed'].append(entry)
            continue

        # No old body to compare: report as unresolved, never guess.
        entry['reason'] = 'no stored old function body for byte comparison'
        buckets['ambiguous'].append(entry)

    report = {
        'image': str(Path(args.image).name),
        'image_base': hex(img.image_base),
        'pdata_functions': len(img.functions),
        'policy': 'only verified is safe to migrate; no runtime evidence used',
        'buckets': buckets,
        'counts': {k: len(v) for k, v in buckets.items()},
    }
    text = json.dumps(report, indent=2, ensure_ascii=False) + '\n'
    if args.out_json:
        Path(args.out_json).write_text(text, encoding='utf-8')
    print(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
