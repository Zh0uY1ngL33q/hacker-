#!/usr/bin/env python3
"""PE metadata report for the migration baseline.

Emits JSON so every number in the written reports is traceable to a command
rather than copied by hand. Run it twice: once on the on-disk (unpacked) file and
once on the ModuleDump memory image, because the two layouts differ and mixing
them is the classic source of "offset X used to work" bugs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys

import pefile

IMAGE_SCN_MEM_EXECUTE = 0x20000000


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def dir_name(pe, idx):
    names = {
        0: "EXPORT", 1: "IMPORT", 2: "RESOURCE", 3: "EXCEPTION", 4: "SECURITY",
        5: "BASERELOC", 6: "DEBUG", 7: "ARCHITECTURE", 8: "GLOBALPTR", 9: "TLS",
        10: "LOAD_CONFIG", 11: "BOUND_IMPORT", 12: "IAT", 13: "DELAY_IMPORT",
        14: "CLR_RUNTIME",
    }
    d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[idx]
    return {"name": names.get(idx, str(idx)), "rva": hex(d.VirtualAddress), "size": hex(d.Size)}


def sections(pe, image: bool):
    out = []
    for s in pe.sections:
        name = s.Name.rstrip(b"\x00").decode("latin1")
        chars = s.Characteristics
        out.append({
            "index": out.__len__(),
            "name": name,
            "rva": hex(s.VirtualAddress),
            "virtualSize": hex(s.Misc_VirtualSize),
            "rawSize": hex(s.SizeOfRawData),
            "rawPointer": hex(s.PointerToRawData),
            "characteristics": hex(chars),
            "exec": bool(chars & IMAGE_SCN_MEM_EXECUTE),
            "read": bool(chars & 0x40000000),
            "write": bool(chars & 0x80000000),
            "entropy": round(s.get_entropy(), 4),
        })
    return out


def analyse(path, image_mode):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DEBUG"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_BASERELOC"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_TLS"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG"],
    ])

    imports = []
    for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []) or []:
        dll = entry.dll.decode("latin1")
        funcs = [imp.name.decode("latin1") if imp.name else f"#{imp.ordinal}" for imp in entry.imports]
        imports.append({"dll": dll, "count": len(funcs), "functions": funcs})

    exports = []
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") and pe.DIRECTORY_ENTRY_EXPORT:
        for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            exports.append({
                "ordinal": sym.ordinal,
                "name": sym.name.decode("latin1") if sym.name else None,
                "rva": hex(sym.address),
            })

    debug = []
    for d in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []) or []:
        try:
            entry = {"type": d.struct.Type, "size": d.struct.SizeOfData, "rva": hex(d.struct.AddressOfRawData)}
            if d.entry is not None:
                entry["pdb"] = getattr(d.entry, "PdbFileName", b"").decode("latin1", "replace")
                entry["guid"] = str(getattr(d.entry, "PdbSig70", ""))
                entry["age"] = getattr(d.entry, "PdbAge", None)
            debug.append(entry)
        except Exception as e:  # pragma: no cover - diagnostic only
            debug.append({"error": str(e)})

    relocs = 0
    if hasattr(pe, "DIRECTORY_ENTRY_BASERELOC") and pe.DIRECTORY_ENTRY_BASERELOC:
        for block in pe.DIRECTORY_ENTRY_BASERELOC:
            relocs += len(block.entries)

    tls = []
    if hasattr(pe, "DIRECTORY_ENTRY_TLS") and pe.DIRECTORY_ENTRY_TLS:
        t = pe.DIRECTORY_ENTRY_TLS.struct
        tls = [{"callbacks": hex(t.AddressOfCallBacks), "index": hex(t.AddressOfIndex)}]

    rich = None
    try:
        rich = pe.parse_rich_header()
        if rich:
            rich = {"key": hex(rich.get("key", 0)),
                    "entries": [{"id": hex(e[0]), "count": e[1]} for e in rich.get("values", [])][:40]}
    except Exception:
        rich = None

    result = {
        "file": path,
        "mode": "memory-image" if image_mode else "on-disk",
        "sha256": sha256(path),
        "size": len(pe.__data__) if hasattr(pe, "__data__") else None,
        "machine": hex(pe.FILE_HEADER.Machine),
        "machineName": "AMD64" if pe.FILE_HEADER.Machine == 0x8664 else hex(pe.FILE_HEADER.Machine),
        "peFormat": "PE32+" if pe.OPTIONAL_HEADER.Magic == 0x20B else "PE32",
        "characteristics": hex(pe.FILE_HEADER.Characteristics),
        "isDll": bool(pe.FILE_HEADER.Characteristics & 0x2000),
        "isExe": bool(pe.FILE_HEADER.Characteristics & 0x0002),
        "timeDateStamp": hex(pe.FILE_HEADER.TimeDateStamp),
        "imageBase": hex(pe.OPTIONAL_HEADER.ImageBase),
        "entryPoint": hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint),
        "sizeOfImage": hex(pe.OPTIONAL_HEADER.SizeOfImage),
        "sizeOfHeaders": hex(pe.OPTIONAL_HEADER.SizeOfHeaders),
        "subsystem": hex(pe.OPTIONAL_HEADER.Subsystem),
        "dllCharacteristics": hex(pe.OPTIONAL_HEADER.DllCharacteristics),
        "checksum": hex(pe.OPTIONAL_HEADER.CheckSum),
        "sectionAlignment": hex(pe.OPTIONAL_HEADER.SectionAlignment),
        "fileAlignment": hex(pe.OPTIONAL_HEADER.FileAlignment),
        "numberOfSections": pe.FILE_HEADER.NumberOfSections,
        "sections": sections(pe, image_mode),
        "dataDirectories": [dir_name(pe, i) for i in range(16)],
        "imports": imports,
        "exports": exports,
        "debug": debug,
        "relocationCount": relocs,
        "tls": tls,
        "richHeader": rich,
    }

    # Compiler fingerprint: the import table and the CRT signatures are the only
    # trustworthy evidence here. Guessing "MSVC 19.x" from code style is not.
    crt = [d for d in imports if d["dll"].lower().startswith(("vcruntime", "msvcp", "ucrtbase"))]
    result["crtImports"] = [{"dll": d["dll"], "count": d["count"]} for d in crt]
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--image", action="store_true", help="input is an RVA-indexed memory image")
    ap.add_argument("--json", required=True)
    a = ap.parse_args()
    data = analyse(a.path, a.image)
    with open(a.json, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    print(json.dumps({k: v for k, v in data.items()
                      if k not in ("sections", "imports", "dataDirectories", "exports")}, indent=1))
    print("\nSECTIONS")
    for s in data["sections"]:
        print(f"  [{s['index']:2}] {s['name']!r:12} rva={s['rva']:>10} vs={s['virtualSize']:>10} "
              f"raw={s['rawSize']:>10} ent={s['entropy']:<7} exec={int(s['exec'])} r={int(s['read'])} w={int(s['write'])}")
    print("\nIMPORTS")
    for d in data["imports"]:
        print(f"  {d['dll']:45} {d['count']}")


if __name__ == "__main__":
    main()
