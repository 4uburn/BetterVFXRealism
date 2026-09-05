"""Read-only checks for the authored VFX contract; Python standard library only."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def emitters(text):
    """Yield named emitter blocks, counting braces outside quoted resource names."""
    for match in re.finditer(r'(?m)^  EmitterDef (\S+) \{', text):
        depth = 1
        end = match.end()
        for token in re.finditer(r'"[^"\n]*"|[{}]', text[match.end():]):
            if token.group() == '{':
                depth += 1
            elif token.group() == '}':
                depth -= 1
            if depth == 0:
                end = match.end() + token.end()
                break
        if depth:
            raise ValueError(f'Unclosed emitter {match.group(1)}')
        yield match.group(1), text[match.start():end], match.start(), end


def value(block, key, default=None):
    match = re.search(r'(?m)^   ' + re.escape(key) + r' (.+)$', block)
    return match.group(1).strip() if match else default


def main():
    errors = []
    contract = json.loads((ROOT / 'docs/vfx-contract.json.txt').read_text(encoding="utf-8"))
    resources = {}
    for meta in ROOT.rglob('*.meta'):
        match = re.search(r'Name "\{([A-Fa-f0-9]{16})\}([^"\n]+)"', meta.read_text(encoding="utf-8"))
        if not match:
            continue
        guid, path = match.groups()
        if guid in resources and resources[guid] != path:
            errors.append(f'Duplicate GUID {guid}: {path}')
        resources[guid] = path
        if not (ROOT / path).is_file():
            errors.append(f'Missing asset for {meta.relative_to(ROOT)}')
    total = 0
    for ptc in ROOT.rglob('*.ptc'):
        text = ptc.read_text(encoding="utf-8")
        # Ignore braces embedded in resource GUID strings.
        plain = re.sub(r'"[^"\n]*"', '', text)
        if plain.count('{') != plain.count('}'):
            errors.append(f'Unbalanced asset {ptc.relative_to(ROOT)}')
        for name, block, _, _ in emitters(text):
            total += 1
            if name.startswith('ber_dust_'):
                if value(block, 'ParticleType') == 'Prefab':
                    errors.append(f'Dust classification includes prefab: {ptc.name}/{name}')
                if value(block, 'LocalTransform', '0') != '0':
                    errors.append(f'Dust must simulate in world space: {ptc.name}/{name}')
                if float(value(block, 'AirResistance', '0')) <= 0:
                    errors.append(f'Dust wind needs drag: {ptc.name}/{name}')
            for key in ('BirthRate', 'BirthRateRND', 'Lifetime', 'LifetimeRND', 'SizeMultiplier', 'SizeRND'):
                if float(value(block, key, '0')) < 0:
                    errors.append(f'Negative {key}: {ptc.name}/{name}')
    tuning = (ROOT / 'Scripts/Game/BER_EffectTuningComponent.c').read_text(encoding="utf-8")
    for material, expected in contract['fragment_wisps'].items():
        path = f'Particles/BER/BER_FragHit_{material}.ptc'
        blocks = list(emitters((ROOT / path).read_text(encoding="utf-8")))
        if len(blocks) != 1 or blocks[0][0] != 'ber_dust_whisp':
            errors.append(f'Fragment must contain only the primary wisp: {path}')
            continue
        block = blocks[0][1]
        for key in ('SizeMultiplier', 'SizeRND'):
            actual = float(value(block, key, '0'))
            if abs(actual - expected[key] * 0.33) > 0.000001:
                errors.append(f'Fragment is not 33% of vanilla {key}: {path}')
        if path not in tuning:
            errors.append(f'Fragment lookup missing: {path}')
    for path, expected in contract['dust_colours'].items():
        for name, block, _, _ in emitters((ROOT / path).read_text(encoding="utf-8")):
            if not name.startswith('ber_dust_'):
                continue
            match = re.search(r'(?s)\n   Color \{\s*(.*?)\s*\}', block)
            colours = [float(v) for v in match.group(1).split()] if match else []
            target = [min(1, x * 1.33) for x in expected]
            if not colours or len(colours) % 4:
                errors.append(f'Invalid colour curve: {path}/{name}')
            elif any(abs(colours[i + 1 + c] - target[c]) > 0.000001
                     for i in range(0, len(colours), 4) for c in range(3)):
                errors.append(f'Colour is not reference RGB x 1.33: {path}/{name}')
    for path in list(ROOT.rglob('*.c')) + list(ROOT.rglob('*.et')) + list(ROOT.rglob('*.ptc')):
        if 'WorkbenchGame' in path.parts:
            continue
        for guid, ref_path in re.findall(r'\{([A-Fa-f0-9]{16})\}([^"\s]+)', path.read_text(encoding="utf-8")):
            if guid in resources and resources[guid].lower() != ref_path.lower():
                errors.append(f'GUID/path mismatch in {path.name}: {guid}/{ref_path}')
            if ref_path.startswith(('Particles/BER/', 'Prefabs/BER/')) and not (ROOT / ref_path).is_file():
                errors.append(f'Missing local reference: {path.name} -> {ref_path}')
    assert not errors, '\n'.join(errors)
    print(f'PASS: {total} emitters; {len(resources)} resource GUIDs; '
          f'{len(contract["fragment_wisps"])} exact 33% wisps; '
          f'{len(contract["dust_colours"])} surface-palette assets.')
    print('This verifies data contracts, not compilation, rendered appearance or multiplayer.')


if __name__ == '__main__':
    main()
