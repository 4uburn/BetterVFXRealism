# Better VFX Realism — Auburn review

## Decision record

REQUIREMENTS
- Review and improve all existing code, particles, prefab wiring, wind and effect lifecycles as a team PR for Auburn.
- Make explosion dust denser and less scattered; make its surface-related palette 33% lighter.
- Fragment landings use only a primary wisp at 33% of vanilla linear size; retain lasting hole decals independently of dust.
- Fade loose dust from dry/dusty surfaces to none on wet ground; preserve combustion smoke, water reactions and solid debris.
- Support lingering fine particulate during sustained indoor fighting, informed by physics.
- Keep the changes inspectable and provide repeatable checks plus explicit live-test boundaries.

MINIMUM COMPONENTS NEEDED
- Auburn's existing addon and helpers, refactored in place on an isolated review branch.
- Existing particle resources and material contacts; one shared dust classifier/tuner.
- This research/reviewer guide, a small reference fixture, and a standard-library asset validator.

REJECTED/NEEDS CLARIFICATION BEFORE ACTION
- A separate successor addon was rejected: this is a contribution to Auburn's project.
- No new package dependency, damage simulation, fluid solver, copied Squad content, Workshop release or merge.
- Exact rendered terrain RGB cannot be sampled and applied through the inspected public particle API. Surface-specific authored palettes are the reviewable approximation, explicitly below.

PRIMARY RISKS
- Dust, smoke, fire, sparks and prefab debris share effects and need independent treatment.
- Proximity adoption can claim a neighbouring shot's effect; emitter names and ownership reduce this risk but do not constitute an engine event identity.
- Particle sprites and roof/room traces cannot guarantee wall containment or model ventilation.
- Compile/data checks do not prove appearance, performance, AI visibility or multiplayer behaviour.

REQUEST INTERPRETATION
- Refactor the complete existing script set; improve behaviour where supported by evidence rather than replace working features solely to change every line.
- Preserve addon ID BetterEffectsRealism, GUID 4DA875471101785E, BER class names and resource identities. Display title is Better VFX Realism.
- 33% size is 0.33 times vanilla, including size variation; not a 33% reduction.
- 33% lighter is RGB component multiplication by 1.33, clamped to 1, against the material's original vanilla primary-wisp palette. This is neither perceptual luminance nor a live terrain-pixel measurement.
- “Debris hanging in the air” means fine suspended material; heavy chunks retain gravity.

UNDERSTANDING OF THE OVERALL TASK IN A BRIEF SUMMARY
Improve Auburn's existing VFX implementation as one researched review PR, covering fragmentation, dust density/colour, wind, indoor buildup and code reliability. Prepare it for direct visual verification without claiming a release has been validated.

## Evidence and physics decisions

| Evidence | Consequence for this implementation |
| --- | --- |
| [FEMA 426, chapter 4](https://www.fema.gov/pdf/plan/prevent/rms/426/fema426_ch4.pdf) describes a brief pressure pulse and reflected loading around structures. | Remove the three-second “blast overcomes wind” assumption. Use brief decaying local impulses; line-of-sight checks block the simple impulse model at walls. This does not simulate reflected shocks. |
| [Explosion-induced dust entrainment experiments](https://ntrl.ntis.gov/NTRL/dashboard/searchResults/titleDetail/PB253014.xhtml) distinguish entrained surface dust from the initial blast. | Keep ground/contact dust separate from hot combustion aerosol and solid fragments. Preserve invisible contact-trigger travel so it still reaches surfaces. |
| [NIOSH dust-generation experiments](https://stacks.cdc.gov/view/cdc/204722) examine material, moisture and impact conditions. | Use material availability and wetness to suppress loose dust. Wet does not mean all impact products or smoke disappear. |
| [Aerosol settling and transport](https://wwwnc.cdc.gov/eid/article/12/11/06-0426_article) and [ventilation/removal principles](https://wwwnc.cdc.gov/eid/article/3/3/97-0310_article) distinguish persistent fine particles from fast-settling large particles. | Fine indoor dust lasts tens of seconds with a gradual fade; stones and clods remain short-lived ballistic effects. Roof detection is only a shelter approximation, not measured air exchange. |
| [Sandia multiphase shock experiments, SAND2011-8421](https://www.osti.gov/servlets/purl/1030399) concern gas/particle coupling. | Delete rigid cloud repulsion/merging. Moving whole clouds apart is not a gas-flow model. Native particle drag handles outdoor explosion dust. |
| [BI Particle Editor](https://community.bistudio.com/wiki/Arma_Reforger:Particle_Editor) documents emitter lifetime, local transforms, drag and wind. | Explicitly classify named dust/smoke emitters. World-space dust has nonzero air resistance; fire, solids and prefab triggers avoid blanket tuning. |
| [Squad 7.2 notes](https://www.joinsquad.com/updates/squad-7-2-release-notes) discuss synchronizing explosion effects with damage. | Use Squad as a reference for readable, coherent events, not a source of physical constants or copied assets. No claim of measured visual parity. |

The numeric art settings are proposed tuning values, not experimentally calibrated physics: 1.35 density, 0.65 authored dust footprint/velocity factors, up to 40-second fine-dust lifetime, wetness cutoff 0.25, room weights and particle budgets. They are deliberately visible in existing code/assets for Auburn to judge.

### Colour and size contract

The checked-in [reference fixture](vfx-contract.json.txt) records 17 original vanilla primary-wisp size pairs and 60 surface-palette assets. The fixture uses a text suffix so Workbench does not try to register it as a runtime JSON resource.

- All 17 fragment resources contain only one primary wisp. Both SizeMultiplier and SizeRND equal their original vanilla values times 0.33.
- Colour keys on classified dust use the material-reference RGB times 1.33. Alpha remains a separate opacity curve.
- Material contacts select the surface's authored effect. Generic explosion gas uses a neutral reference palette; it cannot dynamically recolour itself to arbitrary terrain through the inspected API.
- EmitterParam exposes no runtime colour control. Lighting, terrain textures, blending, exposure and surface variations prevent guaranteeing a rendered 33% terrain difference. That part of the request remains an approximation requiring visual acceptance.
- Combustion smoke, sparks and coloured smoke grenades retain their own colours.

The public API was checked through Enfusion MCP and the installed generated script sources: EmitterParam, Particles, World.CreateDecal, game-material hit effects, weather and contact components. MultParam applies against the authored original; repeated calls are not multiplicative accumulation. Emitter names are resolved at runtime because dedicated-server emitter filtering can change indices.

The subsequent [video reference and debris mathematics pass](VIDEO-REFERENCE-REVIEW.md) records all six supplied clips, three YouTube references, cannon/AP/HE changes, fine-dust tails and the selected large-blast condensation effects and heat-refraction boundary.

## Whole-project review and changes

Baseline: 0dd4fcf. Inventory: all 12 original Game scripts, 121 particle files, 59 prefabs and 180 resource metadata identities. Existing prefab/resource references and every emitter's authored properties were inspected. This was a source/data review; no rendered recording was produced.

| Original module | Findings and final treatment |
| --- | --- |
| BER_EffectTuningComponent | Repaired the undefined fragment lookup. Preserved material holes independently of wetness. Removed oversized global impact haze and broad 6–8 m adoption. Tune only named dust/smoke; own paused spawns before emission. Reuse a bounded, per-room accumulation model and remove unused settings. |
| BER_SurfaceUtil | Centralized dust availability and named-emitter tuning. Removed sea-level-as-wet-sand assumption; distinguish dry mud from wet mud. Keep persistent wetness, shelter and material exclusions consistent. Ownership storage is lazy, bounded and reset per world. |
| BER_OrientedContactComponent | Tune the actual contact material before the first particle, orient toward the incident side including walls/ceilings, claim ownership immediately and guard optional sound data. |
| BER_APDSImpactResolver | Keep surface selection and normal probes; gate only dust. Wet metal still has its hot impact aerosol/sparks. Indoor lifetime changes no longer stretch every chip/spark. |
| BER_WindDriftAnimator | Restrict whole-entity motion to existing local-space muzzle dust. Remove cloud-separation support and duplicate blast sweep. Refresh wind twice a second, retain calm-weather registrations for later gusts, bound entries/impulses, probe obstructions and reset callbacks on world changes. |
| BER_CloudField | Removed. Pairwise cloud repulsion and birth-rate “merging” could scatter the effect and did not merge already emitted gas. No remaining consumers. |
| BER_MuzzleBlastDust | Lazy, bounded session state and shot history; per-weapon volley accumulation instead of one global volley. Wall-gated radial impulses. Shelter-aware dust availability. Avoid adding whole-entity drift to native world-space heavy blast dust. |
| BER_DustReservoir | Lazy per-world state; evict the oldest cell instead of resetting all depleted surfaces. Bound tracked vehicles and block blast depletion across walls. Existing replenishment timing remains a gameplay approximation. |
| BER_VehicleFX | Guard invalid simulation/particles and zero speed range. Preserve water/mud/rock behaviour while tuning dry dust. Clear stale carryover on water contact, handle birth variation and clean up tail effects. |
| BER_CasingSmoke | Bound and reset hot-UGL watchers, stop callbacks on world change, and require the reloaded weapon to be the equipped launcher. Retain casing trails. |
| BER_WetnessAuthority | Reset state at game start, publish immediately from authority, remove duplicate/torn-down timers, and publish exact dry/wet endpoints despite the normal change threshold. |
| BER_FragTestSpawner | Make the test fixture opt-in, guard resource loading, and remove its queued callback at teardown. |

### Particle and prefab treatment

- Compact the existing ground-blast/mortar dust emission shapes and velocities. Preserve trigger-prefab velocities and timing: reducing their travel could prevent surface contact.
- World-space outdoor explosion dust and combustion smoke use native drag/wind. Supplemental solid debris now uses consistent gravity and bounded count/speed/cleanup; it does not ride a rigid drifting explosion entity.
- Existing six environment-hit overrides stop the large dirt-splash emitters. Normal bullet-hole handling remains the engine's responsibility; fragment effects contain only the reduced primary wisp.
- Fragment holes are requested on the struck entity for 300 seconds, before dust suppression. World.CreateDecal requires an entity; terrain traces without TraceEnt and surfaces that reject decals cannot be promised holes. A dynamic decal's null return is not treated as failure.
- Fragment rays are visual-only, uniform sphere samples, capped at 64 and 14 m. They do not alter damage, calculate fragment ballistics or represent measured shrapnel ranges.
- Indoor fog appears near the hit, pulled into the room and sized by clearance. A room receives one initial layer and at most three additional layers during its 40-second accumulation period; at most 64 rooms are tracked.
- The two room-fog assets fade gradually. Roof shelter disables ambient wind, while collision/clearance reduce wall leakage. Billboard edges can still intersect geometry.
- Explicitly wet indoor materials also suppress room-dust accumulation. Ambient rain alone does not wet a sheltered floor.
- Existing disabled vehicle hull blankets/sheets/wash remain disabled. The forced hull-dust test switch is now also off; wheel dust and exhaust remain active.
- No prefab or asset GUID was renamed. The unused wind-ramp override on the smoke-grenade base was removed with its unused attribute.

## Validation evidence

Validation was performed with installed Arma Reforger/Workbench 1.8.0.13 on Windows.

| Check | Result / boundary |
| --- | --- |
| Workbench MCP ValidateScripts, configuration WORKBENCH | Passed, 14 existing base-game obsolete-API warnings; no addon errors. This includes Game and WorkbenchGame modules, not a live dedicated-server test. |
| python tools/validate_vfx.py | Passed: 524 emitters, 183 resource GUIDs, 17 exact 0.33 size pairs, 60 surface-palette assets; local paths/GUIDs, numeric ranges, brace balance and dust classification checked. |
| MCP particle inspection | Initial pass covered all 121 particle files. Follow-up inspected all 17 changed/new assets; the completion pass also inspected all 48 curve-corrected assets. Current total 124. Structural parsing only, not rendering or a native runtime build. |
| Size-curve regression | Passed: 131 curves across 48 files preserve all control times and intended size/variation products within 1e-7 tolerance. Other particle fields are unchanged, apart from trailing newlines. Size/Alpha/Color curve values are now in 0–1. |
| Native Particle Editor | Loaded the addon condensation resource and confirmed corrected size properties without new curve errors. This is a limited load check, not visual acceptance of all effects. |
| Prefab-emitter regression comparison | 56 trigger/other prefab emitters retain preceding review travel/emission properties. Three supplemental solid emitters were intentionally retuned with the bounded debris model. |
| git diff --check | Passed. |
| Native data packaging | Not validated. The MCP build launcher missed base-game resolution; a direct hidden launch with the correct dependency path initialized and compiled the addon but did not produce a data build. Task-owned build processes were stopped. No package or Workshop release is claimed. |
| Multiplayer/JIP, performance, rendered VFX, AI occlusion | Not run. Required before release. |

MCP handlers and local caches are excluded from the contribution. The tracked resource database is preserved from baseline rather than shipping a database polluted by temporary editor handlers.

## Direct review matrix

Use identical camera, exposure, weather and weapons for baseline/candidate captures. Record frame time and active particles as well as appearance.

| Scenario | Acceptance to observe |
| --- | --- |
| Grenade, HE round, RPG, mortar, mine and satchel on dry sand/dirt/grass/stone; slopes and walls | Coherent dense initial dust; material-appropriate palette; contact effects actually reach surfaces; smoke persists while solid chunks fall. Check near/far LODs. |
| Same impacts on explicitly wet/muddy ground, at wetness 0 / 0.125 / 0.25, then after rain stops | Dry dust falls continuously to zero; residual wetness persists. Water spray, hot smoke, sparks and holes remain where appropriate. Dry sand at sea level still emits dust. |
| Fragment fixture enabled on concrete, wood, sand, metal and wet surfaces | Only a small primary wisp where dust is available; 0.33 authored size relative to vanilla. No large fragment splash; hole persists on a decal-capable entity after the wisp fades. |
| Calm outdoors, then changing wind direction/speed; muzzle puffs and explosions together | New gusts move old tracked muzzle dust; direction agrees with native particles; no double blast shove or rigid movement of chunks. |
| Two neighbouring rooms and a corridor, exterior wind and rain, prolonged automatic fire | Dust builds near hits, fades after firing stops, does not accumulate without bound or merge through a solid wall; sheltered floors remain dry. Check sprite leakage and open-door ventilation appearance. |
| Two shooters alternating weapons; sustained UGL reload/ejection; vehicle sand-to-water transition | Each shooter's volley works independently, callbacks stop on teardown, correct casing trail, water effects survive, no lingering dry wheel carryover. |
| Dedicated server, two remote clients, late join, restart scenario, long firefight | No script errors, no new duplicate effects, acceptable CPU/GPU/particle counts, intended client visibility and smoke/AI behaviour. Local cosmetics and their budgets are not assumed replicated. |

The branch is ready for code and direct visual review. Exact terrain-pixel colour, renderer appearance, packaging and multiplayer acceptance are not represented as proven.
