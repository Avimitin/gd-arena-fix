"""Read literal profile data for developer probes from the C++ source of truth.

Profiles deliberately use literal designated initializers so addresses and
instruction bytes remain readable next to their names in game_profile.h.
This reader rejects missing/unsupported profiles rather than guessing offsets.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read_profile(sha256):
    header = (ROOT / 'src/game_profile.h').read_text(encoding='utf-8')
    for body in re.findall(r'inline constexpr Profile \w+\s*\{(.*?)\n\};', header, re.S):
        digest = re.search(r'\.sha256\s*=\s*"([0-9a-f]{64})"', body)
        if not digest or digest[1] != sha256:
            continue
        result = {'sha256': sha256}
        for name, value in re.findall(r'\.(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*,', body):
            result[name] = int(value, 16)
        for name, values in re.findall(r'\.(\w+)\s*=\s*\{((?:\s*0x[0-9a-fA-F]+\s*,?)+)\}', body):
            result[name] = [int(value, 16) for value in re.findall(r'0x[0-9a-fA-F]+', values)]
        for name, value, signature in re.findall(r'\.(\w+)\s*=\s*\{(0x[0-9a-fA-F]+),\s*\{([^}]+)\}', body):
            result[name] = {'address': int(value, 16),
                            'signature': bytes(int(x.strip(), 0) for x in signature.split(',') if x.strip())}
        return result
    raise ValueError('unsupported game image: no matching central profile')


def check_address_location():
    address = re.compile(r'\b0x1[89a-fA-F][0-9a-fA-F]{7}(?:[uUlL]+)?\b')
    bad = []
    for directory in ('src', 'tests'):
        for path in (ROOT / directory).rglob('*'):
            if path.suffix not in ('.cpp', '.h', '.py') or path.name == 'game_profile.h':
                continue
            for line, text in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
                if address.search(text):
                    bad.append(f'{path.relative_to(ROOT)}:{line}')
    if bad:
        raise RuntimeError('game addresses must live in game_profile.h: ' + ', '.join(bad))
    print('PASS: game addresses are centralized in game_profile.h')


if __name__ == '__main__':
    check_address_location()
