#!/usr/bin/env python3
"""Collect complete static candidate evidence for Batch 2 ambiguous offsets.

The dump is a virtual image: file offset == RVA.  Function boundaries come
from the PE32+ exception directory (RUNTIME_FUNCTION entries), not guessed
prologues.  This tool does not select a winner or modify mappings.
"""
import argparse
import bisect
import json
import mmap
import struct
import sys
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86_const import X86_GRP_JUMP, X86_GRP_RET

import verify_sigs as scanner


TARGETS = [
    "GameSession_mEventCallback", "Actor_mDestroying",
    "ClientInstance_mStackScene", "ClientInstance_setDisableInput",
    "ClientInstance_grabMouse", "ClientInstance_releaseMouse",
    "Level_getHitResult", "BlockSource_setBlock",
    "ClientInstance_getBlockSource", "gDataUtil_MiscData_Instance",
    "LevelData_mTick",
]


def pe_image_base(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    return struct.unpack_from("<Q", data, pe + 24 + 24)[0]


def runtime_functions(data, size_of_image):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x20B:
        raise ValueError("expected PE32+")
    directory = opt + 112 + 3 * 8  # IMAGE_DIRECTORY_ENTRY_EXCEPTION
    rva, size = struct.unpack_from("<II", data, directory)
    end = min(rva + size, len(data), size_of_image)
    if rva == 0 or end < rva or (end - rva) < 12:
        return []
    funcs = []
    for off in range(rva, end - 11, 12):
        begin, finish, unwind = struct.unpack_from("<III", data, off)
        if 0 < begin < finish <= size_of_image:
            funcs.append((begin, finish, unwind))
    funcs.sort()
    return funcs


def containing_function(funcs, starts, rva):
    pos = bisect.bisect_right(starts, rva) - 1
    if pos >= 0 and funcs[pos][0] <= rva < funcs[pos][1]:
        return funcs[pos]
    return None


def section_for(img, rva):
    for sec in img.sections:
        size = min(sec["vsize"], max(0, img.size_of_image - sec["rva"]))
        if size and sec["rva"] <= rva < sec["rva"] + size:
            return {"index": sec["index"], "name": sec["name"],
                    "rva": "0x%X" % sec["rva"], "virtualSize": "0x%X" % sec["vsize"],
                    "characteristics": "0x%08X" % sec["characteristics"]}
    return None


def decode_function(md, data, function, hit):
    if not function:
        start, end = max(0, hit - 64), min(len(data), hit + 96)
    else:
        start, end = function[0], min(function[1], len(data))
    instructions = list(md.disasm(data[start:end], start))
    hit_index = next((i for i, ins in enumerate(instructions)
                      if ins.address <= hit < ins.address + ins.size), None)
    boundary = hit_index is not None and instructions[hit_index].address == hit
    window = []
    if hit_index is not None:
        lo, hi = max(0, hit_index - 8), min(len(instructions), hit_index + 12)
        window = instructions[lo:hi]

    leaders = {start}
    for ins in instructions:
        is_jump = ins.group(X86_GRP_JUMP)
        is_ret = ins.group(X86_GRP_RET)
        if is_jump and ins.operands and ins.operands[0].type == 2:  # X86_OP_IMM
            target = ins.operands[0].imm
            if start <= target < end:
                leaders.add(target)
        if is_jump or is_ret:
            nxt = ins.address + ins.size
            if start <= nxt < end:
                leaders.add(nxt)
    leaders = sorted(leaders)
    block = None
    if hit_index is not None:
        ins_addr = instructions[hit_index].address
        leader_pos = bisect.bisect_right(leaders, ins_addr) - 1
        block_start = leaders[max(0, leader_pos)]
        block_end = leaders[leader_pos + 1] if leader_pos + 1 < len(leaders) else end
        block_ins = [ins for ins in instructions if block_start <= ins.address < block_end]
        block = {
            "startRva": "0x%X" % block_start, "endRva": "0x%X" % block_end,
            "instructionCount": len(block_ins),
            "terminator": ("0x%X: %s %s" % (block_ins[-1].address,
                                             block_ins[-1].mnemonic,
                                             block_ins[-1].op_str)
                           if block_ins else None),
        }
    return boundary, ["0x%X: %-8s %s" % (i.address, i.mnemonic, i.op_str)
                      for i in window], block


def read_derived(data, hit, definition):
    pos = hit + definition["disp_off"]
    ctype = definition.get("read_type", "int")
    widths = {"uint8_t": (1, False), "int8_t": (1, True),
              "uint16_t": (2, False), "int16_t": (2, True),
              "uint32_t": (4, False), "int32_t": (4, True),
              "int": (4, True), "uint64_t": (8, False), "int64_t": (8, True)}
    width, signed = widths[ctype]
    raw = int.from_bytes(data[pos:pos + width], "little", signed=signed)
    aligned = definition["offset_type"] != "Index" or raw % 8 == 0
    value = ((-1 if raw < 0 else 1) * (abs(raw) // 8)
             if definition["offset_type"] == "Index" and aligned else raw)
    return {"operandRva": "0x%X" % pos, "readType": ctype, "width": width,
            "raw": raw, "aligned": aligned, "derivedValue": value}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", type=Path)
    ap.add_argument("--src", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    ap.add_argument(
        "--definitions-json", type=Path,
        help=("reuse the definition snapshot embedded in an earlier analyzer "
              "report (needed to reproduce pre-fix candidate sets)"),
    )
    args = ap.parse_args()

    if args.definitions_json:
        snapshot = json.loads(args.definitions_json.read_text("utf-8"))
        by_name = {
            item["name"]: item["definition"] for item in snapshot["results"]
            if item["name"] in TARGETS
        }
    else:
        definitions, _ = scanner.load_definitions(str(args.src))
        # grabMouse/releaseMouse also have DEFINE_SIG entries. Batch 2 explicitly
        # targets the OffsetProvider declarations, so never let the later signature
        # definition overwrite the offset definition in this lookup.
        by_name = {d["name"]: d for d in definitions
                   if d["kind"] == "offset" and d["name"] in TARGETS}
    missing_defs = [name for name in TARGETS if name not in by_name]
    if missing_defs:
        raise SystemExit("definitions missing: " + ", ".join(missing_defs))

    img = scanner.Image(str(args.image))
    try:
        data = img.data
        base = pe_image_base(data)
        funcs = runtime_functions(data, img.size_of_image)
        starts = [f[0] for f in funcs]
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        md.detail = True
        results = []
        for name in TARGETS:
            definition = by_name[name]
            pattern = scanner.parse_pattern(definition["pattern"])
            hits = scanner.scan_spans(data, img.exec_spans(), pattern, limit=1_000_000)
            candidates = []
            for hit in hits:
                function = containing_function(funcs, starts, hit)
                boundary, disassembly, block = decode_function(md, data, function, hit)
                lo, hi = max(0, hit - 32), min(len(data), hit + len(pattern) + 32)
                candidate = {
                    "rva": "0x%X" % hit, "va": "0x%X" % (base + hit),
                    "section": section_for(img, hit),
                    "contextStartRva": "0x%X" % lo,
                    "contextHex": bytes(data[lo:hi]).hex(" "),
                    "instructionBoundary": boundary,
                    "function": ({"entryRva": "0x%X" % function[0],
                                  "endRva": "0x%X" % function[1],
                                  "unwindRva": "0x%X" % function[2],
                                  "size": function[1] - function[0]} if function else None),
                    "basicBlock": block, "disassembly": disassembly,
                    "extraction": read_derived(data, hit, definition),
                }
                candidates.append(candidate)
            results.append({"name": name, "definition": definition,
                            "matchCount": len(hits), "candidates": candidates})
        report = {"schemaVersion": 1, "image": str(args.image.resolve()),
                  "imageBase": "0x%X" % base, "sizeOfImage": img.size_of_image,
                  "runtimeFunctionCount": len(funcs), "results": results}
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2), "utf-8")
        for item in results:
            print("%-52s %d" % (item["name"], item["matchCount"]))
    finally:
        img.data.close()


if __name__ == "__main__":
    main()
