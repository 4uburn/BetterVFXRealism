# Wall impacts and indoor particulate follow-up

REQUIREMENTS
- Fix angle-insensitive wall impacts and missing glancing debris.
- Replace concentrated floating indoor dust balls with gradual dispersed haze from impacts.
- Improve sustained smoke-grenade discharge and smoke motion against walls.
- Review the two session screenshots, real smoke footage and relevant physics; retain the existing PR and testable budgets.

MINIMUM COMPONENTS NEEDED
- Existing surface/tuning/AP/contact helpers for traced hit normals and bounded directional ejecta.
- Existing room fog and smoke particles, with spatial emission, continuous source curves, settling/buoyancy and native collision/drag.
- This evidence record and focused geometry/data regression checks.

REJECTED/NEEDS CLARIFICATION BEFORE ACTION
- No new fluid solver, per-particle script updater, damage/ricochet simulation, Blender dependency, or altered hole decals.
- Gas is transported by airflow; a bouncing solid-debris equation alone cannot predict smoke. Native particles approximate transport, not pressure or room ventilation.

PRIMARY RISKS
- Recent-shot ray matching is an approximation and must validate the actual surface near the impact.
- Native sprite collision does not guarantee room containment; a few traces cannot reconstruct arbitrary room geometry.
- Static/compile checks cannot prove the screenshot defect is visually resolved; angled-hit and small-room comparisons remain necessary.

REQUEST INTERPRETATION
- Fix the reported runtime defects in the same review contribution. Preserve the separate 0.33 fragment-wisp and palette contracts, identities and damage behaviour.

UNDERSTANDING OF THE OVERALL TASK IN A BRIEF SUMMARY
Use traced geometry and restrained particle transport to make wall shots visibly directional, dust accumulate as dispersed haze, and smoke discharge continuously and move along obstacles.

## Baseline evidence (101bb82)

- Session screenshots Screenshot 2026-09-05 020244.png and Screenshot 2026-09-05 020416.png were inspected locally. The first shows a dense round brown cloud in a narrow entryway; the second shows a thin disconnected white trail near a smoke device. A still image does not prove emission timing or obstacle response by itself.
- DeflectImpactSplash uses the particle entity up-axis as the surface normal, skips parented effects and rejects dot(D,N) > -0.15. This explicitly excludes shallow glances. It also rotates an already emitting world-space effect, which cannot redirect particles already born.
- RoomFog emits 24+12 particles at a zero-volume point, at rate 1000, with 0.12 m/s initial speed, drag 3, common size growth and correlated fade. Clamping size in a small room compresses that cloud without distributing its particles.
- AN-M8 sustained near-source emission starts at zero and reaches only 0.363 of authored rate after 9 seconds; the long layer reaches full rate after 18 seconds. The near-source velocity is zero outdoors. Initial layers exhaust their small capacities almost immediately while particles live 10 seconds.
- Smoke collisions retain little outward speed; no positive transport away from a head-on wall is guaranteed. Zero/positive gravity can leave aerosol pressed against a wall under wind. Native drag/collision/buoyancy are available; per-particle fluid pressure and humidity are not.

## Scope update: firing gases and physical relationships

REQUIREMENTS
- Include fragment-generated room dust, firing-driven haze, action/magwell gas, and more gas for suppressed firearms.
- Explain momentum loss, buoyancy, settling, wall interaction and source/removal balance; distinguish visual coefficients from measurements.
MINIMUM COMPONENTS NEEDED
- A bounded action-gas helper in the existing casing-smoke file and a single small gas particle asset.
- The existing muzzle callback, native suppression status and actual weapon chamber/magazine attachment points.
- Reuse the room dust accumulator for fragment impacts with a clear path inside the source room.
REJECTED/NEEDS CLARIFICATION BEFORE ACTION
- No guessed universal ejection-side position, firearm modifications, weapon-pressure computation, or scripted solver for every smoke particle. Missing attachment geometry skips that source.
PRIMARY RISKS
- Suppressor designs differ: the exposed gas multiplier is an art approximation and can be tuned per weapon.
- The chamber pivot locates the action but does not expose the exact vent shape or opening time.
REQUEST INTERPRETATION
- Extend the active defect fixes to the newly requested weapon gases and fragment contribution; all remain cosmetic.
UNDERSTANDING OF THE OVERALL TASK IN A BRIEF SUMMARY
- Improve all discussed effect paths using source-specific emission and bounded physical approximations within Enfusion.

## Physical model and implementation

### Impacts and fragmentation

An impact transfers momentum: `J = integral(F dt) = m (v_after - v_before)`. The wall receives the opposite impulse. An average force estimate needs the stopping time, `F_avg = J / delta_t`; peak force additionally depends on the deformation history. Incoming momentum and energy divide among penetration, projectile/target deformation, heat, sound and ejecta. No exact force, fragment mass, stopping time or ballistic outcome is inferred from footage. This addon changes none of those gameplay quantities.

For a normalized incoming direction D and an outward-facing surface normal N, resolve `D_t = D - N (D dot N)` and incidence `c = clamp(-D dot N, 0, 1)`. Our direction-only visual rule is `E = normalize(D_t + N (0.2 + 0.8 c))`. It retains the tangential component at shallow hits, while keeping a positive lift off the wall. The coefficients are authored visual choices, not experimentally fitted material constants. Head-on hits eject along N; a shallow shot retains along-wall motion without being discarded by an angle cutoff.

The effect is spawned paused and then oriented before emission. Only the six BER wall-hit overrides identified by their unique fines emitter qualify; nearby blast and third-party effects retain their resources. The actual nearby traced normal replaces the particle transform guess. The original hole decal remains untouched. AP surface/object classification now comes from the same incoming trace, with the existing six-axis probes as fallback. When no shot ray matches, native orientation remains. Recent-shot matching is approximate and needs multiple-shooter tests, especially at long range.

The cone bound is `45 degrees * dot(E,N)`, which conservatively fits inside the outward hemisphere. A mathematical spot check at incidence 0, 30, 60, 85 and 89.9 degrees produced nonzero outward directions and cone limits inside that hemisphere. This checks the formula, not native particle rendering. Solid chips retain gravity 1, zero wind and inelastic collisions. Fragment rays use the same directional rule for their existing 0.33-size wisps; nearby dry fragment hits add a bounded amount to the existing room-haze accumulator. This models dust newly liberated by strikes, not every fragment entraining the whole surrounding cloud.

### Updraft and smoke against walls

A warm parcel's upward buoyancy is approximately `F_b = (rho_air - rho_parcel) V g`. Air entrainment and heat exchange reduce its temperature difference and thus buoyancy. NIST describes smoke plume entrainment, ceiling spreading and wall cooling; these processes explain why smoke does not simply stop on reaching a wall. See [NIST CFAST technical reference](https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=861384) and [NIST ceiling-jet/wall-flow discussion](https://tsapps.nist.gov/publication/get_pdf.cfm?pub_id=916979).

At an impermeable wall the airflow's normal component is constrained; pressure redirects flow along the surface. For a sprite collision we approximate this with `v_next = e_t v_t - e_n v_n`, retaining tangential speed while strongly dissipating normal speed. Smoke uses small normal restitution (0.12), tangential retention (0.95), drag and weak upward acceleration. This is an ensemble visual approximation: gas does not bounce like rubble. Native particles do not provide a pressure field, solved room ventilation, thermal exchange, or guaranteed corner/ceiling circulation. A head-on wall collision has little initial tangential velocity; buoyancy and varied birth directions provide subsequent motion, rather than a claim of a solved wall jet.

All 16 indoor/outdoor grenade variants now have faster near-source discharge, an early sustained-emission plateau, and a slower expanding outer layer. Source ramps begin at 60% and reach full rate at 1% of device emission duration. Near-source velocity is 1.8 with 0.8 variation; the outer layer uses 0.65 with 0.45 variation. Negative gravity coefficients are visual lift, not measured gas temperatures. The faster near layer and weaker outer-layer lift approximate a cooling plume; lift does not dynamically change with temperature. Existing grenade colours, caps, LODs, occlusion, duration and repeat behavior are preserved. Outdoor wind remains native; sheltered variants remove the outdoor wind term.

The [48th Fighter Wing M18 training footage](https://www.dvidshub.net/video/774695/m18-smoke-grenade-training) was played and a developed plume frame inspected: the source feeds a continuous cloud extending downwind. This is qualitative reference, not a calibrated discharge-rate measurement or timing analysis. A YouTube Pine Bluff Arsenal clip was opened but not sufficiently inspected to support additional visual claims.

Long-lived authored explosion smoke now also gets collision, tangential retention and weak lift; selected fine-dust emitters no longer have zero normal restitution that pins their motion on contact. Short fire, lights and prefab contact emitters are excluded. [Bohemia's Particle Editor documentation](https://community.bistudio.com/wiki/Arma_Reforger:Particle_Editor) and the installed Enfusion API support these native parameters. Their drag coefficients are not asserted to equal SI fluid drag constants.

### How long dust hangs

For an isolated small spherical grain in still air at low Reynolds number, balance Stokes drag against effective weight:

`v_settle = (rho_particle - rho_air) g d^2 / (18 mu)`.

The diameter-squared dependence is the useful design fact: a fine tail can remain airborne after the visible chips have fallen. See [NIOSH aerosol measurement reference](https://www.cdc.gov/niosh/nmam/pdf/NMAM_5thEd_EBook-508-final.pdf) for aerodynamic diameter and settling, and [NIST smoke-component transport](https://www.nist.gov/publications/generation-and-transport-smoke-components) for sedimentation, diffusion and wall losses. Larger debris requires a different drag regime; [NASA's falling-object model](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/falling-object-with-air-resistance/) describes weight and aerodynamic drag.

Illustration only: for spherical mineral grains of density 2500 kg/m3 in air of density 1.2 kg/m3 and viscosity 1.81e-5 Pa s, ignoring slip, turbulence and deposition:

| Diameter | Still-air settling speed | Time to fall 2 m |
| --- | --- | --- |
| 2 micrometres | 0.000301 m/s | about 111 minutes |
| 10 micrometres | 0.00752 m/s | about 4.4 minutes |
| 30 micrometres | 0.0677 m/s | about 30 seconds |

These are calculations from stated assumptions, not measured wall-impact dust. Ventilation, irregular grain shape, aggregation and thermal motion change them. Visible cloud opacity also falls through dilution before every particle deposits. A 40-second sprite lifetime therefore represents a visible haze layer fading out, not a claim that all real dust has settled.

### Accumulation and firing gases

For a mixed-volume approximation, `dM/dt = Q_hits + Q_fragments + Q_gas - k M`. This addon keeps structural dust and firearm gas as separate visual sources. The room dust event accumulator follows `M_new = M_old exp(-dt / 20 s) + hit_weight`. The 20-second time constant is a bounded visual memory, not measured room ventilation. Dust amount depends on material, shelter, wetness and existing caliber weight. Fragment contribution is limited to nearby sheltered hits with a clear path to the source room, and capped at weight 3 per detonation.

Room fog now births gradually over four seconds from a nonzero box, with randomized frames, varied lifetimes and much lower per-sprite opacity. Six clearance probes constrain the birth box and a swept center move avoids the old fixed 1.4 m pull through narrow spaces. Increased source strength changes amount rather than compressing a single larger sprite cloud. Each layer caps at 12 particles, with at most four layers per 40-second accumulation window and 64 tracked room records. Layers can briefly overlap the next window during their remaining lifetime; 48 is a per-window birth capacity, not an absolute global live-particle cap. Ray-based room grouping and billboard bounds cannot guarantee arbitrary building containment.

The existing muzzle callback now drives one small action-gas asset at an authored chamber pivot and the inserted magazine's attachment slot. Missing geometry skips that source. The magwell contribution is 18% of the chamber contribution. No universal guessed side offset is used. A per-weapon firing memory follows `H_new = min(20, H_old exp(-dt / 4 s) + 1)`; gas amount is proportional to `0.45 + 0.04 H`. Native suppression status applies a default 2x factor, exposed alongside the amount setting on SCR_MuzzleEffectComponent. That factor is adjustable because suppressor designs differ; [HUXWRX describes reduced blowback for its flow-through designs](https://huxwrx.com/technology), a manufacturer statement supporting variability rather than a universal multiplier.

This is chamber/action haze, not a simulated vent geometry, operating cycle, internal pressure or temperature. Carried firearms qualify; launchers/turrets do not receive magwell gas. Each source emits for 0.12 seconds, caps at eight sprites and lasts at most 1.2 seconds outdoors or 3.6 seconds indoors. At most 96 action effects and 96 recent weapons are tracked. Repeated shots overlap and increase gas amount; the quiet-period decay prevents permanent buildup. Existing muzzle dust and impact haze remain complementary sources. No per-particle script loop or new scheduled callback is introduced.

## Validation and direct acceptance

- Existing data contracts plus new haze/smoke/gas budgets pass: 125 particle assets, 525 emitters, 184 local resource GUIDs, 17 exact 0.33 wisps and 60 surface-palette assets.
- MCP structured inspection passed on all 37 changed/new particle assets in this follow-up.
- Comparison with 101bb82 preserved all 52 grenade emitters' caps, LODs, occlusion, duration, repeat and material settings; non-smoke grenade emitters are unchanged. All 12 prefab emitters inside the changed particle files are unchanged.
- Workbench WORKBENCH script validation passed with 14 base-game obsolete-API warnings and no addon diagnostics. Opening the action-gas resource succeeded. A hot reload was refused while an Animation Editor module was open; this was a module restriction, not a script diagnostic. After the final gas-budget timing guard, a clean review-project launch at 02:27:53 local compiled Game and WorkbenchGame successfully (Game CRC32 256b437b); subsequent native validation again passed with the same 14 base warnings. A stalled post-cleanup hot reload was recovered by restarting only the task-opened Workbench process. The review project is left open in World Editor with the M821 prefab selected.
- No calibrated plume measurement, full fluid simulation, rendered before/after firefight, multiplayer/JIP, performance or AI-occlusion acceptance is claimed.

| Direct test | Acceptance |
| --- | --- |
| Same rifle, wall, camera; 0/30/60/85-degree hits from both sides, then floor/ceiling | Debris remains on the incident side and visibly follows oblique direction. Hole persists. Check default, concrete/plaster, brick, stone, asphalt, wood and AP metal separately. |
| Two shooters crossing rays; near and distant impacts | Correct source association or safe native fallback; no unrelated explosion replacement. |
| Session's narrow entryway, corner and two adjacent rooms; single shots and sustained bursts | Haze builds from distributed hits rather than one opaque floating ball, fades after firing, and does not merge through solid walls. |
| Grenade indoors with fragment contribution, then fragment rays disabled | Small material wisps and a restrained additional haze contribution, no per-fragment giant cloud; wetness suppresses dust while retaining holes. |
| Each smoke device at 0.5/2/5/20 s, stationary and moving, still/windy outdoors, normal/oblique wall, corner and ceiling | Sustained discharge without the old long weak ramp; source follows device and old smoke stays behind; motion continues along obstacles. Compare AI occlusion and near/far LODs. |
| Unsuppressed vs suppressed rifle, sustained burst, magazine reload, moving shooter and scoped view | Chamber/magwell position is credible, suppressed multiplier is visible, no duplicate callback cloud, quiet-period decay and source caps work. Tune low-backpressure suppressor override separately. |
| Two clients, dedicated server, JIP, scenario restart and long gunfight | No script errors, acceptable frame time and particle load, expected visibility and occlusion behavior. |
