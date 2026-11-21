"""Map candidate VR integration symbols without modifying the Android port.

The recompiled game uses ELF32 headers with AArch64 machine code. Symbol
values belong to its custom guest loader, not the Windows executable.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

TARGETS = {
    "_Z13R_RenderSceneP8refdef_tP12GfxViewParms": "scene submission",
    "_Z17CG_CalcViewValuesj": "camera calculation",
    "_Z18CG_DrawActiveFramejiiiii": "client frame",
    "_Z20CL_CreateNewCommandsj": "player input",
    "cgArray": "client state",
}


def inspect(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Expected little-endian ELF32 guest image")
    header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    if header[1] != 183:
        raise ValueError("Expected AArch64 machine type")
    section_offset, section_size, section_count = header[5], header[10], header[11]
    sections = [struct.unpack_from("<IIIIIIIIII", data, section_offset + i * section_size)
                for i in range(section_count)]
    found = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name_index, value, size, info, _, index = struct.unpack_from("<IIIBBH", data, offset)
            name = names[name_index:names.index(b"\0", name_index)].decode("utf-8")
            if name not in TARGETS or not 0 < index < len(sections):
                continue
            owner = sections[index]
            file_offset = owner[4] + value - owner[3]
            available = owner[1] != 8 and 0 <= file_offset < len(data)
            found[name] = {
                "purpose": TARGETS[name], "guest_virtual_address": hex(value),
                "size": size, "symbol_type": info & 15,
                "file_offset": hex(file_offset) if available else None,
                "first_16_bytes": data[file_offset:file_offset + 16].hex() if available else None,
            }
    return {
        "image": str(path), "sha256": hashlib.sha256(data).hexdigest(),
        "format": "ELF32 little-endian AArch64", "symbols": found,
        "missing": sorted(set(TARGETS) - set(found)),
        "status": "Static candidates only. Runtime addresses, ABI, and VR integration are unverified.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = inspect(args.image)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Mapped {len(result['symbols'])}/{len(TARGETS)} candidate symbols to {args.out}")
    if result["missing"]:
        raise SystemExit(1)
