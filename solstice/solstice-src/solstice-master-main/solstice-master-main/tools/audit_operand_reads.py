#!/usr/bin/env python3
"""Audit configured extraction positions against actual x64 instruction encodings.

Usage: python audit_operand_reads.py image.image verify.json --json operands.json
This does NOT infer class/function identity: compatible encoding is necessary,
not sufficient. It reads only small windows at already-reported match RVAs.
"""
import argparse
import json
import mmap
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP


def audit(data, entry, md):
    if entry['kind'] == 'sig' and entry['sig_type'] == 'Sig':
        return None
    widths = {'int': 4, 'uint8_t': 1, 'int8_t': 1, 'uint16_t': 2,
              'int16_t': 2, 'uint32_t': 4, 'int32_t': 4, 'uint64_t': 8, 'int64_t': 8}
    size = widths[entry.get('read_type', 'int')] if entry['kind'] == 'offset' else 4
    issues = []
    for text in entry.get('match_rvas', []):
        start = int(text, 16)
        position = start + entry['disp_off']
        instructions = list(md.disasm(data[start:start + 0x100], start))
        insn = next((i for i in instructions if i.address <= position < i.address+i.size), None)
        issue = None
        if insn is None:
            issue = 'no instruction boundary covers configured read'
        elif entry['kind'] == 'offset':
            if position != insn.address + insn.disp_offset or not insn.disp_size:
                issue = 'configured read is not the start of a memory displacement'
            elif size != insn.disp_size:
                issue = f'declared read width {size} differs from encoded disp{insn.disp_size*8}'
            elif any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in insn.operands):
                issue = 'RIP-relative address treated as a field/index'
            elif entry['offset_type'] == 'Index' and insn.disp % 8:
                issue = 'virtual byte displacement is not divisible by 8'
        else:
            immediate = (insn.mnemonic in ('call', 'jmp') and insn.imm_size == 4 and
                         position == insn.address + insn.imm_offset)
            rip = (insn.disp_size == 4 and position == insn.address + insn.disp_offset and
                   any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in insn.operands))
            if not (immediate or rip):
                issue = 'rel32 extraction is neither a call/jump immediate nor a RIP displacement'
        if issue:
            issues.append({'match_rva': text, 'issue': issue,
                           'instruction': f'{insn.mnemonic} {insn.op_str}' if insn else None})
    return {'name': entry['name'], 'kind': entry['kind'], 'issues': issues} if issues else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image')
    ap.add_argument('report')
    ap.add_argument('--json', required=True)
    args = ap.parse_args()
    entries = json.load(open(args.report, encoding='utf-8'))['results']
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    with open(args.image, 'rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        results = [a for e in entries if (a := audit(data, e, md))]
    with open(args.json, 'w', encoding='utf-8') as f:
        json.dump({'scope': 'encoding only; not semantic function identification', 'results': results}, f, indent=2)
    for e in results:
        print(e['kind'], e['name'], ':', e['issues'][0]['issue'], '/', e['issues'][0]['instruction'])
    print(f'{len(results)} definitions have operand-extraction issues')
    return 1 if results else 0


if __name__ == '__main__':
    raise SystemExit(main())
