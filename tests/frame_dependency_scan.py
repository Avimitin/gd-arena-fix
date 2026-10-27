"""Read-only candidate inventory for frame dependencies in the supported image.

Requires pefile and capstone. This is a heuristic audit aid, not proof that a
counter represents time. Validate candidates and indirect callers in IDA.
No game code is executed and no binary bytes are included in the JSON output.
"""
import argparse
import bisect
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import re
import struct

import capstone
import pefile
from profile_data import read_profile


def scan(path, ida_functions=None):
    raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    profile = read_profile(digest)
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    image = text.get_data()
    text_start = base + text.VirtualAddress
    # Exception directory gives reproducible function ranges, including funclets.
    # Optional IDA spans below supplement code absent from unwind metadata.
    ranges = sorted({(base + e.struct.BeginAddress, base + e.struct.EndAddress)
                     for e in pe.DIRECTORY_ENTRY_EXCEPTION
                     if text_start <= base + e.struct.BeginAddress < text_start + len(image)})
    unwind_count = len(ranges)
    leaf_ranges = []
    excluded_leaf_ranges = 0
    if ida_functions:
        exported = json.loads(ida_functions.read_text(encoding="utf-8-sig"))
        if exported["sha256"] != digest:
            raise ValueError("IDA function inventory belongs to a different game image")
        unwind_starts = [a for a, _ in ranges]
        previous_end = 0
        for function in sorted(exported["functions"], key=lambda f: int(f["addr"], 16)):
            start = int(function["addr"], 16)
            end = start + int(function["size"], 16)
            i = bisect.bisect_right(unwind_starts, start) - 1
            if i >= 0 and start < ranges[i][1]:
                continue
            if not text_start <= start < end <= text_start + len(image):
                continue
            # Sizes are not a substitute for IDA chunk metadata. Accept only
            # nonoverlapping spans wholly inside a gap in the unwind inventory.
            if start < previous_end or (i + 1 < len(ranges) and end > ranges[i + 1][0]):
                excluded_leaf_ranges += 1
                continue
            leaf_ranges.append((start, end))
            previous_end = end
        ranges = sorted(ranges + leaf_ranges)
    starts = [a for a, _ in ranges]
    def owner(addr):
        i = bisect.bisect_right(starts, addr) - 1
        return starts[i] if i >= 0 and addr < ranges[i][1] else None
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    functions = {}
    candidates = []
    instructions = 0
    undecoded_spans = []
    temporal = {5, 8, 10, 15, 16, 20, 30, 60, 80, 120, 180, 240, 300, 600, 1200, 1800, 3600}
    # Enumerate every exact occurrence, then retain only actual RIP-relative refs.
    constants = {}
    for section in pe.sections:
        if section.Characteristics & 0x20000000:
            continue
        data = section.get_data()
        for label, fmt, value in [("float_1_over_60", "f", 1/60), ("float_afp_step", "f", 0.01667666807770729),
                                  ("double_1_over_60", "d", 1/60), ("float_60", "f", 60),
                                  ("double_60", "d", 60), ("float_ms_60", "f", 1000/60),
                                  ("double_ms_60", "d", 1000/60),
                                  ("float_300", "f", 300), ("double_300", "d", 300)]:
            pattern = struct.pack("<" + fmt, value)
            at = data.find(pattern)
            while at >= 0:
                constants[base + section.VirtualAddress + at] = label
                at = data.find(pattern, at + 1)
    constant_refs = []
    for start, end in ranges:
        decoded = list(md.disasm_lite(image[start-text_start:end-text_start], start))
        instructions += len(decoded)
        decoded_end = decoded[-1][0] + decoded[-1][1] if decoded else start
        if decoded_end < end:
            undecoded_spans.append({"start": hex(decoded_end), "end": hex(end)})
        calls = set()
        indirect = 0
        reasons = []
        comparisons = []
        for i, (addr, size, mnemonic, operands) in enumerate(decoded):
            # Conditional branches can cross MSVC chained unwind ranges too.
            # These are reachability edges, not necessarily function calls.
            if mnemonic == "call" or mnemonic.startswith("j") or mnemonic.startswith("loop"):
                if operands.startswith("0x"):
                    target = int(operands, 16)
                    resolved = owner(target)
                    if resolved and resolved != start:
                        calls.add(resolved)
                elif mnemonic == "call":
                    indirect += 1
            rip = re.search(r"\[rip ([+-]) (0x[0-9a-f]+)\]", operands)
            if rip:
                target = addr + size + (1 if rip[1] == "+" else -1) * int(rip[2], 16)
                if target in constants:
                    constant_refs.append({"address": hex(addr), "function": hex(start),
                                          "constant": hex(target), "kind": constants[target],
                                          "instruction": mnemonic + " " + operands})
            if mnemonic == "cmp":
                imm = operands.rsplit(", ", 1)[-1]
                try:
                    number = int(imm, 0)
                except ValueError:
                    number = None
                if number in temporal:
                    comparisons.append({"address": hex(addr), "value": number})
            is_memory = "[" in operands.split(",")[0]
            stack = "[rsp" in operands or "[rbp" in operands
            if mnemonic in ("inc", "dec") and is_memory and not stack:
                reasons.append({"address": hex(addr), "kind": "memory_unit_step", "instruction": mnemonic + " " + operands})
            elif mnemonic in ("inc", "dec", "add", "sub") and not is_memory:
                register = operands.split(",")[0]
                if mnemonic in ("add", "sub") and operands.rsplit(", ", 1)[-1] not in ("1", "5"):
                    continue
                # A local register loop increment is not sufficient. Look for
                # a nearby store of that register to persistent-looking memory.
                # This is not data-flow proof: intervening writes, aliasing and
                # conditional control flow still need manual inspection.
                for a2, _, m2, o2 in decoded[i+1:i+5]:
                    if m2 in ("call", "ret", "jmp"):
                        break
                    if m2 == "mov" and "[" in o2.split(",")[0] and o2.endswith(", " + register) and "[rsp" not in o2 and "[rbp" not in o2:
                        reasons.append({"address": hex(addr), "kind": "unit_step_then_store", "store": hex(a2),
                                        "instruction": mnemonic + " " + operands, "store_instruction": o2})
                        break
            elif mnemonic in ("add", "sub") and is_memory and not stack and operands.rsplit(", ", 1)[-1] in ("1", "5"):
                reasons.append({"address": hex(addr), "kind": "memory_constant_step", "instruction": mnemonic + " " + operands})
            elif mnemonic == "lea" and re.fullmatch(r"\w+, \[\w+ [+-] (?:1|5)\]", operands):
                register = operands.split(",")[0]
                for a2, _, m2, o2 in decoded[i+1:i+6]:
                    if m2 in ("call", "ret", "jmp"):
                        break
                    if m2 == "mov" and "[" in o2.split(",")[0] and o2.endswith(", " + register) and "[rsp" not in o2 and "[rbp" not in o2:
                        reasons.append({"address": hex(addr), "kind": "lea_step_then_store", "store": hex(a2),
                                        "instruction": mnemonic + " " + operands, "store_instruction": o2})
                        break
            elif mnemonic in ("addss", "subss", "mulss", "divss", "vaddss", "vsubss", "vmulss", "vdivss"):
                register = operands.split(",")[0]
                for a2, _, m2, o2 in decoded[i+1:i+6]:
                    if m2 in ("call", "ret", "jmp"):
                        break
                    if m2 in ("movss", "vmovss") and "[" in o2.split(",")[0] and o2.endswith(", " + register) and "[rsp" not in o2 and "[rbp" not in o2:
                        reasons.append({"address": hex(addr), "kind": "float_update_then_store", "store": hex(a2),
                                        "instruction": mnemonic + " " + operands, "store_instruction": o2})
                        break
        # MSVC chained unwind records split a single function into adjacent
        # ranges. Include fallthrough edges or reachability stops at a prolog.
        if decoded and decoded[-1][2] not in ("ret", "retf", "jmp", "int3"):
            following = owner(end)
            if following and following != start:
                calls.add(following)
        functions[start] = {"calls": calls, "indirect_calls": indirect}
        if reasons:
            candidates.append({"function": hex(start), "size": end-start, "steps": reasons, "comparisons": comparisons,
                               "indirect_calls": indirect})
    # These tables/records were traced from lifecycle initialization in IDA.
    # Record layout: ID, init, update, draw, exit, payload. Never reinterpret
    # arbitrary data xrefs (notably EH scope tables) as callback pointers.
    records = set(profile['audit_scene_records'])
    for table in profile['audit_scene_factories']:
        for index in range(4096):
            factory = struct.unpack('<Q', pe.get_data(table-base+index*8, 8))[0]
            if not factory:
                break
            code = pe.get_data(factory-base, 8)
            # Verified descriptor getters: LEA RAX,[RIP+disp32]; RET.
            if code[:3] != b'\x48\x8d\x05' or code[7] != 0xc3:
                raise ValueError(f'unresolved scene factory {factory:#x}')
            records.add(factory+7+struct.unpack('<i', code[3:7])[0])
        else:
            raise ValueError('unterminated scene factory table')
    for table in profile['audit_scene_tables']:
        for index in range(4096):
            record = struct.unpack('<Q', pe.get_data(table-base+index*8, 8))[0]
            if not record:
                break
            records.add(record)
        else:
            raise ValueError('unterminated scene callback table')
    callbacks = set()
    for record in records:
        for callback in struct.unpack('<4Q', pe.get_data(record-base+8, 32)):
            if callback:
                if not text_start <= callback < text_start+len(image):
                    raise ValueError('scene callback outside .text')
                callbacks.add(owner(callback) or callback)
    seeds = {"normal_frame": [profile['frame']['address']], "song_scene": [profile['audit_song_scene']],
             "drum_worker": [profile['audit_drum_worker']], "scene_callbacks": sorted(callbacks)}
    reach = {}
    for name, seed in seeds.items():
        seen, queue = set(), deque(seed)
        while queue:
            address = queue.popleft()
            if address in seen:
                continue
            seen.add(address)
            queue.extend(functions.get(address, {}).get("calls", ()))
        reach[name] = seen
    for candidate in candidates:
        address = int(candidate["function"], 16)
        candidate["direct_reachability"] = [name for name, seen in reach.items() if address in seen]
    for reference in constant_refs:
        address = int(reference["function"], 16)
        reference["direct_reachability"] = [name for name, seen in reach.items() if address in seen]
    return {"sha256": digest, "scope": "all .text exception-directory ranges and supplied nonoverlapping IDA leaf spans; indirect calls and undiscovered code unresolved",
            "unwind_ranges": unwind_count, "ida_leaf_ranges": len(leaf_ranges), "excluded_leaf_ranges": excluded_leaf_ranges,
            "decoded_instructions": instructions, "candidate_ranges": len(candidates),
            "undecoded_bytes": sum(int(s["end"], 16) - int(s["start"], 16) for s in undecoded_spans),
            "undecoded_spans": undecoded_spans,
            "step_sites": sum(len(c["steps"]) for c in candidates),
            "direct_reachable_ranges": {k: len(v) for k, v in reach.items()},
            "resolved_scene_records": [hex(a) for a in sorted(records)],
            "resolved_scene_callbacks": [hex(a) for a in sorted(callbacks)],
            "candidate_reachability": dict(Counter(n for c in candidates for n in c["direct_reachability"])),
            "constants": constant_refs, "candidates": candidates}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--patch-plan", type=Path,
                        help="JSON from gd-arena-fix-test --dump-timing-sites; annotates physical patch overlap only")
    parser.add_argument("--ida-functions", type=Path,
                        help="Hash-bound IDA function inventory; adds nonoverlapping code spans absent from PE unwind metadata")
    args = parser.parse_args()
    result = scan(args.image, args.ida_functions)
    if args.patch_plan:
        plan = json.loads(args.patch_plan.read_text(encoding="utf-8-sig"))
        if plan["sha256"] != result["sha256"]:
            raise ValueError("patch plan belongs to a different game image")
        sites = [(int(s["address"], 16), int(s["address"], 16) + s["size"], s["kind"])
                 for s in plan["sites"]]
        for candidate in result["candidates"]:
            for step in candidate["steps"]:
                address = int(step["address"], 16)
                step["local_patch_overlap"] = [hex(lo) for lo, hi, _ in sites if lo <= address < hi]
        result["patch_plan_note"] = ("Local instruction overlap only, not semantic coverage. Function detours, "
                                     "branch bypasses and shared producers need manual attribution. A global "
                                     "address merely appearing in the profile is not evidence of conversion.")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in result.items() if k not in
                      ("constants", "candidates", "resolved_scene_records", "resolved_scene_callbacks", "undecoded_spans")}, indent=2))
    print(f"Resolved {len(result['resolved_scene_records'])} scene records and "
          f"{len(result['resolved_scene_callbacks'])} callback entry points")


if __name__ == "__main__":
    main()
