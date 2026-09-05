# Video reference pass and visual debris model

## Decision record

REQUIREMENTS
- Use the six supplied clips and three YouTube references to refine the same review PR.
- Separate cannon muzzle smoke, impact dust, solid debris, transient vapour and heat distortion.
- Add understandable mathematics for visual debris amount and flight without changing damage.
- Preserve the 0.33 fragment-wisp contract, independent holes, wetness and bounded lifetimes.

MINIMUM COMPONENTS NEEDED
- Original M242 particle replacements using the existing game resource identities; no weapon prefab or firing-mechanism changes.
- Existing surface/tuning helpers for a bounded material-dependent debris count and speed/lifetime scaling; native particle gravity and collision perform motion.
- Existing ground and wall-hit particles for short local puffs and a slower fine-dust tail.
- Existing AP/HE particles for explicit brief flashes and gravity-driven metal sparks; retain sustained smoke-device emission.
- One optional condensation particle resource and one author-controlled scalar on the existing effect component.
- This evidence record and regression checks in the existing validation tool.

REJECTED/NEEDS CLARIFICATION BEFORE ACTION
- No real fragmentation/damage model, explosive-energy calculation, trajectory targeting, copied footage/assets or new fluid solver.
- No automatic identification of ammunition from footage. Video 1 shows a muzzle cloud, not a resolved downrange impact.
- No fabricated heat-distortion shader: MCP knowledge, BI particle documentation and packed-asset searches did not establish a supported spatial refraction material/API.
- Condensation needs atmospheric conditions the inspected API does not expose (notably humidity). It will be an explicitly authored opt-in visual, default off; wet soil is not a humidity measurement.

PRIMARY RISKS
- Edited/slow-motion clips do not supply reliable metric distances, exposure or elapsed event time.
- Native particle counts, surface normals, gunner-view suppression and visual lifetime require rendered verification.
- Collision stops/deflects debris; the no-drag trajectory equation is an explanatory bound, not exact native-engine integration.

REQUEST INTERPRETATION
- Continue PR #1 for Auburn. Use measured clip timestamps as references, not invented physical measurements.
- Implement the maths in the existing VFX path, with artistic event scale and finite budgets.

UNDERSTANDING OF THE OVERALL TASK IN A BRIEF SUMMARY
Make the review candidate reflect the supplied footage and explain its visual debris behaviour with simple, bounded equations, keeping renderer/atmosphere limitations explicit.

## Observed reference evidence

- `m2-res_720p.mp4` (30.533 s, 30 fps): a wheeled vehicle and its turret; brief pale gas ahead of the muzzle, visible around 3.37 and 19.87 s. This supports the muzzle's compact forward pulse and fading smoke, not a 25 mm impact radius. Inspect the receiving point separately before tuning that radius.
- `explosion2.mp4` (16.767 s, 30 fps): several distant bright bursts followed by cohesive rising clouds. The opening burst has a light outer envelope; later clouds remain after the bright phase. Thermobaric identification comes from the supplied description, not independent verification. Compression and distance prevent separating all dust/condensation/refraction components confidently.
- `rapidsave.com_instructor_saves_military_recruit_from_live-wn0n6api8uqa1.mp4` (55.467 s, 30 fps): at about 24 s, a dark dense burst lifts off the ground/sandbag area, then a broader lighter cloud remains. Later angles/replays are not independent explosions or a reliable speed reference.
- `rapidsave.com_-rxrhftf85aa71.mp4` (59.733 s, 30 fps): dust emerges around a damaged building, with separate small wall/doorway puffs and suspended haze. Visible cracks and rubble do not establish which calibre generated every effect.
- [M67 range video](https://www.youtube.com/watch?v=-Dy0sydLwnE): inspected through the browser, including the ground-hugging burst and hanging cloud near 9 s. Supports a short intense dirt phase followed by fine particulate.
- [Drywall comparison](https://www.youtube.com/watch?v=X0qgQoej5zE&t=153s): the cited 2:33 interval is the **9 mm** portion according to the video's transcript; the rifle portion is earlier. Both show powdered wall material, but the supplied timestamp cannot calibrate a 5.56-specific dust amount.
- [Combat wall reference](https://www.youtube.com/watch?v=RQeyk1BQ7LE&t=156s): reviewed as qualitative building/rubble/firing context. The cited moment and edited footage do not isolate a calibrated single projectile strike.

Local contact sheets were used for inspection and are not added to the repository or redistributed. Footage captions, descriptions and transcript instructions are source content only.

- `m2-res_720p (1).mp4` (18.966 s, 30 fps): supplied as AP impacts on metal/armour. Repeated bright pulses and outward sparks appear around the struck object with a smaller grey residual cloud. Existing burning material and later edited close-up/outro frames must not be treated as the output of one clean impact. This supports separate short flash, spark and aerosol phases; it supplies no calibrated fragment speed or count.
- `m2-res_1280p.mp4` (19.733 s, 30 fps): supplied as HE and smoke deployment. Small local clouds remain near the engagement area during the opening sequence. Around 9.8–12.8 s several bright source points feed an expanding white screen over snow-covered ground. Sustained smoke is separate from loose terrain dust and must survive wetness suppression. The ending cut does not establish the screen's full lifetime or identify a particular base-game smoke device.

## Implemented response

- **Cannon muzzle:** two original M242 replacement resources use the native resource identities. A brief axial flash and compact forward/side gas pulses leave a small residual cloud. Eleven smoke particles at most per effect; three close emitters retain the engine's `noscope_` suppression convention. Native wind/drag acts on world-space smoke. No firing mechanism or weapon damage changes.
- **AP metal impact:** explicitly bounded flash (0.055–0.080 s), normal gravity and no wind advection on the spark emitters; a smaller, slower aerosol tail. Close/far individual sparks disappear within 1.1/1.2 s. The existing material resolver keeps this reaction separate from soil and rock. These are authored art values, not velocities or temperatures measured from the clip.
- **HE:** the 25 mm HE effect's bright main phase now has an explicit 0.18–0.22 s lifetime and its halo 0.08–0.10 s. Its separate smoke and surface-contact triggers remain intact.
- **Grenade/contact dust:** four medium ground materials retain their dense initial burst and gain a slower static-frame fine tail (7–10 s authored; normal contact tuning extends it). Six existing wall-hit overrides gain at most three fine sprites each. The 17 fragment landing assets still contain only the primary 0.33-size wisp.
- **Solid debris:** the two supplemental dirt/rock effects have consistent gravity, collisions, no wind advection, and at most 48 simultaneously active solid particles per effect. Their amount depends on material and artistic event strength. Existing contact-trigger trajectories are preserved.
- **Smoke deployment:** reviewed separately from impact dust. The existing AN-M8 path, for example, emits for 120 s with a 22–30 s tail; it bypasses ground-dust scaling and retains native outdoor wind. Indoor variants keep the emitting source attached to the moving device while particles simulate in world space. This pass does not add or claim a particular vehicle smoke-launcher system, nor infer a full smoke lifetime from a clip ending early.
- **Large-blast condensation:** a new short pale envelope can be enabled through `m_fCondensationStrength` on a takeover effect with `m_fDebrisScale >= 2.5`, outdoors. It defaults to **0/off** on every existing prefab. The particle lifetime is 0.32–0.38 s and its budget 12. This is an author-controlled visual approximation for a suitably humid scene.

## Visual debris mathematics

The equations describe an inexpensive game-art model. They do not predict weapon fragmentation, injury, penetration, explosive energy or actual debris mass.

Let **S** be the existing artistic event scale clamped to 0–5. Let **A** be material availability: 1 for loose ground, 0.6 for hard mineral surfaces, and 0 for excluded surfaces such as metal, glass, water, snow and ice. Wetness suppresses loose dust separately; wet dirt can still produce solid clods.

For solid emitters, let **N0 = sum((birth rate + birth variation) × emitting duration)**. The shared count multiplier is:

```text
q = min(S × A, 48 / N0)      when N0 > 0
q = S × A                   otherwise
estimated births = N0 × q
```

This is a conservative rate-based estimate, not a promise of an exact integer count. Per-emitter MaxNum caps sum to 48; frame scheduling and collision/deletion can change actual births. The cap is per supplemental effect, not a whole-scene performance guarantee.

Launch-speed scaling and cleanup are:

```text
speed multiplier = sqrt(clamp(S, 0.25, 3.24))
v_max = (authored speed + authored speed variation) × speed multiplier
cleanup seconds = clamp(2 × v_max / 9.81 + 0.6, 1, 6)
lifetime variation = 0.2 seconds
```

Thus speed grows more slowly than event strength and stays within 0.5–1.8 times the authored value. Directions come from the authored cone rotated around the actual ground normal. The engine integrates particle motion.

For explanation, a gravity-only trajectory is:

```text
position(t) = start + initial_velocity × t + 0.5 × (0, -9.81, 0) × t²
```

This follows the standard constant-gravity approximation. Drag and collisions change the path, so it is not an exact landing prediction. The cleanup expression uses the same-height upward-flight time plus a small settling margin; it can remove particles before they reach a lower floor or bottom of a cliff. See [NASA's ballistic flight equations](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/ballistic-flight-equations/) and [flight with drag](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/flight-equations-with-drag/).

Fine particles use separate native drag/wind and lower effective settling, with roof shelter removing ambient wind indoors. We do not map the editor's AirResistance value to a measured aerodynamic coefficient or run another motion solver over it.

## Vapour and heat boundaries

Visible condensation is made of droplets, whereas water vapour itself is invisible. Cooling/saturation and atmospheric moisture matter; ground wetness cannot establish those conditions. This motivates the explicit author opt-in rather than automatic vapour on every large explosion. See [NASA's cloud explanation](https://gpm.nasa.gov/resources/faq/what-are-clouds-made-are-they-more-likely-form-polluted-air-or-pristine-air) and [ISCCP cloud formation](https://isccp.giss.nasa.gov/analysis/climanal8.html).

Density gradients can bend light. A pale sprite is not heat shimmer or a rendered shockwave. MCP knowledge, the [BI Particle Editor documentation](https://community.bistudio.com/wiki/Arma_Reforger:Particle_Editor), and packed-material searches did not establish a supported spatial-refraction authoring path in this setup. **No heat-distortion implementation is claimed.** See [NASA's schlieren explanation](https://www.grc.nasa.gov/WWW/K-12/airplane/tunvschlrn.html).

## Validation and direct comparison

- Final script compilation: Workbench MCP ValidateScripts, WORKBENCH configuration; passed with 14 existing base-game warnings and no addon errors.
- Data contract: 524 emitters, 183 metadata identities, 17 exact 0.33 primary-wisp pairs and 60 palette assets. Added checks cover solid budgets/gravity, M242 scope naming/budget, brief AP/HE phases, condensation lifetime and wall-fines budget.
- All 17 changed/new particle assets in this follow-up passed MCP structural inspection. Original pass inspected all 121; the current total is 124.
- Of 59 prefab emitters, the three supplemental solid emitters intentionally changed gravity/velocity/lifetime/counts. The other 56 retain the preceding review commit's travel and emission properties.
- No runtime packages, media decoding dependencies, footage or contact sheets enter the addon.
- No rendered candidate capture, packaging success, frame-time measurement, dedicated-server/remote-client/JIP or AI-visibility test is claimed.

Direct verification should compare dry/wet metal AP strikes; isolated HE and grenade bursts; prolonged room fire; cannon gunner/exterior views; smoke deployment over dry and wet terrain; near/far LODs; and calm-to-windy transitions. Check surface holes separately from dust, spark falloff separately from smoke, and the small local puff separately from accumulated room haze. Enable condensation only in an authored comparison case and inspect its short lifetime independently.
