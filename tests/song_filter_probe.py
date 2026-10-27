"""Run the supported DLL's song filter in an isolated Windows x64 process.

No imports or DLL entry point are executed. This is a developer measurement,
not a game launcher. The input file is never modified.
"""
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from profile_data import read_profile


def main():
    if sys.platform != "win32" or C.sizeof(C.c_void_p) != 8:
        raise RuntimeError("Windows x64 Python is required")
    data = Path(sys.argv[1]).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    profile = read_profile(digest)
    u16 = lambda p: struct.unpack_from("<H", data, p)[0]
    u32 = lambda p: struct.unpack_from("<I", data, p)[0]
    pe = u32(0x3C)
    optional = pe + 24
    preferred = struct.unpack_from("<Q", data, optional + 24)[0]
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.VirtualAlloc.argtypes = [C.c_void_p, C.c_size_t, C.c_ulong, C.c_ulong]
    kernel.VirtualAlloc.restype = C.c_void_p
    kernel.VirtualFree.argtypes = [C.c_void_p, C.c_size_t, C.c_ulong]
    kernel.VirtualFree.restype = C.c_int
    kernel.FlushInstructionCache.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
    kernel.FlushInstructionCache.restype = C.c_int
    image = kernel.VirtualAlloc(None, u32(optional + 56), 0x2000, 0x01)
    if not image:
        raise C.WinError(C.get_last_error())

    def commit(address, size, protection=0x04):
        if not kernel.VirtualAlloc(address, size, 0x1000, protection):
            raise C.WinError(C.get_last_error())

    def address(va):
        return image + va - preferred

    def cell(va, kind=C.c_uint32):
        return kind.from_address(address(va))

    try:
        commit(image, u32(optional + 60))
        C.memmove(image, data, u32(optional + 60))
        sections = optional + u16(pe + 20)
        for index in range(u16(pe + 6)):
            section = sections + index * 40
            size, offset = u32(section + 16), u32(section + 20)
            if size:
                destination = image + u32(section + 12)
                commit(destination, size, 0x40)
                C.memmove(destination, data[offset:offset + size], size)

        commit(address(profile['song_state']), 0x200)
        commit(address(profile['audio_main']), 8)
        # Reproduce the earlier fixed-divisor implementation for comparison. Store
        # the private 120.0 divisor in unused space in this isolated PE header.
        rate = image + 0x800
        C.c_float.from_address(rate).value = 120.0
        for name in ('filter_division0', 'filter_division1'):
            entry = profile[name]
            site = address(entry['address'])
            if C.string_at(site, 8) != entry['signature']:
                raise RuntimeError("Filter signature mismatch")
            displacement = struct.pack("<i", rate - (site + 8))
            C.memmove(site + 4, displacement, 4)
        if not kernel.FlushInstructionCache(C.c_void_p(-1), image, u32(optional + 56)):
            raise C.WinError(C.get_last_error())
        run_filter = C.WINFUNCTYPE(C.c_uint64, C.c_ubyte)(address(profile['song_filter']['address']))

        scenarios = {
            "steady_120": [1 / 120],
            "steady_119": [1 / 119],
            "steady_110": [1 / 110],
            "steady_95": [1 / 95],
            "alternating_6_10.667ms_mean_120": [0.006, 2 / 120 - 0.006],
            "one_25ms_frame_per_120_frames": [1 / 120] * 119 + [0.025],
        }
        results = []
        for name, intervals in scenarios.items():
            C.memset(address(profile['song_state']), 0, 0x200)
            elapsed = 100.0
            errors, raw_errors = [], []
            for frame in range(7200):
                elapsed += intervals[frame % len(intervals)]
                exact = elapsed * 300.0
                raw = math.floor(exact + 1e-7)
                cell(profile['audio_main']).value = raw
                run_filter(0)
                if cell(profile['song_raw'], C.c_uint64).value != raw:
                    raise RuntimeError("Native filter did not consume the supplied timestamp")
                if frame >= 2400:
                    filtered = cell(profile['song_output'], C.c_float).value
                    if not math.isfinite(filtered):
                        raise RuntimeError("Invalid native filter output")
                    errors.append((filtered - exact) / 0.3)
                    raw_errors.append((filtered - raw) / 0.3)
            results.append({
                "scenario": name,
                "actual_mean_fps": len(intervals) / sum(intervals),
                "filtered_minus_exact_ms_mean": sum(errors) / len(errors),
                "filtered_minus_exact_ms_min": min(errors),
                "filtered_minus_exact_ms_max": max(errors),
                "filtered_minus_raw_ms_mean": sum(raw_errors) / len(raw_errors),
            })
        if abs(results[0]["filtered_minus_raw_ms_mean"]) > 0.1:
            raise RuntimeError("Steady 120 FPS reference unexpectedly diverged")
        print(json.dumps({"game_sha256": digest, "filter_divisor": 120,
                          "results": results}, indent=2))
    finally:
        kernel.VirtualFree(image, 0, 0x8000)


if __name__ == "__main__":
    main()
