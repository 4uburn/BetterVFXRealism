// Better VFX Realism: owns takeover explosions, fragment wisps/holes and bounded indoor dust.
// Contact dust is tuned by BER_OrientedContactComponent before emission. Adoption is
// restricted to named BER dust emitters close to this impact; fire/debris remain independent.

[EntityEditorProps(category: "GameScripted/BetterVFXRealism", description: "Surface/weather/indoor aware tuning of spawned effect particles")]
class BER_EffectTuningComponentClass : ScriptComponentClass
{
}

class BER_EffectTuningComponent : ScriptComponent
{
	[Attribute(defvalue: "", UIWidgets.ResourcePickerThumbnail, desc: "Explosion effect to spawn under BER control (vanilla reference must be blanked in the same prefab)", params: "ptc")]
	protected ResourceName m_rTakeoverEffect;

	[Attribute(defvalue: "0", desc: "Optional transient condensation for large outdoor blasts, 0 disables. Author only for suitably humid scenes; rain/wet soil is not humidity.", params: "0 1 0.05")]
	protected float m_fCondensationStrength;
	protected const ResourceName CONDENSATION = "{BA176BEE23D045AC}Particles/BER/BER_BlastCondensation.ptc";

	[Attribute(defvalue: "1", desc: "Scale particle density/lifetime by surface dustiness at the detonation point")]
	protected bool m_bScaleBySurface;

	// Compact density target; footprint is authored separately from particle count.
	[Attribute(defvalue: "1.35", desc: "Baseline particle density (birth rate) multiplier before surface scaling", params: "0.1 4 0.05")]
	protected float m_fDensityBoost;

	[Attribute(defvalue: "1.8", desc: "Baseline particle lifetime multiplier before surface scaling", params: "0.1 6 0.05")]
	protected float m_fLifetimeBoost;

	[Attribute(defvalue: "1.3", desc: "Emission time multiplier for named dust and smoke emitters (fire and debris keep their authored timing)", params: "1 5 0.05")]
	protected float m_fEmissionBoost;

	[Attribute(defvalue: "0.7", desc: "Particle size multiplier on top of the authored .ptc sizes", params: "0.4 2.5 0.05")]
	protected float m_fSizeBoost;

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

	// Indoor events accumulate structural dust with a gradual fade over 30+ seconds;
	// wood interiors shed a browner, thinner sawdust haze instead of grey plaster
	protected const ResourceName ROOM_FOG = "{BE20250902AC0027}Particles/BER/BER_RoomFog.ptc";
	protected const ResourceName ROOM_FOG_WOOD = "{BE20250903AC0037}Particles/BER/BER_RoomFog_Wood.ptc";

	protected static ref array<vector> s_aFogPos;
	protected static ref array<float> s_aFogTime;
	protected static ref array<float> s_aFogHits;   // accumulated impact weight
	protected static ref array<int> s_aFogLayers;   // -1 = still accumulating, no fog spawned yet
	protected const float FOG_DEDUP_RADIUS_SQ = 12.25; // 3.5 m
	protected const float FOG_DEDUP_TIME = 40.0;
	protected const float FOG_SPAWN_WEIGHT = 1.0;      // accumulated weight before the first fog appears
	protected const float FOG_WEIGHT_PER_LAYER = 12.0; // further weight per extra fog layer
	protected const int FOG_MAX_EXTRA_LAYERS = 3;

	// TESTING: log the warhead-vs-impact-effect transform relationship so the deflection
	// axis choice is verified with real numbers in a live test — flip to false before publish
	protected const bool DIAG_DEFLECT = false;


	protected const float FRAG_IMPACT_RANGE = 14.0;
	protected const float FRAG_IMPACT_MIN_DIST = 0.7;
	protected const float FRAG_DECAL_LIFETIME = 300.0;

	protected static BaseWorld s_FogWorld;
	protected static float s_fFogClock;
	protected static void EnsureFogState(BaseWorld world)
	{
		float now = world.GetWorldTime();
		if (!s_aFogPos || s_FogWorld != world || now < s_fFogClock)
		{
			s_FogWorld = world;
			s_aFogPos = {};
			s_aFogTime = {};
			s_aFogHits = {};
			s_aFogLayers = {};
		}
		s_fFogClock = now;
	}

	protected ref array<ParticleEffectEntity> m_aProcessed = {};
	protected float m_fElapsed;
	protected float m_fScanAccum;
	protected bool m_bTakeoverDone;
	protected bool m_bFragDone;
	protected bool m_bHullKickupDone;
	protected Vehicle m_KickupVehicle;

	// cached environment classification (computed once)
	protected bool m_bEnvComputed;
	protected float m_fDustFactor = 1.0;
	protected bool m_bIndoor;
	protected vector m_vGroundNormal = vector.Up;
	protected bool m_bGroundFound;
	protected vector m_vGroundPos;
	protected string m_sGroundMat;


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
		if (!owner || !owner.GetWorld())
			return;
		EnsureFogState(owner.GetWorld());
		// A carried grenade must not trigger effects or exhaust its watch interval.
		IEntity parent = owner.GetParent();
		if (parent && ChimeraCharacter.Cast(parent.GetRootParent()))
			return;

		if (!m_bTakeoverDone && m_rTakeoverEffect != ResourceName.Empty)
		{
			m_bTakeoverDone = true;
			SpawnTakeoverEffect(owner);
		}
		if (!m_bFragDone && m_iBerFragImpacts > 0)
		{
			m_bFragDone = true;
			SpawnFragmentImpacts(owner);
		}
		if (!m_bHullKickupDone && m_fHullKickup > 0.01)
		{
			m_bHullKickupDone = true;
			TryHullKickup(owner);
		}

		m_fElapsed += timeSlice;
		m_fScanAccum += timeSlice;
		// Surface contact effects now tune themselves before their first particle.
		// Only the owning impact's small neighbourhood may be adopted.
		if (m_rTakeoverEffect == ResourceName.Empty && !m_bIndoorSmokeSwap && m_fElapsed <= 0.35)
		{
			m_vQueryCenter = owner.GetOrigin();
			owner.GetWorld().QueryEntitiesBySphere(m_vQueryCenter, 0.6, QueryParticleCallback, null, EQueryEntitiesFlags.ALL);
		}
		if (m_fScanAccum >= 0.2)
		{
			m_fScanAccum = 0;
			ScanChildren(owner);
		}
		if (m_fElapsed > m_fScanDuration)
			ClearEventMask(owner, EntityEvent.FRAME);
	}

	protected void SpawnTakeoverEffect(IEntity owner)
	{
		ComputeEnvironment(owner);

		// the blast shock rips accumulated dust off nearby thin layers and vehicle hulls
		BER_DustReservoir.RipArea(owner.GetWorld(), owner.GetOrigin(), 9.0, 1.0);

		// One impulse per tracked muzzle cloud, including sheltered impulse-only puffs.
		BER_MuzzleBlastDust.ShovePuffs(owner.GetWorld(), owner.GetOrigin(), 9.0, vector.Zero, 2.5);

		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		spawnParams.PlayOnSpawn = false;
		// Align initial emission to the slope; subsequent motion is in world space.
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

		// Dust and smoke drift per particle through native drag; debris stays ballistic.
		SpawnCondensation(owner);

		if (m_fDebrisScale > 0.01)
			SpawnImpactDebris();

		// the concussion of an indoor detonation shakes structural dust out of the whole
		// room — a lingering fog on top of the explosion's own dust (not when it went off
		// on a material that sheds none, e.g. a metal floor). Explosions carry enough
		// weight to raise the fog at once and advance the layer buildup fast.
		if (m_bIndoor && m_fDustFactor > 0)
			SpawnRoomFog(owner, 1.0, 3.0 * m_fDustFactor, FogVariantFor(m_sGroundMat));
	}

	//------------------------------------------------------------------------------------------------
	//! Accumulate impact weight per estimated room, then emit bounded, fading dust layers
	//! near the hit. A clear path prevents adjacent rooms sharing the same accumulator.
	protected void SpawnRoomFog(IEntity owner, float strength, float weight, ResourceName fogRes)
	{
		BaseWorld world = owner.GetWorld();
		vector pos = owner.GetOrigin();
		if (weight <= 0)
			return;
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
			if (vector.DistanceSq(s_aFogPos[i], roomCenter) < FOG_DEDUP_RADIUS_SQ
				&& BER_SurfaceUtil.HasClearPath(world, roomCenter, s_aFogPos[i], owner))
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
					if (SpawnFogEntity(fogPos, 0.45, roomHalf, fogRes))
						s_aFogLayers[i] = s_aFogLayers[i] + 1;
				}
				return;
			}
		}

		if (s_aFogPos.Count() >= 64)
			return; // bound the active room budget during large battles

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

		// Visual puff size follows weapon-class scale; this is not a wound/penetration model.
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
			// Limit both average size and variation by clearance at the actual spawn point.
			float clearance = BER_SurfaceUtil.GetMinWallDistance(GetGame().GetWorld(), center, null, roomHalf);
			float roomSize = BER_SurfaceUtil.ClampF(clearance / 3.0, 0.02, 1.0);
			float sizeMult = roomSize * BER_SurfaceUtil.ClampF(strength, 0.4, 1.0);
			int emitterCount = particles.GetNumEmitters();
			for (int i = 0; i < emitterCount; i++)
			{
				particles.MultParam(i, EmitterParam.SIZE, sizeMult);
				particles.MultParam(i, EmitterParam.SIZE_RND, sizeMult);
			}
		}

		pfx.Play();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Visual ground debris: surface availability and event scale determine a bounded
	//! count. Native particles handle gravity/collision; the cleanup estimate is not
	//! a damage or shrapnel-trajectory simulation.
	protected void SpawnImpactDebris()
	{
		if (!m_bGroundFound)
			return; // airburst — nothing to rip out of the ground

		string mat = m_sGroundMat;
		mat.ToLower();

		float solidAvailability = BER_SurfaceUtil.GetSolidDebrisAvailability(mat);
		if (solidAvailability <= 0)
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
			float eventScale = BER_SurfaceUtil.ClampF(m_fDebrisScale, 0, 5);
			float sizeMult = BER_SurfaceUtil.ClampF(0.75 + 0.25 * eventScale, 0.6, 1.8);
			float speedMult = BER_SurfaceUtil.GetDebrisSpeedScale(eventScale);
			array<string> names = {};
			particles.GetEmitterNames(names);
			float authoredCount;
			foreach (int i, string name : names)
			{
				if (name.IndexOf("ber_dust_") == 0)
					continue;
				float rate, variation, duration;
				particles.GetParamOrig(i, EmitterParam.BIRTH_RATE, rate);
				particles.GetParamOrig(i, EmitterParam.BIRTH_RATE_RND, variation);
				particles.GetParamOrig(i, EmitterParam.EMITTING_TIME, duration);
				authoredCount += (rate + variation) * duration;
			}
			float countMult = eventScale * solidAvailability;
			if (authoredCount > 0 && authoredCount * countMult > 48)
				countMult = 48.0 / authoredCount;
			foreach (int i, string name : names)
			{
				if (name.IndexOf("ber_dust_") == 0)
					continue;
				particles.MultParam(i, EmitterParam.BIRTH_RATE, countMult);
				particles.MultParam(i, EmitterParam.BIRTH_RATE_RND, countMult);
				particles.MultParam(i, EmitterParam.SIZE, sizeMult);
				particles.MultParam(i, EmitterParam.SIZE_RND, sizeMult);
				particles.MultParam(i, EmitterParam.VELOCITY, speedMult);
				particles.MultParam(i, EmitterParam.VELOCITY_RND, speedMult);
				float speed, speedRandom;
				particles.GetParamOrig(i, EmitterParam.VELOCITY, speed);
				particles.GetParamOrig(i, EmitterParam.VELOCITY_RND, speedRandom);
				particles.SetParam(i, EmitterParam.LIFETIME, BER_SurfaceUtil.GetDebrisLifetime((speed + speedRandom) * speedMult));
				particles.SetParam(i, EmitterParam.LIFETIME_RND, 0.2);
			}
			float dust = BER_SurfaceUtil.GetDustAvailability(GetOwner().GetWorld(), m_vGroundPos, m_sGroundMat, m_bIndoor);
			BER_SurfaceUtil.TuneDust(particles, dust * m_fDebrisScale, m_bIndoor, sizeMult);
		}

		pfx.Play();
	}

	//! Fully wired author opt-in: no inference of atmospheric humidity from ground wetness.
	protected void SpawnCondensation(IEntity owner)
	{
		if (m_fCondensationStrength <= 0 || m_fDebrisScale < 2.5 || m_bIndoor)
			return;
		ParticleEffectEntitySpawnParams params = new ParticleEffectEntitySpawnParams();
		params.UseFrameEvent = true;
		params.PlayOnSpawn = false;
		params.Transform[3] = owner.GetOrigin();
		ParticleEffectEntity vapor = ParticleEffectEntity.SpawnParticleEffect(CONDENSATION, params);
		if (!vapor)
			return;
		BER_OwnedEffects.MarkOwned(vapor);
		Particles particles = vapor.GetParticles();
		if (particles)
			particles.MultParam(-1, EmitterParam.BIRTH_RATE, BER_SurfaceUtil.ClampF(m_fCondensationStrength, 0, 1));
		vapor.Play();
	}

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

	protected void SpawnFragmentImpacts(IEntity owner)
	{
		BaseWorld world = owner.GetWorld();
		vector origin = owner.GetOrigin() + Vector(0, 0.18, 0);
		int count = Math.ClampInt(m_iBerFragImpacts, 0, 64);
		for (int i = 0; i < count; i++)
		{
			vector dir = RandomSphereDir();
			TraceParam tp = new TraceParam();
			tp.Start = origin;
			tp.End = origin + dir * FRAG_IMPACT_RANGE;
			tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
			tp.Exclude = owner;
			float fraction = world.TraceMove(tp, null);
			float distance = FRAG_IMPACT_RANGE * fraction;
			if (fraction >= 1.0 || distance < FRAG_IMPACT_MIN_DIST || !tp.SurfaceProps)
				continue;
			string material = tp.SurfaceProps.GetName();
			material.ToLower();
			if (material.Contains("water") || material.Contains("flesh"))
				continue;
			GameMaterial hitMaterial = tp.SurfaceProps;
			if (!hitMaterial)
				continue;
			HitEffectInfo hit = hitMaterial.GetHitEffectInfo();
			if (!hit)
				continue;
			vector pos = origin + dir * distance;
			vector normal = tp.TraceNorm;
			// Accept either getter ordering; select resources by their actual extension.
			ResourceName first = hit.GetParticleEffectValue();
			ResourceName second = hit.GetDecalMaterialValue();
			ResourceName particle = ResourceName.Empty;
			ResourceName decal = ResourceName.Empty;
			string firstPath = first;
			string secondPath = second;
			if (firstPath.Contains(".ptc")) particle = first;
			if (secondPath.Contains(".ptc")) particle = second;
			if (firstPath.Contains(".emat")) decal = first;
			if (secondPath.Contains(".emat")) decal = second;

			// Hole and dust have independent lifecycles: wetness never erases the hole.
			World decalWorld = world;
			if (decalWorld && tp.TraceEnt && decal != ResourceName.Empty)
				decalWorld.CreateDecal(tp.TraceEnt, pos + normal * 0.01, -normal,
					0, 0.08, Math.RandomFloat(0, 360), 0.045, 1, decal, FRAG_DECAL_LIFETIME, 0xFFFFFFFF);
			bool indoor = BER_SurfaceUtil.IsRoofed(world, pos + normal * 0.15, owner, 25);
			float dust = BER_SurfaceUtil.GetDustAvailability(world, pos, material, indoor);
			if (dust <= 0.001 || particle == ResourceName.Empty)
				continue;
			ParticleEffectEntitySpawnParams params = new ParticleEffectEntitySpawnParams();
			params.UseFrameEvent = true;
			params.PlayOnSpawn = false;
			if (normal != vector.Zero)
				SCR_EntityHelper.OrientUpToVector(normal, params.Transform);
			params.Transform[3] = pos + normal * 0.01;
			ParticleEffectEntity pfx = ParticleEffectEntity.SpawnParticleEffect(ResolveFragHitEffect(particle), params);
			if (!pfx)
				continue;
			BER_OwnedEffects.MarkOwned(pfx);
			Particles particles = pfx.GetParticles();
			if (particles)
				BER_SurfaceUtil.TuneDust(particles, dust, indoor, 1.0, 1.0);
			pfx.Play();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Shrapnel wisp for the material whose bullet-hit effect is hitPtc ("…/Hit_<mat>_enter_01.ptc").
	//! Materials that have no wisp of their own (flesh, water, foliage) use the default one.
	protected static ResourceName ResolveFragHitEffect(ResourceName hitPtc)
	{
		string path = hitPtc;
		path.ToLower();
		if (path.Contains("hit_asphalt_enter_01"))
			return "{BE20250905AC0040}Particles/BER/BER_FragHit_asphalt.ptc";
		if (path.Contains("hit_brick_enter_01"))
			return "{BE20250905AC0041}Particles/BER/BER_FragHit_brick.ptc";
		if (path.Contains("hit_concrete_enter_01"))
			return "{BE20250905AC0042}Particles/BER/BER_FragHit_concrete.ptc";
		if (path.Contains("hit_dirt_enter_01"))
			return "{BE20250905AC0044}Particles/BER/BER_FragHit_dirt.ptc";
		if (path.Contains("hit_fabric_enter_01"))
			return "{BE20250905AC0045}Particles/BER/BER_FragHit_fabric.ptc";
		if (path.Contains("hit_glass_enter_01"))
			return "{BE20250905AC0046}Particles/BER/BER_FragHit_glass.ptc";
		if (path.Contains("hit_grass_enter_01"))
			return "{BE20250905AC0047}Particles/BER/BER_FragHit_grass.ptc";
		if (path.Contains("hit_gravel_enter_01"))
			return "{BE20250905AC0048}Particles/BER/BER_FragHit_gravel.ptc";
		if (path.Contains("hit_metal_enter_01"))
			return "{BE20250905AC0049}Particles/BER/BER_FragHit_metal.ptc";
		if (path.Contains("hit_plastic_enter_01"))
			return "{BE20250905AC0050}Particles/BER/BER_FragHit_plastic.ptc";
		if (path.Contains("hit_rubber_enter_01"))
			return "{BE20250905AC0051}Particles/BER/BER_FragHit_rubber.ptc";
		if (path.Contains("hit_sand_enter_01"))
			return "{BE20250905AC0052}Particles/BER/BER_FragHit_sand.ptc";
		if (path.Contains("hit_snow_enter_01"))
			return "{BE20250905AC0053}Particles/BER/BER_FragHit_snow.ptc";
		if (path.Contains("hit_soil_enter_01"))
			return "{BE20250905AC0054}Particles/BER/BER_FragHit_soil.ptc";
		if (path.Contains("hit_stone_enter_01"))
			return "{BE20250905AC0055}Particles/BER/BER_FragHit_stone.ptc";
		if (path.Contains("hit_wood_enter_01"))
			return "{BE20250905AC0056}Particles/BER/BER_FragHit_wood.ptc";
		return "{BE20250905AC0043}Particles/BER/BER_FragHit_default.ptc";
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
		if (vector.DistanceSq(pfx.GetOrigin(), m_vQueryCenter) > 0.36)
			return true;

		m_aProcessed.Insert(pfx);
		ProcessAdopted(pfx, GetOwner());
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Adopt only nearby named impact dust. Native world-space particles provide drift;
	//! smoke-device replacement follows its source while already emitted smoke stays behind.
	protected void ProcessAdopted(ParticleEffectEntity pfx, IEntity owner)
	{
		if (BER_OwnedEffects.IsOwned(pfx))
			return;
		Particles particles = pfx.GetParticles();
		if (!particles)
			return;
		if (!m_bIndoorSmokeSwap && !BER_SurfaceUtil.HasDustEmitters(particles))
			return; // never adopt an unrelated flash, exhaust, tracer or another mod's effect
		ComputeEnvironment(owner);

		if (m_bIndoorSmokeSwap)
		{
			if (m_bIndoor)
				SwapToIndoorVariant(pfx, owner);
			return;
		}
		if (m_rTakeoverEffect != ResourceName.Empty)
			return; // takeover and surface contacts already have an owner

		BER_OwnedEffects.MarkOwned(pfx);
		ResolveImpactInfo(owner);
		DeflectImpactSplash(pfx, owner);
		string struck = m_sBerStruckMat;
		if (struck == "")
			struck = m_sGroundMat;
		float dust = BER_SurfaceUtil.GetDustAvailability(owner.GetWorld(), pfx.GetOrigin(), struck, m_bIndoor);
		BER_SurfaceUtil.TuneDust(particles, dust * m_fBerImpactDensity, m_bIndoor, m_fBerImpactSize, 1.0);
		if (m_bIndoor && dust > 0)
			SpawnRoomFog(owner, 0.7, dust * m_fBerCalWeight * FogMaterialWeight(struck), FogVariantFor(struck));
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
		if (swapped)
		{
			pfx.StopEmission();
			BER_OwnedEffects.MarkOwned(pfx);
			BER_OwnedEffects.MarkOwned(swapped);
		}
		return swapped;
	}

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
	protected void TuneEmitters(Particles particles)
	{
		if (!m_bScaleBySurface)
			return;
		float density = m_fDensityBoost * BER_SurfaceUtil.ClampF(m_fDustFactor, 0, 1.6);
		BER_SurfaceUtil.TuneDust(particles, density, m_bIndoor, m_fSizeBoost, m_fLifetimeBoost);
		// Combustion aerosol is not suppressed by wet soil. Fire and prefab emitters
		// retain their original timing/counts, including on a dedicated server.
		array<string> names = {};
		particles.GetEmitterNames(names);
		foreach (int i, string name : names)
		{
			if (name.IndexOf("ber_dust_") == 0 || name.IndexOf("ber_smoke_") == 0)
				particles.MultParam(i, EmitterParam.EMITTING_TIME, BER_SurfaceUtil.ClampF(m_fEmissionBoost, 1, 2));
			if (name.IndexOf("ber_smoke_") != 0)
				continue;
			particles.SetParam(i, EmitterParam.WIND, !m_bIndoor);
			if (m_bIndoor)
			{
				particles.MultParam(i, EmitterParam.VELOCITY, 0.65);
				particles.MultParam(i, EmitterParam.LIFETIME, 1.5);
				particles.MultParam(i, EmitterParam.LIFETIME_RND, 1.5);
			}
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
			if (groundNormal != vector.Zero)
				m_vGroundNormal = groundNormal;
			m_bGroundFound = true;
			m_vGroundPos = groundPos;
			m_sGroundMat = matName;
		}
		else
			m_fDustFactor = 1.0; // airburst / nothing below — neutral


		m_bIndoor = BER_SurfaceUtil.IsRoofed(world, pos, owner, m_fRoofCheckDistance);

		// wetness only wets the outdoors — a roofed interior floor stays dry in any storm
		m_fDustFactor = BER_SurfaceUtil.GetDustAvailability(world, pos, m_sGroundMat, m_bIndoor);

	}
}
