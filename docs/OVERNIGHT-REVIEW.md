# Overnight realism refactor — 2026-09-05

Status: refactor and native checks complete, ready for draft-PR review. Final combined run: zero failures, 2,340 samples over 120.014 seconds; original smoke completed and room layers cleared. Final Workbench compilation passed. Baseline d8c5faa. Inline only, no subagents. Start 06:36:18 UTC; hard stop 08:36:18 UTC.

REQUIREMENTS
- Improve existing scripts and effects using the supplied impact/indoor evidence and primary physics references.
- Preserve identities, native holes, exact 0.33 fragment wisps, palette contracts, wetness, device duration/occlusion and bounded real-time cost.
- Exercise production helpers and particle assets in Workbench; inspect rendered behavior where available; keep one test run active at a time.
- Update the existing draft PR with verified work and explicit evidence boundaries.

MINIMUM COMPONENTS NEEDED
- Existing surface, muzzle, tuning, wind, casing and vehicle helpers; refactor only the state that causes observed behavior or measurable overhead.
- Existing particle assets, including source-specific smoke and fine dust.
- Extend the existing opt-in test fixture for repeatable native checks; use one local review world and one consolidated evidence report.

REJECTED/NEEDS CLARIFICATION BEFORE ACTION
- No new dependencies, fluid solver, damage/weapon-pressure simulation, subagents, Workshop publication or changes to the untouched main checkout.
- Exact terrain-pixel sampling and heat refraction require supported renderer hooks; do not substitute undocumented shader behavior or claim them solved.

PRIMARY RISKS
- Native sprite collisions and coarse room traces approximate transport and cannot guarantee arbitrary room containment.
- Nearby shot inference is fallible; preserve native fallback and test glancing geometry explicitly.
- Smoke occlusion/LOD and fire duration are gameplay-sensitive; do not change them while improving cosmetic discharge.
- Retaining stale effects or deferred callbacks can outlive the scene; limits and teardown need native checks.

REQUEST INTERPRETATION
- A bounded autonomous refactor of the existing team contribution, guided by evidence and tests, ready for Auburn to review.

UNDERSTANDING OF THE OVERALL TASK IN A BRIEF SUMMARY
Make existing effects behave more naturally through correct source timing, material/angle response, room accumulation, settling and obstacle transport, while simplifying fragile implementation details.

## Diagnostic findings

- Indoor smoke adoption creates a fresh full-duration particle effect and stops the original emission. A late swap therefore restarts the smoke clock and repeats onset; environment is cached once even while a thrown device moves.
- Room haze stores five parallel arrays and a fixed 40-second generation window. The layer count never falls as particles expire, so sustained fire can reach a quota then cease replenishing until the window resets. The mass is decayed but emission thresholds depend on an irreversible layer count.
- Local-space muzzle clouds remove wall-normal movement as soon as a long look-ahead ray sees a wall. This can freeze the cloud metres before contact; the trace is also offset 0.5 m above the actual entity.
- Shot history uses a muzzle-effect transform rather than available projectile velocity, and prefers distance to any recent ray without a time-of-flight check. This can misassociate crossed shots.
- The six wall assets have distinct fine/solid layers; the recent wisp/colour contracts remain intact and must stay intact.
- Action/magwell source rates can fall below one particle per short burst; attachment coordinates and actual native birth behavior need measurement before changing them.
- Vehicle exhaust contains a duplicated BirthRateRND write in its unstaged branch. This is redundant, not a double multiplier (native MultParam uses authored originals).

## Work log and evidence

- Read-only baseline and API inventory: current 11 Game files (one original cloud-field module already removed), existing authored particles and latest follow-up records reviewed. Workbench is open on an unnamed empty world; no live test is running.
- Reconfirmed native Particles parameter semantics, ProjectileMoveComponent.GetVelocity, muzzle suppression/magazine APIs and native emitter enumeration through MCP. BI particle page request returned HTTP 403; installed generated source is the fallback evidence.

- Native baseline run found all five paused-spawn paths had no particle handle: the previous code skipped tuning. All three gas strengths therefore peaked at the same five particles. This is a runtime defect, not an inferred art preference.
- A second native run verified Play then Pause creates the handle at simulation time zero with zero particles. Tuning then resume produced gas peaks of 1 / 3 / 5 for magwell / receiver / stronger receiver. All helper assertions passed, and the room layer completed within 45 seconds.
- Added one shared SpawnPaused helper in the existing surface utility, used by all 11 production tunable spawn sites and the review fixture. It does not add a timer or service; callers retain their existing ownership and resume points.
- Saved a native-generated opt-in review world (GUID D36F38413B1A5FB6). GUID-qualified prefab creation is essential: unqualified MCP creation loses inherited components on save. Replaced those fixture instances with native base references.
- Workbench hot reload initially failed because Animation Editor does not support it. Closing that unused module exposed a native thermal-profile resource assertion. A clean single Workbench instance restored handlers; subsequent hot reloads succeeded. Two temporary instances were stopped; no extra Workbench process was retained.
- Native collision-room test resolved 0 / 30 / 60 / 85 / 89.9 degree wall strikes, outward ejecta, roof/floor detection, blocked cross-wall placement, and zero-birth suppression. Fixture floor height revealed the initial sources were slightly inside its sloped collision mesh; the floor was lowered before the final visual and runtime checks.

- Combined production-room run: 8 layers created over sustained input, at most 4 alive and 44 sprites, no remaining room layers after firing stopped; 120.02 s run, zero failures. M18 original burn/tail completed naturally. Rendered smoke remained concentrated near the source-side wall, motivating a bounded outer-plume velocity/drag/lift adjustment while retaining the hot discharge layer and all duration/occlusion/cap fields.

## Native firing and source findings

- The initial real-rifle test consumed 20 rounds from each rifle but produced no action gas. A corrected observer excludes the stopped standalone test effects, so those cannot produce a false pass.
- Native tracing confirmed the muzzle hook and configured amounts were present, with two attachment callbacks for each projectile. The chamber identifier was `-1385089522`: a valid signed native node, rejected by the previous `>= 0` check. Matrix lookup now validates the node; the chamber's authored PointInfo supplies its world transform.
- The inserted magazine is attached through InventoryItemComponent.GetParentSlot. EntitySlotInfo.GetSlotInfo alone returned no source for the tested rifle. The inventory slot is now preferred, with the existing entity-slot lookup retained as fallback.
- Final live rifle runs consumed 20 unsuppressed and 20 suppressed rounds. Each produced 40 gas effects, exactly one chamber and one magwell source per shot. Summed native birth rates were approximately 634 and 1268, confirming the configured 2x suppression factor reaches the emitters. Source positions were distinct and on the carried weapon. This tests M16A2 and its native suppressed variant, not every weapon or suppressor.
- Shot history now uses available projectile velocity and flight-time plausibility, and rejects duplicate callbacks for the same live projectile before dust impulses/volley accumulation. Native straight and oblique rifle shots recovered their direction and 0.36 rifle scale. It remains an association heuristic, not a replacement ballistic simulation.
- AP surface selection now tries the matched shot first; its six nearest-surface probes are a fallback. The previous code could return early when those probes failed before trying the valid incoming ray. Repeated probe blocks were replaced with one small loop.
- Directional cone tuning previously omitted AP `stone_chips`, `rock_chunks_*`, `dirt_chips` and `dirt_clumps`. Those solid layers now receive the same outward cone restriction. Native 85-degree asset tests check rock, dirt and spark emitters while confirming smoke/light cone values remain unchanged.

## Smoke and room transport

The actual M18 device was triggered through its native TimerTriggerComponent. Its original particle source was checked indoors, moved outdoors, and moved back indoors. All three smoke emitters reported wind off/on/off, the same source followed the device, and its clock advanced approximately 3.1 -> 12.4 -> 22.1 seconds without restarting. This exercises the production shelter-update path, beyond simply setting a parameter on a standalone test particle.

Sixteen smoke assets and 26 outer emitters were adjusted. A comparison against d8c5faa confirms only Velocity, VelocityRND, AirResistance, AirResistanceRND and GravityMultiply changed. All other serialized fields, including caps, colours, duration, repeat, occlusion, LOD and non-smoke emitters, are identical. MCP structural inspection passed for all 16 files. Native GetParamOrig confirmed velocity 0.95, drag 0.3 and gravity coefficient -0.003 on both M18 outer LOD layers.

The room accumulator now tracks living effects rather than permanently consumed layer slots. A production-path test feeds dust for 75 seconds, then stops: eight layers are created across the run, no more than four are alive at once, the measured peak is 44 sprites, and none remain at 120 seconds. The analytical bound is 48 sprites per room record, not per building. Adjacent rooms require an unobstructed path to share a record.

For local-space muzzle dust, the movement model uses exact exponential impulse decay and its integrated displacement: `v1 = v0 exp(-k dt)` and `dx = v0 (1 - exp(-k dt)) / k`. A cached wall now limits displacement only when the cloud reaches its clearance plane; tangential movement remains. This removes the earlier stop-at-look-ahead-distance defect. Native world-space explosion and smoke particles continue using their own drag/collision integration.

The production animator was exercised against native collision geometry: a cloud started at `(0, 1, 6.5)` and reached approximately `(0.6433, 1.0081, 8.9953)` against a wall at `z=9.0246`. It approached the wall, retained sideways travel, and stopped approximately 3 cm short of the plane. This is one planar-wall impulse case; it does not establish complete corner circulation or changing-weather performance.

The isolated M67 warhead visual test produced 34–36 single-emitter material-wisp effects across two random runs, with peaks of 102–108 wisp sprites. The separate debris effect peaked at 27 total sprites (including that effect's dust sprites). All test assertions passed. The scene's post-burst haze and scattered small fragments were inspected; no calibrated flash-duration or all-material visual acceptance is inferred from those snapshots.

## Physics and reference interpretation

- Buoyancy arises from a density difference. A warm mixture entrains surrounding air, cools, and becomes less buoyant; at a ceiling, pressure redirects the plume laterally. The addon represents this with a stronger hot source and a weaker-lift outer layer. It does not calculate temperature, pressure or room ventilation. [NIST ceiling-jet research](https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=916979) supports the qualitative transport distinction.
- Fine aerosol is carried by airflow, while larger debris retains more inertia and settles faster. Settling, diffusion and surface losses all affect lingering smoke; a particle lifetime is a visibility/cleanup choice, not a measured deposition time. See [NIST smoke transport](https://www.nist.gov/publications/generation-and-transport-smoke-components) and [NASA weight/drag explanation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/falling-object-with-air-resistance/). The preceding follow-up document includes the stated-assumption settling calculations.
- The supplied local clips were rechecked with one bounded set of contact sheets outside the repository: compact M242 muzzle pulses, repeated AP flash/sparks with residual grey aerosol, HE/white screening smoke, larger coherent explosion clouds, grenade dirt throw and building dust. They support relative timing/layering, not measured projectile energy, particle mass or calibrated emission rates. The thermobaric identification remains the user's label.
- No new simulation service, external dependency or per-particle script loop was added. Art coefficients remain visible in the existing assets/scripts. No native weapon damage, recoil, projectile flight, magazine capacity or fragmentation damage was changed.

## Validation boundaries

Workbench 1.8.0.13 compiles the changes with no addon errors and the same 14 base-game obsolete-API warnings. The data validator passes 125 PTCs / 525 emitters / 185 local GUIDs, including the new world, with all 17 exact 0.33 wisp and 60 palette contracts intact. Combined 120-second native runs pass, including real rifle fire, signed chamber nodes, original smoke burn completion and moving-source shelter transitions.

The editor had an access violation during one hot reload after a live-character test. The crash log does not establish a script cause; a clean single instance restored the project. Subsequent separated stop/reload cycles passed. No crash report was manually submitted. Compile/runtime checks do not establish a Workshop package, multiplayer/JIP behavior, arbitrary-room containment, AI occlusion parity or a frame-time performance result.

## Reproduce the review

Open `BetterEffectsRealism.gproj` from this branch, then `{D36F38413B1A5FB6}BER_Review.ent`. The small native collision room has three walls, a floor, a roof and the existing opt-in fixture. It intentionally has no game mode or terrain; inspection uses the free camera.

- The saved review world enables regression mode, the room check, native rifle/suppressor fire and the moving M18 device, for 120 seconds. Press Play; look for `BER RESULT failures=0` in the script log. It includes pure helper checks, native emitter initialization/parameter checks, signed chamber-node resolution, live ammunition consumption, two gas sources per shot, stronger suppressed discharge, straight/oblique shot association, smoke clock/shelter transitions, actual cloud-wall motion and sustained room-haze cleanup.
- For the separate grenade demonstration, disable `m_bRunRegressionTests` and enable `m_bEnabled` on BER_ReviewFixture. Its M67 warhead visual path starts eight seconds into play. Look for `BER FRAGMENT RESULT ... failures=0` approximately 20 seconds later. Regression mode takes precedence if both switches are enabled.
- Exit play with Escape and allow the editor to return before reloading scripts. The MCP mode heuristic can report edit mode during this free-camera simulation; native rendering and test logs are the reliable evidence.
- The fixture's modes default off in the reusable prefab. Test callbacks and directly owned test entities are removed on fixture teardown. Production effects retain their normal bounded lifetimes. No test runs automatically in ordinary gameplay worlds.

The wider direct acceptance matrix remains in [IMPACT-AND-INDOOR-FOLLOWUP.md](IMPACT-AND-INDOOR-FOLLOWUP.md): real buildings/materials, crossed remote shooters, moving/scoped weapon views, all smoke variants and weather, dedicated-server/JIP, AI occlusion and frame-time comparisons still need release acceptance. Exact terrain-pixel RGB sampling and true heat refraction remain unsupported and are not represented as completed features.
