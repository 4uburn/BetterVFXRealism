//------------------------------------------------------------------------------------------------
// Better Effects Realism — effect tuning component
//
// Attached (via prefab override) to Warhead_Base — which EVERY explosion AND every bullet
// impact spawns — and to smoke grenade prefabs.
//
// TAKEOVER MODE (m_rTakeoverEffect set, per-warhead overrides): the vanilla explosion
// particle reference is blanked in the prefab and this component spawns the same effect
// itself, paused, so every parameter is set BEFORE the first particle exists:
//  - density/size/lifetime scaled by surface dustiness (rain-aware),
//  - emission time of long-lived dust emitters extended (opaque plateau, dissipation
//    truncated to the end of the lifetime instead of a linear fade),
//  - engine wind disabled and emitters switched to local-space simulation, then the
//    whole effect entity is ANIMATED by BER_WindDriftAnimator: standstill during the
//    pressure-wave hold, then smooth acceleration up to the actual wind speed.
//
// ADOPTION MODE (always active): child/nearby particles the engine spawned (gamemat
// bullet-impact dust, effects without a takeover override) get the same density/lifetime
// scaling; indoors their wind is disabled. They cannot be drift-animated retroactively.
//------------------------------------------------------------------------------------------------

[EntityEditorProps(category: "GameScripted/BetterEffectsRealism", description: "Surface/weather/indoor aware tuning of spawned effect particles")]
class BER_EffectTuningComponentClass : ScriptComponentClass
{
}

class BER_EffectTuningComponent : ScriptComponent
{
	[Attribute(defvalue: "", UIWidgets.ResourcePickerThumbnail, desc: "Explosion effect to spawn under BER control (vanilla reference must be blanked in the same prefab)", params: "ptc")]
	protected ResourceName m_rTakeoverEffect;

	[Attribute(defvalue: "1", desc: "Scale particle density/lifetime by surface dustiness at the detonation point")]
	protected bool m_bScaleBySurface;

	// 52nd-pass USER RULING (reference: real frag-grenade footage on grass, 3-frame sequence
	// in Downloads): explosions were generating far too much gas at far too large a size.
	// Real look = compact ground-hugging puff at detonation, then a LOW translucent cloud
	// ~6-7 m wide that thins out within seconds — no lingering gas ball. Old defaults
	// (2 / 3 / 2.2 / 1) pumped ~3x the emitted volume of these values; never re-raise
	// without an express instruction.
	[Attribute(defvalue: "1.1", desc: "Baseline particle density (birth rate) multiplier before surface scaling", params: "0.1 4 0.05")]
	protected float m_fDensityBoost;

	[Attribute(defvalue: "1.8", desc: "Baseline particle lifetime multiplier before surface scaling", params: "0.1 6 0.05")]
	protected float m_fLifetimeBoost;

	[Attribute(defvalue: "1.3", desc: "Emission time multiplier for long-lived emitters (keeps the cloud replenished, dissipation happens near the end)", params: "1 5 0.05")]
	protected float m_fEmissionBoost;

	[Attribute(defvalue: "0.7", desc: "Particle size multiplier on top of the authored .ptc sizes", params: "0.4 2.5 0.05")]
	protected float m_fSizeBoost;

	[Attribute(defvalue: "12", desc: "Seconds over which the drift-animated effect accelerates up to wind speed (0 = no drift)", params: "0 60 0.5")]
	protected float m_fWindRampTime;

	[Attribute(defvalue: "25", desc: "Roof detection trace distance in meters", params: "5 100 1")]
	protected float m_fRoofCheckDistance;

	[Attribute(defvalue: "8", desc: "Seconds to keep watching for spawned particle children", params: "1 300 1")]
	protected float m_fScanDuration;

	[Attribute(defvalue: "0", desc: "Smoke grenades: when the device sits under a roof, restart the adopted smoke effect as a windless BER variant so the screen stays in the room instead of being blown through walls")]
	protected bool m_bIndoorSmokeSwap;

	[Attribute(defvalue: "0", desc: "Ground debris (dirt clumps / rock chips) thrown by the detonation; 1.0 = the 25mm HEIT baseline, 0 = none (bullet impacts)", params: "0 5 0.05")]
	protected float m_fDebrisScale;

	[Attribute(defvalue: "0", desc: "Visual shrapnel impacts: number of fragment rays traced out of the detonation; every surface hit plays the struck material's bullet-hit effect and bullet-hole decal (0 = off)", params: "0 64 1")]
	protected int m_iBerFragImpacts;

	[Attribute(defvalue: "0", desc: "Kick accumulated dust off a struck vehicle hull (heavy rounds, 12.7mm and up); value = kickoff strength, 0 = off", params: "0 3 0.05")]
	protected float m_fHullKickup;

	protected const ResourceName DEBRIS_DIRT = "{BE20250902AC0020}Particles/BER/BER_Impact_DirtChunks.ptc";
	protected const ResourceName DEBRIS_ROCK = "{BE20250902AC0021}Particles/BER/BER_Impact_RockChips.ptc";

	// indoor events fill the room with structural dust fog that hangs at a constant
	// opacity for 30+ seconds (authored plateau alpha curve, windless, wall-collided);
	// wood interiors shed a browner, thinner sawdust haze instead of grey plaster
	protected const ResourceName ROOM_FOG = "{BE20250902AC0027}Particles/BER/BER_RoomFog.ptc";
	protected const ResourceName ROOM_FOG_WOOD = "{BE20250903AC0037}Particles/BER/BER_RoomFog_Wood.ptc";

	// one fog per room (keyed on the estimated room CENTER, so impacts on different walls
	// of the same room merge). Every impact contributes a WEIGHT scaled by the round's
	// caliber/destructive power (a 5.56 hit counts ~0.25, an autocannon round ~2.5) and by
	// what it struck (plaster/masonry full, wood a quarter — splinters, not dust; sheet
	// metal/glass nothing): the first fog appears once a room has accumulated enough
	// weight, and sustained fire stacks extra layers per weight step until visibility
	// really suffers. Once the entry ages out, continued fighting refreshes it from scratch.
	protected static ref array<vector> s_aFogPos = {};
	protected static ref array<float> s_aFogTime = {};
	protected static ref array<float> s_aFogHits = {};   // accumulated impact weight
	protected static ref array<int> s_aFogLayers = {};   // -1 = still accumulating, no fog spawned yet
	protected const float FOG_DEDUP_RADIUS_SQ = 12.25; // 3.5 m
	protected const float FOG_DEDUP_TIME = 20.0;
	protected const float FOG_SPAWN_WEIGHT = 1.0;      // accumulated weight before the first fog appears
	protected const float FOG_WEIGHT_PER_LAYER = 12.0; // further weight per extra fog layer
	protected const int FOG_MAX_EXTRA_LAYERS = 3;

	// TESTING: log the warhead-vs-impact-effect transform relationship so the deflection
	// axis choice is verified with real numbers in a live test — flip to false before publish
	protected const bool DIAG_DEFLECT = false;

	// TESTING: log every visual-shrapnel ray (hit distance, material, traced entity, decal
	// and particle results) — flip to false before publish
	protected const bool DIAG_FRAG = true;

	// Impact haze round-gate: a machinegun magazine into one wall must not stand up dozens
	// of overlapping 30-40 s haze effects — only every Nth impact keeps its long-lived haze
	// emitter (the authored haze alpha is raised xN in the .ptc so the summed occlusion of
	// massed fire is conserved), the rest get their haze birth zeroed while still inside its
	// 0.2 s emission window. This gate at SPAWN is the working merge for impacts: the cloud
	// field's 400 ms tick only ever sees them after emission ended, when StopEmission can no
	// longer absorb anything. The keep/suppress decision is remembered per effect for a few
	// seconds because neighbouring impact components adopt each other's fresh splashes — a
	// later adopter must repeat the first decision (its absolute-vs-original BIRTH_RATE
	// write would otherwise silently un-zero a suppressed haze), never roll a new one.
	protected static ref array<ParticleEffectEntity> s_aBerHazeGatePfx = {};
	protected static ref array<float> s_aBerHazeGateTime = {};
	protected static ref array<bool> s_aBerHazeGateKeep = {};
	protected static int s_iBerHazeRound = 0;
	protected const int HAZE_KEEP_EVERY_N = 5;
	protected const float HAZE_GATE_MEMORY = 3.0;
	// original lifetime above which an impact emitter is the appended long-lived haze
	// (vanilla emitters in the rebuilt Hit_*_enter_01 files all live well under 2 s)
	protected const float HAZE_ORIG_LIFETIME_MIN = 5.0;

	// original lifetime below which an emitter counts as flash/sparks and is only mildly scaled
	protected const float FLASH_LIFETIME_THRESHOLD = 0.6;

	// Visual shrapnel (m_iBerFragImpacts): the engine's ExplosionFragmentationEffect is
	// damage-only — no per-fragment visual exists anywhere in its configs — so the impacts
	// players expect to see around a frag grenade are traced here. Each ray that lands plays
	// the struck gamemat's HitEffectInfo bullet-hit particle plus its bullet-hole decal,
	// tuned like a 9x19 strike (same caliber formulas and haze round-gate as bullet impacts).
	protected const float FRAG_IMPACT_RANGE = 14.0;    // visual budget; damage reaches further but distant pocks read as unrelated
	protected const float FRAG_IMPACT_MIN_DIST = 0.7;  // hits inside the blast's own splash add nothing
	protected const float FRAG_DECAL_LIFETIME = 300.0; // seconds a shrapnel pock stays on the wall (tuning lever)
	protected const float FRAG_SHOT_SCALE = 0.2;       // 9x19 class — fragment hits read as pistol-caliber strikes

	protected ref array<ParticleEffectEntity> m_aProcessed = {};
	protected float m_fElapsed;
	protected float m_fScanAccum;
	protected int m_iQueriesDone;
	protected bool m_bTakeoverDone;
	protected bool m_bFragDone;
	protected bool m_bHullKickupDone;
	protected Vehicle m_KickupVehicle;

	// cached environment classification (computed once)
	protected bool m_bEnvComputed;
	protected float m_fDustFactor = 1.0;
	protected bool m_bIndoor;
	protected float m_fWindSpeed;
	protected vector m_vGroundNormal = vector.Up;
	protected bool m_bGroundFound;
	protected vector m_vGroundPos;
	protected string m_sGroundMat;
	protected float m_fMinWallDist = 100; // nearest wall around the detonation (indoors only)

	// dust-branch multipliers actually applied by the last TuneEmitters call — the drift
	// animator scales these down as the cloud travels (MultParam is absolute vs original)
	protected float m_fAppliedDensityMult = 1.0;
	protected float m_fAppliedLifetimeMult = 1.0;
	protected float m_fAppliedSizeMult = 1.0;

	// one cloud-field group per detonation: the central cloud and its own scattered
	// contact dust never merge with or shove each other — only with OTHER events' clouds
	protected int m_iBerCloudGroup;

	// set while ProcessAdopted handles an effect born at this hit's own impact point (same
	// 0.8 m gate as the deflection) — only such effects may take a NEW haze-gate decision;
	// remoter adoptions are neighbours' effects and only ever reuse a remembered one
	protected bool m_bBerAdoptOwnSplash;
	// the last TuneImpactEmitters call kept its haze emitter alive — gated impacts have
	// nothing long-lived left, so they stay out of the cloud field
	protected bool m_bBerHazeKept;

	// bullet impacts: matched shot's caliber weight + the material the shot actually
	// struck, resolved once per impact and reused by fog accumulation and dust tuning
	protected bool m_bBerImpactInfoDone;
	protected float m_fBerCalWeight = 0.8;      // no matched ray (fragments, unknown) = medium
	protected float m_fBerImpactDensity = 1.0;  // scales the impact's own dust density by caliber
	protected float m_fBerImpactSize = 1.0;     // scales the impact's particle size by caliber
	protected string m_sBerStruckMat;
	protected vector m_vBerShotDir;             // matched incoming shot direction (zero when unmatched)

	protected vector m_vQueryCenter;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (!m_bTakeoverDone && m_rTakeoverEffect != ResourceName.Empty)
		{
			m_bTakeoverDone = true;
			SpawnTakeoverEffect(owner);
		}

		// Visual shrapnel fires on the first frame the warhead exists (the prefab only ever
		// exists at the moment of detonation). It is deliberately NOT gated on the takeover:
		// the AT mines spawn their blast through the engine's distance-effect system instead
		// of a HitEffectComponent, so they carry no takeover effect, yet they fragment harder
		// than anything else in the game and must still pockmark their surroundings.
		// ComputeEnvironment is idempotent, so the takeover path above having already run it
		// costs nothing here.
		if (!m_bFragDone && m_iBerFragImpacts > 0)
		{
			m_bFragDone = true;
			ComputeEnvironment(owner);
			SpawnFragmentImpacts(owner);
		}

		// heavy rounds shake dust off a struck vehicle hull — resolved on the first frame
		// so the kickoff appears together with the impact itself
		if (!m_bHullKickupDone && m_fHullKickup > 0.01)
		{
			m_bHullKickupDone = true;
			TryHullKickup(owner);
		}

		m_fElapsed += timeSlice;
		m_fScanAccum += timeSlice;

		// warheads and bullet impacts classify their environment immediately, so indoor
		// tuning can still catch their dust while it is emitting; smoke grenades (long
		// scan) keep deferring — the device may still be carried around before use
		if (m_fScanDuration <= 30 && !m_bEnvComputed)
			ComputeEnvironment(owner);

		// the tuning must catch the scattered blast/impact dust while it is still
		// emitting (its emission window opens within ~50 ms) — query every frame for
		// the first moments instead of waiting for the 0.5 s pass. Indoors that covers
		// the whole room; outdoors, bullet impacts (no takeover) get a tight early
		// query so the impact splash can be deflected/tuned before it finishes emitting
		if (m_fScanDuration <= 30)
		{
			if (m_bIndoor && m_fElapsed <= 0.8)
			{
				m_vQueryCenter = owner.GetOrigin();
				owner.GetWorld().QueryEntitiesBySphere(m_vQueryCenter, 8.0, QueryParticleCallback, null, EQueryEntitiesFlags.ALL);
			}
			else if (!m_bIndoor && m_fElapsed <= 0.35 && m_rTakeoverEffect == ResourceName.Empty)
			{
				m_vQueryCenter = owner.GetOrigin();
				owner.GetWorld().QueryEntitiesBySphere(m_vQueryCenter, 3.0, QueryParticleCallback, null, EQueryEntitiesFlags.ALL);
			}
		}

		if (m_fScanAccum < 0.2)
			return;
		m_fScanAccum = 0;

		// while held/carried by a character nothing can have detonated yet — don't scan or age out
		IEntity parent = owner.GetParent();
		if (parent && ChimeraCharacter.Cast(parent))
		{
			m_fElapsed = 0;
			return;
		}

		ScanChildren(owner);

		// the surface dust effects are spawned by invisible collision particles scattered
		// around the blast zone — adopt everything close by, in two passes for late spawns
		if (m_fScanDuration <= 30)
		{
			if ((m_iQueriesDone == 0 && m_fElapsed > 0.5) || (m_iQueriesDone == 1 && m_fElapsed > 1.5))
			{
				m_iQueriesDone++;
				m_vQueryCenter = owner.GetOrigin();
				owner.GetWorld().QueryEntitiesBySphere(m_vQueryCenter, 8.0, QueryParticleCallback, null, EQueryEntitiesFlags.ALL);
			}
		}

		if (m_fElapsed > m_fScanDuration)
			ClearEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	//! Spawn the explosion effect ourselves, paused, tune, then play. The BER .ptc overrides
	//! already author WindInfluence 0 + LocalTransform 1, so wind exists only through the
	//! drift animator moving the effect entity.
	protected void SpawnTakeoverEffect(IEntity owner)
	{
		ComputeEnvironment(owner);

		// the blast shock rips accumulated dust off nearby thin layers and vehicle hulls
		BER_DustReservoir.RipArea(owner.GetWorld(), owner.GetOrigin(), 9.0, 1.0);

		// ...and its pressure wave sweeps standing particle clouds radially away from the
		// detonation — small-arms puffs and every drift-animated cloud within reach
		BER_MuzzleBlastDust.ShovePuffs(owner.GetWorld(), owner.GetOrigin(), 9.0, vector.Zero, 2.5);
		BER_WindDriftAnimator.GetInstance().ImpulseSweep(owner.GetOrigin(), 9.0, 2.5);

		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		spawnParams.PlayOnSpawn = false;
		// align the effect to the ground slope — with LocalTransform-authored emitters the
		// whole cloud follows, so hillside detonations no longer form a horizontal disk
		// sticking out of the slope
		if (m_vGroundNormal != vector.Zero && m_vGroundNormal != vector.Up)
			SCR_EntityHelper.OrientUpToVector(m_vGroundNormal, spawnParams.Transform);
		spawnParams.Transform[3] = owner.GetOrigin();

		ParticleEffectEntity pfx = ParticleEffectEntity.SpawnParticleEffect(m_rTakeoverEffect, spawnParams);
		if (!pfx)
			return;

		m_aProcessed.Insert(pfx);
		BER_OwnedEffects.MarkOwned(pfx);

		Particles particles = pfx.GetParticles();
		if (particles)
			TuneEmitters(particles);

		pfx.Play();

		if (!m_bIndoor && m_fWindRampTime > 0.05)
			BER_WindDriftAnimator.GetInstance().Register(pfx, m_fWindRampTime, m_fAppliedDensityMult, m_fAppliedLifetimeMult);

		// the lingering cloud takes part in the overlap field: crowded clouds shoulder
		// each other apart, deeply overlapping ones merge (density-only indoors)
		if (m_iBerCloudGroup == 0)
			m_iBerCloudGroup = BER_CloudField.NewGroup();
		BER_CloudField.GetInstance().Register(pfx, BER_CloudField.FAMILY_EXPLOSION, m_bIndoor, m_iBerCloudGroup, m_fAppliedDensityMult, m_fAppliedSizeMult, m_fAppliedLifetimeMult);

		if (m_fDebrisScale > 0.01)
			SpawnImpactDebris();

		// the concussion of an indoor detonation shakes structural dust out of the whole
		// room — a lingering fog on top of the explosion's own dust (not when it went off
		// on a material that sheds none, e.g. a metal floor). Explosions carry enough
		// weight to raise the fog at once and advance the layer buildup fast.
		if (m_bIndoor && !IsDustExemptMaterial(m_sGroundMat))
			SpawnRoomFog(owner, 1.0, 3.0, FogVariantFor(m_sGroundMat));
	}

	//------------------------------------------------------------------------------------------------
	//! Fill the room with slow structural dust fog (constant opacity for 30+ s, authored).
	//! The fog sits at the estimated ROOM CENTER, not the impact point, so it spreads evenly
	//! through the room instead of hugging the wall that was hit. Deduplicated per room —
	//! and every impact contributes its caliber/material WEIGHT: the first fog appears once
	//! the room has accumulated FOG_SPAWN_WEIGHT (heavy rounds and explosions immediately,
	//! a 5.56 rifle only after several hits), and sustained fire stacks extra layers per
	//! FOG_WEIGHT_PER_LAYER of further weight, so strafing a room with a machine gun builds
	//! real visibility loss at a quarter of the rate for the small calibers.
	protected void SpawnRoomFog(IEntity owner, float strength, float weight, ResourceName fogRes)
	{
		BaseWorld world = owner.GetWorld();
		vector pos = owner.GetOrigin();
		float now = world.GetWorldTime() * 0.001;

		vector roomCenter;
		float roomHalf;
		BER_SurfaceUtil.GetRoomGeometry(world, pos, owner, 9.0, roomCenter, roomHalf);

		// the fog must APPEAR where the hit happened, not float in mid-room: spawn pulled
		// back from the impact point into the room — along the incoming shot when known,
		// toward the room's free center otherwise. Dedup stays keyed on the room center
		// so hits on different walls of one room still merge into one buildup.
		vector fogPos = pos;
		if (m_vBerShotDir != vector.Zero)
		{
			fogPos = pos - m_vBerShotDir * 1.4;
		}
		else
		{
			vector toCenter = roomCenter - pos;
			float len = toCenter.Length();
			if (len > 0.3)
			{
				float pull = 1.4;
				if (pull > len)
					pull = len;
				fogPos = pos + toCenter * (pull / len);
			}
		}

		for (int i = s_aFogTime.Count() - 1; i >= 0; i--)
		{
			if (now - s_aFogTime[i] > FOG_DEDUP_TIME)
			{
				s_aFogTime.Remove(i);
				s_aFogPos.Remove(i);
				s_aFogHits.Remove(i);
				s_aFogLayers.Remove(i);
				continue;
			}
			if (vector.DistanceSq(s_aFogPos[i], roomCenter) < FOG_DEDUP_RADIUS_SQ)
			{
				s_aFogHits[i] = s_aFogHits[i] + weight;

				// still accumulating toward the first fog
				if (s_aFogLayers[i] < 0)
				{
					if (s_aFogHits[i] >= FOG_SPAWN_WEIGHT && SpawnFogEntity(fogPos, strength, roomHalf, fogRes))
						s_aFogLayers[i] = 0;
					return;
				}

				// fog standing — continued fire thickens it in weight steps, each new
				// layer rising where the CURRENT fire is landing
				if (s_aFogLayers[i] < FOG_MAX_EXTRA_LAYERS
					&& s_aFogHits[i] >= FOG_SPAWN_WEIGHT + (s_aFogLayers[i] + 1) * FOG_WEIGHT_PER_LAYER)
				{
					s_aFogLayers[i] = s_aFogLayers[i] + 1;
					SpawnFogEntity(fogPos, 0.45, roomHalf, fogRes);
				}
				return;
			}
		}

		// new room entry — heavy rounds raise the fog at once, light rounds accumulate
		int layers = -1;
		if (weight >= FOG_SPAWN_WEIGHT && SpawnFogEntity(fogPos, strength, roomHalf, fogRes))
			layers = 0;

		s_aFogPos.Insert(roomCenter);
		s_aFogTime.Insert(now);
		s_aFogHits.Insert(weight);
		s_aFogLayers.Insert(layers);
	}

	//------------------------------------------------------------------------------------------------
	//! How much structural dust a struck material sheds into the room, by easily researched
	//! particulate logic: plaster/masonry/concrete/brick = lots of fine dust; bare stone a
	//! bit less; dirt floors plenty; WOOD splinters but powders very little; sheet metal,
	//! glass, plastic and the like (the typewriter) shed none at all. Unknown/no-ray
	//! defaults to full — interiors are mostly masonry.
	protected static float FogMaterialWeight(string matName)
	{
		if (matName == "")
			return 1.0;
		if (IsDustExemptMaterial(matName))
			return 0;

		matName.ToLower();
		if (matName.Contains("wood"))
			return 0.25;
		if (matName.Contains("stone") || matName.Contains("rock"))
			return 0.9;
		if (matName.Contains("dirt") || matName.Contains("soil"))
			return 1.1;
		return 1.0;
	}

	//------------------------------------------------------------------------------------------------
	protected ResourceName FogVariantFor(string matName)
	{
		string m = matName;
		m.ToLower();
		if (m.Contains("wood"))
			return ROOM_FOG_WOOD;
		return ROOM_FOG;
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve the material the shot actually STRUCK (wall, not the floor below the
	//! warhead) by tracing the given shot ray through the impact point. Empty when no
	//! surface can be resolved.
	protected string GetStruckMaterial(IEntity owner, vector shotDir)
	{
		BaseWorld world = owner.GetWorld();

		TraceParam tp = new TraceParam();
		tp.Start = owner.GetOrigin() - shotDir * 0.5;
		tp.End = owner.GetOrigin() + shotDir * 0.4;
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		tp.Exclude = owner;

		if (world.TraceMove(tp, null) >= 1.0 || !tp.SurfaceProps)
			return "";

		return tp.SurfaceProps.GetName();
	}

	//------------------------------------------------------------------------------------------------
	//! Resolve, once per impact, what round hit (matched shot ray -> weapon-class scale ->
	//! caliber weight, tuned so 5.56 builds structural dust at ~25% of the old rate) and
	//! what material it struck. Both feed the fog accumulation and the impact's own
	//! indoor dust density.
	protected void ResolveImpactInfo(IEntity owner)
	{
		if (m_bBerImpactInfoDone)
			return;
		m_bBerImpactInfoDone = true;

		vector shotDir;
		float shotScale;
		if (!BER_MuzzleBlastDust.GetIncomingShotInfo(owner.GetWorld(), owner.GetOrigin(), shotDir, shotScale))
		{
			shotScale = 0.65; // no matched ray (fragments, unknown) — mid-sized
		}
		else
		{
			// (scale/0.72)^2: 5.56 (0.36) -> 0.25, 7.62 (0.42) -> 0.34, pistols floor,
			// 12.7+ (1.2+) capped — destructive power rises superlinearly with caliber
			m_fBerCalWeight = BER_SurfaceUtil.ClampF(Math.Pow(shotScale / 0.72, 2.0), 0.08, 2.5);
			m_sBerStruckMat = GetStruckMaterial(owner, shotDir);
			m_vBerShotDir = shotDir;
		}

		// direct caliber multiplier: a 5.56 hole sheds a quarter of the baseline dust,
		// pistols less, heavy rounds more (the old 0.5-floored blend left small arms
		// generating far too much per hit)
		m_fBerImpactDensity = BER_SurfaceUtil.ClampF(m_fBerCalWeight, 0.12, 1.5);

		// particle size follows the hole that was punched, LINEAR in caliber (a 5.56
		// puff ~0.4x, pistols smaller still, .50+ up to 1.3x) — the generalized room fog
		// is left to the cloud field MERGING nearby impacts together, not to any single
		// impact being oversized
		m_fBerImpactSize = BER_SurfaceUtil.ClampF(1.15 * shotScale, 0.25, 1.3);

		string sm = m_sBerStruckMat;
		sm.ToLower();
		if (sm.Contains("wood"))
			m_fBerImpactDensity = m_fBerImpactDensity * 0.6; // splinters, not a dust cloud
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsDustExemptMaterial(string matName)
	{
		matName.ToLower();
		return matName.Contains("metal") || matName.Contains("armor") || matName.Contains("glass")
			|| matName.Contains("plastic") || matName.Contains("rubber") || matName.Contains("fabric")
			|| matName.Contains("aramid") || matName.Contains("flesh") || matName.Contains("water");
	}

	//------------------------------------------------------------------------------------------------
	protected bool SpawnFogEntity(vector center, float strength, float roomHalf, ResourceName fogRes)
	{
		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		spawnParams.PlayOnSpawn = false;
		spawnParams.Transform[3] = center;

		ParticleEffectEntity pfx = ParticleEffectEntity.SpawnParticleEffect(fogRes, spawnParams);
		if (!pfx)
			return false;

		BER_OwnedEffects.MarkOwned(pfx);

		Particles particles = pfx.GetParticles();
		if (particles)
		{
			// particle size follows the room's free half-extent (measured mid-room, so a
			// detonation near a wall of a big hall still gets hall-sized fog)
			float roomSize = BER_SurfaceUtil.ClampF(roomHalf / 4.0, 0.55, 1.3);
			float sizeMult = roomSize * BER_SurfaceUtil.ClampF(strength, 0.4, 1.0);
			int emitterCount = particles.GetNumEmitters();
			for (int i = 0; i < emitterCount; i++)
				particles.MultParam(i, EmitterParam.SIZE, sizeMult);
		}

		pfx.Play();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Physical ground debris (3D dirt clumps / stone chips) thrown by the detonation,
	//! picked by the struck surface and scaled per warhead: count scales linearly with
	//! m_fDebrisScale, chunk size sub-linearly (a rocket throws many more clods than a
	//! 25mm hit, each somewhat bigger — not boulders).
	protected void SpawnImpactDebris()
	{
		if (!m_bGroundFound)
			return; // airburst — nothing to rip out of the ground

		string mat = m_sGroundMat;
		mat.ToLower();

		if (mat.Contains("water") || mat.Contains("seaweed") || mat.Contains("snow") || mat.Contains("ice")
			|| mat.Contains("metal") || mat.Contains("armor") || mat.Contains("wood"))
			return;

		ResourceName res = DEBRIS_DIRT;
		if (mat.Contains("stone") || mat.Contains("rock") || mat.Contains("concrete") || mat.Contains("asphalt")
			|| mat.Contains("brick") || mat.Contains("cobble") || mat.Contains("tiles")
			|| mat.Contains("gravel") || mat.Contains("pebbles"))
			res = DEBRIS_ROCK;

		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		spawnParams.PlayOnSpawn = false;
		if (m_vGroundNormal != vector.Zero && m_vGroundNormal != vector.Up)
			SCR_EntityHelper.OrientUpToVector(m_vGroundNormal, spawnParams.Transform);
		spawnParams.Transform[3] = m_vGroundPos;

		ParticleEffectEntity pfx = ParticleEffectEntity.SpawnParticleEffect(res, spawnParams);
		if (!pfx)
			return;

		BER_OwnedEffects.MarkOwned(pfx);

		Particles particles = pfx.GetParticles();
		if (particles)
		{
			float sizeMult = BER_SurfaceUtil.ClampF(0.75 + 0.25 * m_fDebrisScale, 0.6, 1.8);
			int emitterCount = particles.GetNumEmitters();
			for (int i = 0; i < emitterCount; i++)
			{
				particles.MultParam(i, EmitterParam.BIRTH_RATE, m_fDebrisScale);
				particles.MultParam(i, EmitterParam.SIZE, sizeMult);
			}
		}

		pfx.Play();
	}

	//------------------------------------------------------------------------------------------------
	//! Uniform random direction over the sphere without trigonometry (Math.Sin/Cos are
	//! unreliable in script components) — rejection-sample the unit ball.
	protected vector RandomSphereDir()
	{
		for (int tries = 0; tries < 16; tries++)
		{
			vector cand = Vector(Math.RandomFloat(-1, 1), Math.RandomFloat(-1, 1), Math.RandomFloat(-1, 1));
			float lsq = vector.Dot(cand, cand);
			if (lsq < 0.02 || lsq > 1.0)
				continue;
			cand.Normalize();
			return cand;
		}
		return vector.Up;
	}

	//------------------------------------------------------------------------------------------------
	//! Visual shrapnel: trace m_iBerFragImpacts random rays out of the detonation; every
	//! surface hit plays the shrapnel wisp for the struck gamemat — the bullet-impact wisp
	//! of that material's Hit_<mat>_enter_01, extracted on its own and shrunk to a third
	//! (BER_FragHit_<mat>.ptc). Sized in data, so it does NOT go through the caliber tuning
	//! and carries no haze emitter to round-gate. Spawns are MarkOwned so no impact
	//! component re-adopts or re-tunes them. No decal: real fragmentation leaves nothing
	//! that reads as the 9 mm bullet-hole material, and the pock was invisible on terrain
	//! anyway (user ruling, pass 57).
	protected void SpawnFragmentImpacts(IEntity owner)
	{
		BaseWorld world = owner.GetWorld();

		if (DIAG_FRAG)
			PrintFormat("BER DIAG frag: START rays=%1 origin=%2", m_iBerFragImpacts, owner.GetOrigin());

		// fragments leave the casing above ground level, not out of the soil
		vector origin = owner.GetOrigin() + Vector(0, 0.18, 0);

		for (int i = 0; i < m_iBerFragImpacts; i++)
		{
			vector dir = RandomSphereDir();

			TraceParam tp = new TraceParam();
			tp.Start = origin;
			tp.End = origin + dir * FRAG_IMPACT_RANGE;
			tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
			tp.Exclude = owner;

			float frac = world.TraceMove(tp, null);
			if (frac >= 1.0)
				continue;

			float dist = FRAG_IMPACT_RANGE * frac;
			if (dist < FRAG_IMPACT_MIN_DIST)
				continue;

			GameMaterial mat = tp.SurfaceProps;
			if (!mat)
				continue;

			string matName = mat.GetName();
			matName.ToLower();
			if (matName.Contains("water") || matName.Contains("flesh"))
				continue; // no pockmarks on water or people

			HitEffectInfo hitInfo = mat.GetHitEffectInfo();
			if (!hitInfo)
				continue;

			vector hitPos = origin + dir * dist;
			vector up = tp.TraceNorm;

			// ⚠ the HitEffectInfo getters are CROSS-WIRED in the engine: GetParticleEffectValue()
			// returns the DECAL .emat and GetDecalMaterialValue() returns the bullet-hit .ptc
			// (proven with real logged values — 51st-pass DIAG_FRAG: every gamemat came back
			// with the two resources swapped). Select by file extension so this keeps working
			// even if a future game update fixes the wiring. Only the .ptc's material key is
			// used here — it picks the matching shrapnel wisp.
			ResourceName ptcRes = hitInfo.GetDecalMaterialValue();
			string extProbe = ptcRes;
			if (extProbe.Contains(".emat"))
				ptcRes = hitInfo.GetParticleEffectValue();
			ResourceName fragRes = ResolveFragHitEffect(ptcRes);

			// shrapnel wisp, oriented out of the surface like the engine spawns bullet hits
			ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
			spawnParams.UseFrameEvent = true;
			if (up != vector.Zero)
				SCR_EntityHelper.OrientUpToVector(up, spawnParams.Transform);
			spawnParams.Transform[3] = hitPos;

			ParticleEffectEntity pfx = ParticleEffectEntity.SpawnParticleEffect(fragRes, spawnParams);
			if (DIAG_FRAG)
			{
				string entName = "NULL";
				if (tp.TraceEnt)
					entName = tp.TraceEnt.ClassName();
				PrintFormat("BER DIAG frag: hit d=%1 mat=%2 ent=%3 hitPtc=%4 frag=%5 pfx=%6", dist, matName, entName, ptcRes, fragRes, pfx != null);
			}
			if (!pfx)
				continue;

			BER_OwnedEffects.MarkOwned(pfx);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Shrapnel wisp for the material whose bullet-hit effect is hitPtc ("…/Hit_<mat>_enter_01.ptc").
	//! Materials that have no wisp of their own (flesh, water, foliage) use the default one.
	protected static ResourceName ResolveFragHitEffect(ResourceName hitPtc)
	{
		string path = hitPtc;
		int a = path.IndexOf("Hit_");
		int b = path.IndexOf("_enter_01");
		if (a >= 0 && b > a + 4)
		{
			string mat = path.Substring(a + 4, b - a - 4);
			int idx = FRAG_HIT_MATS.Find(mat);
			if (idx >= 0)
				return FRAG_HIT_EFFECTS[idx];
		}
		return FRAG_HIT_DEFAULT;
	}

	//------------------------------------------------------------------------------------------------
	//! Find the vehicle this warhead detonated on/next to and shake dust off its hull.
	//! The warhead entity may be parented to the struck entity; otherwise a small sphere
	//! query around the impact point finds it.
	protected void TryHullKickup(IEntity owner)
	{
		Vehicle veh = null;
		IEntity parent = owner.GetParent();
		if (parent)
			veh = Vehicle.Cast(parent.GetRootParent());

		if (!veh)
		{
			m_KickupVehicle = null;
			owner.GetWorld().QueryEntitiesBySphere(owner.GetOrigin(), 2.2, VehicleQueryCallback, null, EQueryEntitiesFlags.ALL);
			veh = m_KickupVehicle;
		}
		if (!veh)
			return;

		BER_MuzzleBlastDust.KickoffOnVehicle(veh, owner.GetOrigin(), m_fHullKickup);
	}

	//------------------------------------------------------------------------------------------------
	protected bool VehicleQueryCallback(IEntity ent)
	{
		Vehicle veh = Vehicle.Cast(ent);
		if (!veh)
			return true;
		m_KickupVehicle = veh;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void ScanChildren(IEntity owner)
	{
		IEntity child = owner.GetChildren();
		while (child)
		{
			ParticleEffectEntity pfx = ParticleEffectEntity.Cast(child);
			if (pfx && m_aProcessed.Find(pfx) == -1)
			{
				m_aProcessed.Insert(pfx);
				ProcessAdopted(pfx, owner);
			}
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool QueryParticleCallback(IEntity ent)
	{
		ParticleEffectEntity pfx = ParticleEffectEntity.Cast(ent);
		if (!pfx || m_aProcessed.Find(pfx) != -1)
			return true;

		// only adopt effects that belong to this blast zone
		if (vector.DistanceSq(pfx.GetOrigin(), m_vQueryCenter) > 36.0)
			return true;

		m_aProcessed.Insert(pfx);
		ProcessAdopted(pfx, GetOwner());
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Adopted effects near a takeover explosion are the scattered surface blast dust with
	//! BER-overridden .ptc (wind-free, local-space) — drift-animating them gives the same
	//! hold + wind acceleration. Drift is gated to takeover warheads ONLY: smoke grenades /
	//! smoke shells attach their emitter to the device and moving it would detach the
	//! smoke origin. Parented effects are never drifted for the same reason.
	protected void ProcessAdopted(ParticleEffectEntity pfx, IEntity owner)
	{
		// never adopt effects BER itself spawned — they are tuned at spawn by whoever
		// spawned them; re-adoption would re-boost lifetimes, double-register drift or
		// reorient effects that were placed deliberately
		if (BER_OwnedEffects.IsOwned(pfx))
			return;

		Particles particles = pfx.GetParticles();
		if (!particles)
			return;

		ComputeEnvironment(owner);

		// bullet impacts: resolve the matched round's caliber weight and the struck
		// material once, then deflect the splash away from the impact angle — the engine
		// spawns it at right angles to the surface no matter where the shot came from
		if (m_rTakeoverEffect == ResourceName.Empty && !m_bIndoorSmokeSwap && m_bScaleBySurface)
		{
			ResolveImpactInfo(owner);
			DeflectImpactSplash(pfx, owner);
		}

		m_bBerAdoptOwnSplash = vector.DistanceSq(pfx.GetOrigin(), owner.GetOrigin()) <= 0.64;
		m_bBerHazeKept = false;

		// smoke grenade under a roof: stop the vanilla wind-coupled plume (its few
		// already-born particles fade on their own curves) and restart the same smoke as
		// the windless BER variant — the screen stays in the room and lingers
		if (m_bIndoorSmokeSwap && m_bIndoor)
		{
			ParticleEffectEntity swapped = SwapToIndoorVariant(pfx, owner);
			if (swapped)
			{
				m_aProcessed.Insert(swapped);
				pfx = swapped;
				particles = pfx.GetParticles();
				if (!particles)
					return;
			}
		}

		TuneEmitters(particles, pfx);

		// bullet impacts spawn NO extra fog entity anymore — the intended behavior is
		// driven entirely through the vanilla impact particle (long-lived wisps that
		// accumulate and merge via the cloud field into the generalized haze). Only
		// explosions still shake a room fog loose (takeover path).

		// (the smoke plume's under-floor birth fix lives in the authored .ptc now: the
		// emission point sits at the fuze opening — Offset 0 0.08 0 along the device's
		// body axis — instead of the old unverified runtime EMITOFFSET push)

		if (m_rTakeoverEffect != ResourceName.Empty && !pfx.GetParent() && !m_bIndoor && m_fWindRampTime > 0.05)
			BER_WindDriftAnimator.GetInstance().Register(pfx, m_fWindRampTime, m_fAppliedDensityMult, m_fAppliedLifetimeMult);

		// adopted impact/contact dust joins the overlap field (same group as this event's
		// own clouds): repeated fire into one spot merges its standing dust — density-only
		// indoors, so the cloud thickens without ever outgrowing the room — instead of
		// stacking ever more overlapping effects. Grenade smoke stays out: suppressing a
		// colored signal plume would alter gameplay, not just visuals. Bullet impacts whose
		// haze the round-gate suppressed carry nothing that outlives the vanilla splash —
		// keeping them out of the field is what keeps its pair scan cheap under a mag dump.
		bool registerField = !pfx.GetParent() && !m_bIndoorSmokeSwap && m_bScaleBySurface;
		if (registerField && m_rTakeoverEffect == ResourceName.Empty && !m_bBerHazeKept)
			registerField = false;
		if (registerField)
		{
			if (m_iBerCloudGroup == 0)
				m_iBerCloudGroup = BER_CloudField.NewGroup();
			BER_CloudField.GetInstance().Register(pfx, BER_CloudField.FAMILY_IMPACT, m_bIndoor, m_iBerCloudGroup, m_fAppliedDensityMult, m_fAppliedSizeMult, m_fAppliedLifetimeMult);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Reorient a freshly adopted bullet-impact splash along the deflection of the shot:
	//! R = D - 2(D.N)N off the surface plane, with a slight lift along the normal. Newly
	//! born particles (the effect is caught inside its ~0.2 s emission window) then splash
	//! away from the impact angle — the wall dust dragged along the ricochet — instead of
	//! jetting straight out of the surface.
	//! The shot direction comes from the muzzle hook's recent-shot rays (the impact point
	//! is matched against the ray it lies on). The warhead entity's own transform proved
	//! USELESS for this — 19th-pass diag logging showed its forward axis pointing straight
	//! up or along the struck building's grid, never along the shot.
	protected void DeflectImpactSplash(ParticleEffectEntity pfx, IEntity owner)
	{
		if (pfx.GetParent())
			return;

		// only the splash born at the impact point itself — anything farther away is
		// scattered secondary dust, not this hit's directional splash
		if (vector.DistanceSq(pfx.GetOrigin(), owner.GetOrigin()) > 0.64)
			return;

		vector d;
		if (!BER_MuzzleBlastDust.GetIncomingShotDir(owner.GetWorld(), owner.GetOrigin(), d))
		{
			if (DIAG_DEFLECT)
				Print("BER DIAG deflect: no shot ray matched this impact");
			return;
		}

		vector pfxMat[4];
		pfx.GetWorldTransform(pfxMat);
		vector n = pfxMat[1]; // engine orients the splash's emission axis along the surface normal

		float dot = vector.Dot(d, n);

		if (DIAG_DEFLECT)
			PrintFormat("BER DIAG deflect: pfxUp=%1 shotDir=%2 dot=%3", n, d, dot);

		// the shot must actually point into the surface (a ray that merely passes nearby
		// on its way somewhere else fails this) — leave the effect untouched otherwise
		if (dot > -0.15)
			return;

		vector r = d - n * (2.0 * dot);
		r = r + n * 0.12; // slight lift off the surface so a grazing splash doesn't hug the wall
		r.Normalize();

		SCR_EntityHelper.OrientUpToVector(r, pfxMat);
		pfx.SetWorldTransform(pfxMat);
	}

	//------------------------------------------------------------------------------------------------
	protected ParticleEffectEntity SwapToIndoorVariant(ParticleEffectEntity pfx, IEntity owner)
	{
		ResourceName res = GetIndoorSmokeVariant(owner);
		if (res == ResourceName.Empty)
			return null;

		pfx.StopEmission();

		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		// the emitter must stay stuck to the grenade for its whole emitting life — the swap
		// can happen while the device is still flying/rolling, and an unparented emitter
		// then keeps pouring smoke at the point in mid-air where the swap happened.
		// With FollowParent the spawn Transform is interpreted LOCAL to the followed entity
		// (TransformMode defaults to LOCAL — vanilla passes local offsets here); keep the
		// identity transform so the effect sits exactly on the device. Passing the world
		// transform instead composes device x world = the effect lands kilometers off-map
		// and no smoke ever appears (the 19th-pass indoor-smoke regression).
		spawnParams.FollowParent = owner;

		ParticleEffectEntity swapped = ParticleEffectEntity.SpawnParticleEffect(res, spawnParams);
		BER_OwnedEffects.MarkOwned(swapped);
		return swapped;
	}

	//------------------------------------------------------------------------------------------------
	//! The engine gives no way to read which .ptc an adopted effect was spawned from, so the
	//! windless variant is resolved from the grenade's own prefab name.
	protected ResourceName GetIndoorSmokeVariant(IEntity owner)
	{
		EntityPrefabData prefabData = owner.GetPrefabData();
		if (!prefabData)
			return ResourceName.Empty;

		string path = prefabData.GetPrefabName();

		if (path.Contains("Smoke_M18_Red_Repeating"))
			return "{BE20250902AC0014}Particles/BER/BER_SmokeIndoor_M18_Red_Repeat.ptc";
		if (path.Contains("Smoke_M18_Violet_Repeating"))
			return "{BE20250902AC0015}Particles/BER/BER_SmokeIndoor_M18_Purple_Repeating.ptc";
		if (path.Contains("Smoke_M18_Red"))
			return "{BE20250902AC0011}Particles/BER/BER_SmokeIndoor_M18_Red.ptc";
		if (path.Contains("Smoke_M18_Violet"))
			return "{BE20250902AC0013}Particles/BER/BER_SmokeIndoor_M18_Purple.ptc";
		if (path.Contains("Smoke_M18_Yellow"))
			return "{BE20250902AC0012}Particles/BER/BER_SmokeIndoor_M18_Yellow.ptc";
		if (path.Contains("ANM8"))
			return "{BE20250902AC0016}Particles/BER/BER_SmokeIndoor_AN-M8_HC.ptc";
		if (path.Contains("RDG2"))
			return "{BE20250902AC0017}Particles/BER/BER_SmokeIndoor_RDG-2.ptc";
		if (path.Contains("Smoke_M18"))
			return "{BE20250902AC0010}Particles/BER/BER_SmokeIndoor_M18_Green.ptc"; // base default

		return ResourceName.Empty; // unknown (modded) smoke — leave the vanilla effect alone
	}

	//------------------------------------------------------------------------------------------------
	//! Shared surface-scaled emitter tuning; requires ComputeEnvironment to have run.
	//! pfx is only needed on the bullet-impact branch (haze round-gate bookkeeping).
	protected void TuneEmitters(Particles particles, ParticleEffectEntity pfx = null)
	{
		if (!m_bScaleBySurface)
			return;

		int emitterCount = particles.GetNumEmitters();
		if (emitterCount <= 0)
			return;

		// bullet impacts drive the intended behavior through the vanilla-shaped .ptc
		// itself (rebuilt with long-lived, spawn-bright wisps) — runtime only modulates
		// caliber, wetness and environment lifetime. The full surface/density/emission
		// boost stack below is what let one 5.56 hit envelop a hallway.
		if (m_rTakeoverEffect == ResourceName.Empty && !m_bIndoorSmokeSwap)
		{
			TuneImpactEmitters(particles, emitterCount, pfx);
			return;
		}

		// travel factor: a cloud the wind will carry dissipates on the way; still air
		// (indoors, becalmed) lets it stand and linger as a gameplay-relevant screen —
		// structures barely ventilate, so indoor effects hang around much longer
		float travelLife = 1.0;
		float travelEmit = 1.0;
		float travelDensity = 1.0;
		if (m_bIndoor)
		{
			travelLife = 2.2;
			travelEmit = 1.5;

		}
		else
		{
			float windNorm = BER_SurfaceUtil.ClampF(m_fWindSpeed / 10.0, 0, 1);
			travelLife = 1.0 - 0.45 * windNorm;
			travelEmit = 1.0 - 0.3 * windNorm;
		}

		float densityMult = BER_SurfaceUtil.ClampF(m_fDensityBoost * m_fDustFactor * travelDensity, 0.4, 3.5);

		// wet (or inherently dustless) ground generates no dust at all — the usual clamp
		// floor would keep 40% of the cloud alive, so it is faded out below the floor here
		if (m_fDustFactor < 0.35)
			densityMult = densityMult * (m_fDustFactor / 0.35);

		float lifetimeMult = BER_SurfaceUtil.ClampF(m_fLifetimeBoost * m_fDustFactor * travelLife, 0.4, 6.5);
		float emissionMult = BER_SurfaceUtil.ClampF(m_fEmissionBoost * m_fDustFactor * travelEmit, 1.0, 4.0);
		float sizeMult = BER_SurfaceUtil.ClampF(m_fSizeBoost * (1.0 + 0.12 * (m_fDustFactor - 1.0)), 0.5, 2.2);


		m_fAppliedDensityMult = densityMult;
		m_fAppliedLifetimeMult = lifetimeMult;
		m_fAppliedSizeMult = sizeMult;

		float origLifetime;
		vector origShape;
		for (int i = 0; i < emitterCount; i++)
		{
			origLifetime = 0;
			particles.GetParamOrig(i, EmitterParam.LIFETIME, origLifetime);

			if (origLifetime < FLASH_LIFETIME_THRESHOLD)
			{
				// flash/sparks/fireball — mild scaling only, fire must not linger
				particles.MultParam(i, EmitterParam.BIRTH_RATE, 1.0 + (densityMult - 1.0) * 0.5);
				particles.MultParam(i, EmitterParam.LIFETIME, 1.0 + (lifetimeMult - 1.0) * 0.25);
				particles.MultParam(i, EmitterParam.SIZE, 1.0 + (sizeMult - 1.0) * 0.5);
			}
			else
			{
				// dust/smoke — full boost plus extended emission for a plateau-then-collapse profile
				particles.MultParam(i, EmitterParam.BIRTH_RATE, densityMult);
				particles.MultParam(i, EmitterParam.LIFETIME, lifetimeMult);
				particles.MultParam(i, EmitterParam.LIFETIME_RND, lifetimeMult);
				particles.MultParam(i, EmitterParam.EMITTING_TIME, emissionMult);
				particles.MultParam(i, EmitterParam.SIZE, sizeMult);

				// indoors an explosion's overpressure has nowhere to go — full-sphere
				// dispersion so the gas fills the room evenly instead of jetting in one
				// authored cone. Bullet impacts are exempt: their splash is deliberately
				// deflected along the shot's ricochet direction instead.
				if (m_bIndoor && m_rTakeoverEffect != ResourceName.Empty)
					particles.SetParam(i, EmitterParam.CONEANGLE, Vector(360, 0, 180));

				// indoors: slow the ejection to the room size so the dust spreads out and
				// hangs instead of the whole cloud smacking into the nearest wall
				if (m_bIndoor)
					particles.MultParam(i, EmitterParam.VELOCITY, BER_SurfaceUtil.ClampF(m_fMinWallDist / 8.0, 0.35, 0.9));
			}

			// indoors: shrink birth volumes wider than the room so no particle is born on
			// the far side of a wall (blast dust boxes span up to 10 m)
			if (m_bIndoor)
			{
				origShape = vector.Zero;
				particles.GetParamOrig(i, EmitterParam.SHAPE_SIZE, origShape);
				float halfExtent = origShape[0];
				if (origShape[2] > halfExtent)
					halfExtent = origShape[2];
				halfExtent = halfExtent * 0.5;
				if (halfExtent > 0.05 && halfExtent > m_fMinWallDist)
					particles.MultParam(i, EmitterParam.SHAPE_SIZE, m_fMinWallDist / halfExtent);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Minimal tuning for a bullet impact's own effect: density and particle size follow
	//! the round that hit (both near-vanilla for the heavies, well below for 5.56 and
	//! pistols), wet surfaces shed nothing, and an unventilated room lets the wisps hang
	//! a little longer while outdoor wind thins them. Nothing else — the look itself is
	//! authored in the rebuilt .ptc.
	protected void TuneImpactEmitters(Particles particles, int emitterCount, ParticleEffectEntity pfx)
	{
		float density = m_fBerImpactDensity;
		if (m_fDustFactor < 0.35)
			density = density * (m_fDustFactor / 0.35);

		float life = 1.0;
		if (m_bIndoor)
			life = 1.4;
		else
			life = 1.0 - 0.45 * BER_SurfaceUtil.ClampF(m_fWindSpeed / 10.0, 0, 1);

		m_fAppliedDensityMult = density;
		m_fAppliedLifetimeMult = life;
		m_fAppliedSizeMult = m_fBerImpactSize;

		// haze round-gate (see the statics up top): a NEW decision only for this hit's own
		// splash; an effect already in the gate memory reuses whatever was decided for it.
		// Effects that are neither (an older neighbour swept up by the adoption query) get
		// no haze write at all — their haze finished emitting long ago either way.
		bool keepHaze = true;
		bool hazeDecided = false;
		if (pfx)
		{
			float now = GetGame().GetWorld().GetWorldTime() * 0.001;
			for (int g = s_aBerHazeGatePfx.Count() - 1; g >= 0; g--)
			{
				if (!s_aBerHazeGatePfx[g] || now - s_aBerHazeGateTime[g] > HAZE_GATE_MEMORY)
				{
					s_aBerHazeGatePfx.Remove(g);
					s_aBerHazeGateTime.Remove(g);
					s_aBerHazeGateKeep.Remove(g);
				}
			}

			int gateIdx = s_aBerHazeGatePfx.Find(pfx);
			if (gateIdx != -1)
			{
				keepHaze = s_aBerHazeGateKeep[gateIdx];
				hazeDecided = true;
			}
			else if (m_bBerAdoptOwnSplash)
			{
				s_iBerHazeRound++;
				// == 1 keeps the 1st, 6th, 11th... round's haze — a lone shot still answers
				keepHaze = (s_iBerHazeRound % HAZE_KEEP_EVERY_N) == 1;
				hazeDecided = true;
				s_aBerHazeGatePfx.Insert(pfx);
				s_aBerHazeGateTime.Insert(now);
				s_aBerHazeGateKeep.Insert(keepHaze);
			}
		}
		m_bBerHazeKept = hazeDecided && keepHaze;

		float origLifetime;
		for (int i = 0; i < emitterCount; i++)
		{
			origLifetime = 0;
			particles.GetParamOrig(i, EmitterParam.LIFETIME, origLifetime);
			if (origLifetime > HAZE_ORIG_LIFETIME_MIN && hazeDecided && !keepHaze)
			{
				particles.MultParam(i, EmitterParam.BIRTH_RATE, 0);
				continue;
			}

			particles.MultParam(i, EmitterParam.BIRTH_RATE, density);
			particles.MultParam(i, EmitterParam.LIFETIME, life);
			particles.MultParam(i, EmitterParam.LIFETIME_RND, life);
			particles.MultParam(i, EmitterParam.SIZE, m_fBerImpactSize);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ComputeEnvironment(IEntity owner)
	{
		if (m_bEnvComputed)
			return;
		m_bEnvComputed = true;

		BaseWorld world = owner.GetWorld();
		vector pos = owner.GetOrigin();

		// surface material below the detonation point
		vector groundPos;
		string matName;
		vector groundNormal;
		if (BER_SurfaceUtil.TraceGround(world, pos, 4.0, owner, groundPos, matName, groundNormal) && matName != "")
		{
			m_fDustFactor = BER_SurfaceUtil.GetDustFactor(matName, pos[1]);
			if (groundNormal != vector.Zero)
				m_vGroundNormal = groundNormal;
			m_bGroundFound = true;
			m_vGroundPos = groundPos;
			m_sGroundMat = matName;
		}
		else
			m_fDustFactor = 1.0; // airburst / nothing below — neutral

		m_fWindSpeed = BER_SurfaceUtil.GetWindSpeed(world);

		m_bIndoor = BER_SurfaceUtil.IsRoofed(world, pos, owner, m_fRoofCheckDistance);

		// wetness only wets the outdoors — a roofed interior floor stays dry in any storm
		if (!m_bIndoor)
			m_fDustFactor = m_fDustFactor * BER_SurfaceUtil.GetRainFactor(world);

		// nearest wall around an indoor detonation — used to shrink particle birth volumes
		// so the cloud is born inside the room, not on the far side of its walls
		if (m_bIndoor)
			m_fMinWallDist = BER_SurfaceUtil.GetMinWallDistance(world, pos, owner, 12.0);
	}
}
