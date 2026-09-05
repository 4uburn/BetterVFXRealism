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
            for curve in ('Size', 'Alpha', 'Color'):
                match = re.search(r'\n   ' + curve + r' \{\s*([^{}]+)\s*\}', block)
                if match and any(not 0 <= float(v) <= 1 for v in match.group(1).split()):
                    errors.append(f'Curve outside native 0-1 range: {ptc.name}/{name}/{curve}')
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
    # Independent authored budgets: runtime tuning may reduce these, never enlarge them.
    for asset in ('BER_Impact_DirtChunks', 'BER_Impact_RockChips'):
        solids = [b for n, b, _, _ in emitters((ROOT / f'Particles/BER/{asset}.ptc').read_text(encoding='utf-8'))
                  if not n.startswith('ber_dust_')]
        if sum(float(value(b, 'MaxNum', '0')) for b in solids) > 48:
            errors.append(f'Solid debris budget exceeds 48: {asset}')
        for b in solids:
            if value(b, 'GravityMultiply') != '1' or value(b, 'GravityMultiplyRND') != '0':
                errors.append(f'Debris needs consistent gravity: {asset}')
            if value(b, 'WindInfluence', '0') != '0' or value(b, 'EnableCollisions') != '1':
                errors.append(f'Debris must collide without wind advection: {asset}')
    smoke = list(emitters((ROOT / 'Particles/Weapon/Smoke_M242.ptc').read_text(encoding='utf-8')))
    if sum(float(value(b, 'MaxNum', '0')) for _, b, _, _ in smoke) > 11:
        errors.append('M242 smoke budget exceeds 11')
    if len([n for n, _, _, _ in smoke if n.startswith('noscope_')]) != 3:
        errors.append('M242 must retain three scope-suppressed close smoke emitters')
    vapor = list(emitters((ROOT / 'Particles/BER/BER_BlastCondensation.ptc').read_text(encoding='utf-8')))
    for _, b, _, _ in vapor:
        if float(value(b, 'Lifetime')) + float(value(b, 'LifetimeRND')) > 0.4:
            errors.append('Condensation must disappear within 0.4 seconds')
    for warhead in ('Warhead_Shell_HE_M821', 'Warhead_Shell_HE_O832DU', 'Warhead_Mine_M15AT', 'Warhead_Mine_TM62M'):
        prefab = (ROOT / f'Prefabs/Weapons/Warheads/{warhead}.et').read_text(encoding='utf-8')
        component = re.search(r'BER_EffectTuningComponent[^\n]*\{([^}]+)', prefab)
        setting = re.search(r'm_fCondensationStrength\s+([\d.]+)', component.group(1)) if component else None
        if not setting or not 0 < float(setting.group(1)) <= 1:
            errors.append(f'Large-blast condensation is not wired: {warhead}')
    for ptc in (ROOT / 'Particles/Enviroment').glob('Hit_*_enter_01.ptc'):
        for name, b, _, _ in emitters(ptc.read_text(encoding='utf-8')):
            if name == 'ber_dust_fines' and float(value(b, 'MaxNum')) > 3:
                errors.append(f'Wall-fines budget exceeds three: {ptc.name}')
    for name, b, _, _ in emitters((ROOT / 'Particles/BER/BER_APDS_Spark.ptc').read_text(encoding='utf-8')):
        if name.startswith('sparks_'):
            if value(b, 'GravityMultiply') != '1' or value(b, 'WindInfluence') != '0':
                errors.append(f'AP sparks must fall independently of smoke: {name}')
        if name == 'Flash' and float(value(b, 'Lifetime')) + float(value(b, 'LifetimeRND')) > 0.1:
            errors.append('AP flash exceeds 0.1 seconds')
    for name, b, _, _ in emitters((ROOT / 'Particles/Weapon/Explosion_HEI.ptc').read_text(encoding='utf-8')):
        if name in ('Explosion_Main', 'Explosion_Halo'):
            if value(b, 'LifetimeByAnim') != '0' or float(value(b, 'Lifetime')) + float(value(b, 'LifetimeRND')) > 0.25:
                errors.append(f'HE bright phase must have an explicit brief lifetime: {name}')
    # Follow-up: dispersed room haze and continuous, buoyant smoke sources.
    for asset in ('BER_RoomFog', 'BER_RoomFog_Wood'):
        blocks = list(emitters((ROOT / f'Particles/BER/{asset}.ptc').read_text(encoding='utf-8')))
        if sum(float(value(b, 'MaxNum', '0')) for _, b, _, _ in blocks) > 12:
            errors.append(f'Room haze exceeds 12 particles per layer: {asset}')
        for name, b, _, _ in blocks:
            if value(b, 'ShapeType') != 'Box' or min(map(float, value(b, 'ShapeSize').split())) <= 0:
                errors.append(f'Room haze needs spatial emission: {asset}/{name}')
            if not 0 < float(value(b, 'BirthRate')) <= 3 or float(value(b, 'EmittingTime')) < 3:
                errors.append(f'Room haze must emit gradually: {asset}/{name}')
            alpha = re.search(r'Alpha \{\s*([^}]+)', b)
            if max(list(map(float, alpha.group(1).split()))[1::2]) > 0.06:
                errors.append(f'Room haze opacity exceeds diffuse-layer budget: {asset}/{name}')
            if float(value(b, 'Lifetime')) + float(value(b, 'LifetimeRND')) > 40:
                errors.append(f'Room haze lifetime exceeds 40 seconds: {asset}/{name}')
    grenade_paths = list((ROOT / 'Particles/Weapon').glob('Smoke_grenade_*.ptc'))
    grenade_paths += list((ROOT / 'Particles/BER').glob('BER_SmokeIndoor_*.ptc'))
    for ptc in grenade_paths:
        indoor = ptc.name.startswith('BER_SmokeIndoor_')
        for name, b, _, _ in emitters(ptc.read_text(encoding='utf-8')):
            if not name.startswith('smoke_'):
                continue
            if value(b, 'EnableCollisions') != '1' or float(value(b, 'TagentialRestitution', '0')) < 0.9:
                errors.append(f'Smoke must retain motion along obstacles: {ptc.name}/{name}')
            if float(value(b, 'GravityMultiply', '0')) >= 0 or float(value(b, 'AirResistance', '0')) <= 0:
                errors.append(f'Smoke requires buoyancy and drag: {ptc.name}/{name}')
            if indoor and float(value(b, 'WindInfluence', '0')) != 0:
                errors.append(f'Indoor smoke uses outdoor wind: {ptc.name}/{name}')
            if 'initial' in name:
                continue
            curve = re.search(r'BRateMast \{\s*([^}]+)', b)
            pairs = list(map(float, curve.group(1).split())) if curve else []
            if len(pairs) < 4 or pairs[1] < 0.5 or pairs[2] > 0.01 or pairs[3] != 1:
                errors.append(f'Smoke sustained source ramps too slowly: {ptc.name}/{name}')
            if name == 'smoke_01' and float(value(b, 'Velocity', '0')) < 1:
                errors.append(f'Near-source smoke discharge is too weak: {ptc.name}')
    gas = list(emitters((ROOT / 'Particles/BER/BER_ActionGas.ptc').read_text(encoding='utf-8')))
    if len(gas) != 1 or gas[0][0] != 'noscope_action_gas':
        errors.append('Action gas must have one scope-suppressed emitter')
    for _, b, _, _ in gas:
        if float(value(b, 'MaxNum')) > 8 or float(value(b, 'EmittingTime')) > 0.15:
            errors.append('Action gas exceeds its per-shot particle or emission budget')
        if float(value(b, 'Lifetime')) + float(value(b, 'LifetimeRND')) > 1.2 + 1e-6:
            errors.append('Action gas exceeds authored lifetime (runtime indoor maximum is 3x)')
    assert not errors, '\n'.join(errors)
    print(f'PASS: {total} emitters; {len(resources)} resource GUIDs; '
          f'{len(contract["fragment_wisps"])} exact 33% wisps; '
          f'{len(contract["dust_colours"])} surface-palette assets.')
    print('This verifies data contracts, not compilation, rendered appearance or multiplayer.')


if __name__ == '__main__':
    main()
