//------------------------------------------------------------------------------------------------
// Better Effects Realism — muzzle overpressure ground/hull dust
//
// Hooks every weapon that uses SCR_MuzzleEffectComponent (all firearms, turrets, launchers).
// The muzzle concussion rips loose dust off whatever lies under the muzzle:
//  - GROUND: the surface's own small blast-dust puffs (gamemat BLAST table — the same
//    per-surface effects grenades scatter). Very dirty ground (dirt, gravel, forest floor)
//    is an infinite dust source; thin layers (roads, dry grass) deplete per shot via
//    BER_DustReservoir and regenerate slowly. Sustained fire drives the dust outbound:
//    every shot's concussion shoves the previous puffs away from the muzzle and thins
//    them out — the weapon blows its own dust cloud clear.
//  - VEHICLE HULLS: firing a mounted gun (e.g. the LAV 25mm) kicks the dust that has
//    accumulated on the hull off the surface in muzzle-smoke-shaped burst puffs. Every
//    puff sits exactly on a traced surface point of the actual vehicle (the deck plane is
//    read dynamically from probe heights, so custom vehicles work unmodified), fires the
//    same frame as the shot, is fully opaque at birth and fades out fast. Micro details —
//    the gun barrel included — get their own small dusting at their exact surface.
// Each weapon class has a rip-off value: bigger concussion = more dust per shot and a
// taller reach. Availability decides the puff: full > faint (tiny) > nothing.
//------------------------------------------------------------------------------------------------

modded class SCR_MuzzleEffectComponent
{
	override void OnFired(IEntity effectEntity, BaseMuzzleComponent muzzle, IEntity projectileEntity)
	{
		super.OnFired(effectEntity, muzzle, projectileEntity);

		IEntity weaponEntity = null;
		if (muzzle)
			weaponEntity = muzzle.GetOwner();
		BER_MuzzleBlastDust.OnWeaponFired(weaponEntity, effectEntity, projectileEntity);
	}
}

class BER_MuzzleBlastDust
{
	protected const int BLAST_INDEX_TINY = 0;
	protected const int BLAST_INDEX_SMALL = 1;
	protected const int BLAST_INDEX_MEDIUM = 2;

	// hull dust kickoff — muzzle-smoke-shaped instant burst puffs (BER-authored: fully
	// opaque at birth, fast fade, near-windless so only strong wind visibly moves them)
	protected const ResourceName HULL_KICKOFF = "{BE20250902AC0023}Particles/BER/BER_HullKickoff.ptc";

	// small-arms ground dust — BER-authored with the tiny footprint (~0.4 m) baked in:
	// runtime SHAPE_SIZE scaling of the meters-wide gamemat blast boxes proved to be a
	// dead API (three successive halvings produced no visible change), so the size
	// guarantee has to live in the authored file
	protected const ResourceName RIFLE_DUST = "{BE20250902AC0025}Particles/BER/BER_RifleDust.ptc";


	// sustained-fire envelopment: high-volume fire on dusty ground builds a soft rising
	// dust cloud around the shooter (windless, slow, authored person-sized)
	protected const ResourceName RIFLE_CLOUD = "{BE20250902AC0026}Particles/BER/BER_RifleDustCloud.ptc";

	// whole-deck hull kickoff sheets — ONE continuous film covering the hull top instead
	// of per-probe point bursts (runtime emission-shape scaling is a dead API, so two
	// authored footprints cover the vehicle size range)
	protected const ResourceName HULL_SHEET_M = "{BE20250902AC0028}Particles/BER/BER_HullSheet_M.ptc";
	protected const ResourceName HULL_SHEET_L = "{BE20250902AC0029}Particles/BER/BER_HullSheet_L.ptc";

	// stationary hull blanket: the wall of foreground dirt a vehicle-mounted heavy weapon
	// kicks up ahead of its own bow, pumped up and over the hull by every shot of the burst
	protected const ResourceName GLACIS_WASH = "{BE20250903AC0030}Particles/BER/BER_GlacisWash.ptc";

	protected static ref array<ParticleEffectEntity> s_aWashPfx = {};
	protected static ref array<float> s_aWashTime = {};
	protected static ref array<float> s_aWashBirth = {};
	protected static ref array<float> s_aWashLife = {};
	protected static int s_iWashPulse;

	protected const float WASH_TRACK_TIME = 8.0;     // s a wash cloud stays pumpable
	protected const float WASH_SPAWN_INTERVAL = 0.6; // s between fresh washes per position (the pulse carries the burst)
	protected const float BLANKET_MAX_SPEED = 2.5;   // m/s — forward movement outruns the cloud

	// MASTER SWITCH for every vehicle hull dust effect: the stationary hull blanket
	// (glacis wash + bellows), the firing-over-own-hull kickoff/deck sheet, and the
	// heavy-round vehicle-hit kickup. Parked OFF for now — making these read right needs
	// a more in-depth custom particle than the current sheets/washes; the code stays for
	// that rework. Flip to true to revive everything at once.
	protected const bool VEHICLE_DUST_ENABLED = false;

	// TESTING: bypass the hull reservoir gating so every burst shows the full hull kickoff.
	// The reservoir keeps simulating underneath — flip to false to restore rip-off/regen gating.
	protected const bool TEST_HULL_DUST_ALWAYS_ON = true;

	protected static ref array<vector> s_aRecentPos = {};
	protected static ref array<float> s_aRecentTime = {};

	protected const float DEDUP_RADIUS_SQ = 2.25;  // 1.5 m — one dust event per muzzle position...
	protected const float DEDUP_TIME = 0.45;       // ...per 0.45 s, so automatic fire doesn't stack effects

	// small-arms puffs the next shots' concussion can blow outbound
	protected static ref array<ParticleEffectEntity> s_aRiflePuffs = {};
	protected static ref array<float> s_aRiflePuffTime = {};
	protected static ref array<float> s_aRiflePuffBirth = {};
	protected static ref array<float> s_aRiflePuffLife = {};

	protected const float RIFLE_PUFF_TRACK_TIME = 8.0;  // s a puff stays shoveable
	protected const float CONCUSSION_RADIUS = 2.0;      // m — the muzzle gas pressure doesn't reach far

	// recent shot rays (every weapon, every shooter) — the impact deflection matches an
	// impact point against the ray it lies on to recover the true incoming direction,
	// and the recorded weapon-class scale lets the impact scale its dust by caliber
	protected static ref array<vector> s_aShotPos = {};
	protected static ref array<vector> s_aShotDir = {};
	protected static ref array<float> s_aShotTime = {};
	protected static ref array<float> s_aShotScale = {};

	protected const float SHOT_RAY_KEEP = 2.5;   // s a ray stays matchable
	protected const float SHOT_RAY_RANGE = 700;  // m of flight beyond which a match is rejected

	// volley tracking — sustained small-arms fire from one position raises an enveloping cloud
	protected static ref array<float> s_aVolleyTime = {};
	protected static vector s_vVolleyPos = vector.Zero;
	protected static float s_fLastCloudTime = -100;

	protected const float VOLLEY_WINDOW = 3.0;    // s a shot counts toward the volley
	protected const int VOLLEY_SHOTS = 6;         // shots within the window that trigger the cloud
	protected const float CLOUD_COOLDOWN = 2.5;   // s between cloud spawns while fire continues
	protected const float VOLLEY_MOVE_RESET = 2.5; // m of muzzle movement that resets the volley

	//------------------------------------------------------------------------------------------------
	static void OnWeaponFired(IEntity weaponEntity, IEntity effectEntity, IEntity projectileEntity)
	{
		IEntity posSource = effectEntity;
		if (!posSource)
			posSource = weaponEntity;
		if (!posSource)
			return;

		BaseWorld world = posSource.GetWorld();
		if (!world)
			return;

		// GetOrigin is parent-local for attached entities (the muzzle flash is a weapon
		// child) — the world transform is the muzzle's real position
		vector posMat[4];
		posSource.GetWorldTransform(posMat);
		vector muzzlePos = posMat[3];
		if (muzzlePos == vector.Zero)
			return;

		// flat barrel direction — offset puffs, concussion shoves and behind-muzzle hull
		// probes all march along it
		vector fwdFlat = vector.Zero;
		vector fwd = posMat[2];
		float fwdX = fwd[0];
		float fwdZ = fwd[2];
		float fwdLen = Math.Pow(fwdX * fwdX + fwdZ * fwdZ, 0.5);
		if (fwdLen > 0.001)
			fwdFlat = Vector(fwdX / fwdLen, 0, fwdZ / fwdLen);

		// weapon class scale from the projectile prefab path
		float scale = 0.35;
		if (projectileEntity)
		{
			EntityPrefabData prefabData = projectileEntity.GetPrefabData();
			if (prefabData)
			{
				string path = prefabData.GetPrefabName();
				path.ToLower();

				if (path.Contains("81mm") || path.Contains("82mm"))
					return; // mortars already get a vanilla ground blast

				if (path.Contains("25x137") || path.Contains("30x1"))
					scale = 2.2;
				else if (path.Contains("145x114"))
					scale = 1.8;
				else if (path.Contains("pg7") || path.Contains("pg22") || path.Contains("m72") || path.Contains("rpg") || path.Contains("hydra") || path.Contains("s5"))
					scale = 1.8;
				else if (path.Contains("127x"))
					scale = 1.2;
				else if (path.Contains("m406") || path.Contains("m433") || path.Contains("vog"))
					scale = 0.5;
				else if (path.Contains("762x"))
					scale = 0.42;
				else if (path.Contains("545x39") || path.Contains("556x45"))
					scale = 0.36;
				else if (path.Contains("9x19") || path.Contains("9x18") || path.Contains("45acp") || path.Contains("45_acp"))
					scale = 0.2;

				// 40mm UGL rounds leave a hot case in the breech — it only smokes if the
				// shooter extracts it right away (GP-25 VOG rounds are caseless)
				if (!path.Contains("vog") && (path.Contains("m406") || path.Contains("m433") || path.Contains("40mm")))
					BER_UGLCaseSmoke.NotifyFired(weaponEntity);
			}
		}

		float now = world.GetWorldTime() * 0.001;

		// record the shot ray (full 3D muzzle direction) for impact-splash deflection
		if (fwd != vector.Zero)
		{
			for (int i = s_aShotTime.Count() - 1; i >= 0; i--)
			{
				if (now - s_aShotTime[i] > SHOT_RAY_KEEP)
				{
					s_aShotTime.Remove(i);
					s_aShotPos.Remove(i);
					s_aShotDir.Remove(i);
					s_aShotScale.Remove(i);
				}
			}
			vector shotDir = fwd;
			shotDir.Normalize();
			s_aShotPos.Insert(muzzlePos);
			s_aShotDir.Insert(shotDir);
			s_aShotTime.Insert(now);
			s_aShotScale.Insert(scale);
		}

		// every small-arms shot shoves the puffs of the previous shots outbound — the
		// concussion blows the weapon's own dust cloud away from the muzzle, so this runs
		// before the dedup gate (shots that spawn nothing still push). The same pre-dedup
		// spot counts the shot toward the volley: high-volume fire from one position on
		// dusty ground raises a soft dust cloud that envelops the shooter.
		if (scale < 1.0)
		{
			PushRecentRiflePuffs(muzzlePos, fwdFlat, now);
			TrackVolley(world, muzzlePos, fwdFlat, weaponEntity, scale, now);
		}

		// a vehicle-mounted heavy weapon firing from a standstill blankets its own hull in
		// kicked-up foreground dust — runs for EVERY round (the burst is the bellows), so
		// it sits before the dedup gate like the small-arms concussion does
		if (VEHICLE_DUST_ENABLED && scale >= 1.0 && weaponEntity)
		{
			Vehicle ownVeh = Vehicle.Cast(weaponEntity.GetRootParent());
			if (ownVeh)
				HullBlanket(world, ownVeh, weaponEntity, muzzlePos, fwdFlat, scale, now);
		}

		// dedup: heavier weapons refresh at most ~2 Hz per position. Small arms are NOT
		// deduped — the initial ground splash pulses with every round so the effect
		// matches the weapon's fire rate; only the accumulating light waft keeps the
		// ~2 Hz cadence (waftDue)
		bool waftDue = true;
		for (int i = s_aRecentTime.Count() - 1; i >= 0; i--)
		{
			if (now - s_aRecentTime[i] > DEDUP_TIME)
			{
				s_aRecentTime.Remove(i);
				s_aRecentPos.Remove(i);
				continue;
			}
			if (vector.DistanceSq(s_aRecentPos[i], muzzlePos) < DEDUP_RADIUS_SQ)
			{
				if (scale >= 1.0)
					return;
				waftDue = false;
			}
		}

		// trace down — exclude the weapon and the shooter's body, but NOT the vehicle:
		// a mounted gun's concussion rips dust off its own hull
		array<IEntity> excludes = {};
		if (weaponEntity)
		{
			excludes.Insert(weaponEntity);
			IEntity root = weaponEntity.GetRootParent();
			if (root && ChimeraCharacter.Cast(root))
				excludes.Insert(root);
		}

		vector hitPos;
		string matName;
		IEntity hitRoot;
		if (!BER_SurfaceUtil.TraceGroundEx(world, muzzlePos, 3.0, excludes, hitPos, matName, hitRoot))
			return;

		float hitDist = muzzlePos[1] - hitPos[1];
		if (hitDist < 0)
			hitDist = 0;

		float ripAmount = BER_SurfaceUtil.ClampF(0.25 + 0.35 * scale, 0.1, 1.0);

		// ---- VEHICLE HULL PATH ----
		Vehicle hitVehicle = null;
		if (hitRoot)
			hitVehicle = Vehicle.Cast(hitRoot);
		if (hitVehicle)
		{
			if (!VEHICLE_DUST_ENABLED)
				return; // parked — see the master switch note

			if (hitDist > 2.0)
				return;

			float hullAvail = BER_DustReservoir.TakeVehicle(hitVehicle, world, ripAmount);
			bool faint = (hullAvail < 0.55);
			if (hullAvail < 0.22 && !TEST_HULL_DUST_ALWAYS_ON)
				return;
			if (TEST_HULL_DUST_ALWAYS_ON)
				faint = false;

			float hullLifeMult;
			bool hullAllowDrift;
			ComputeEnvFactors(world, muzzlePos, weaponEntity, hullLifeMult, hullAllowDrift);
			// kickoff puffs die within ~a second — the tiny authored WindInfluence covers
			// "barely moves unless the wind is really strong", no drift animation needed

			s_aRecentPos.Insert(muzzlePos);
			s_aRecentTime.Insert(now);
			SpawnHullKickoff(world, hitVehicle, excludes, faint, muzzlePos, fwdFlat, hitPos, scale, hullLifeMult);
			return;
		}

		// ---- GROUND PATH ----
		// the muzzle gas pressure doesn't reach far: small arms only disturb the ground
		// from a bipod-deployed kind of height; heavier blast waves reach further down
		float maxGroundDist = 0.7 + 0.65 * scale;
		if (scale < 1.0)
			maxGroundDist = 0.45 + 0.35 * scale;
		if (hitDist > maxGroundDist)
			return; // muzzle too high above ground — a standing rifleman kicks up nothing

		if (matName == "")
			return;

		// only genuinely dusty surfaces react to small-arms overpressure — moist grass,
		// wet sand, rain-soaked ground stays quiet (heavier blast waves excepted)
		float dust = BER_SurfaceUtil.GetDustFactor(matName, hitPos[1]) * BER_SurfaceUtil.GetRainFactor(world);
		float dustThreshold = 1.0;
		if (scale >= 1.0)
			dustThreshold = 0.7;
		if (dust < dustThreshold)
			return;

		// thin layers deplete; very dirty ground is an infinite dust source. Small arms
		// now rip per SHOT instead of per dedup window (~6x as often) — scale the per-rip
		// amount down so the depletion rate stays what it was tuned to
		if (scale < 1.0)
			ripAmount = ripAmount * 0.18;
		float available = 1.0;
		if (dust <= 1.25)
			available = BER_DustReservoir.TakeGround(world, hitPos, ripAmount);

		int blastIndex = -1;
		bool withOffsets = false;
		if (available >= 0.55)
		{
			blastIndex = BLAST_INDEX_SMALL;
			if (scale >= 1.5)
				blastIndex = BLAST_INDEX_MEDIUM;
			withOffsets = (scale >= 1.0);
		}
		else if (available >= 0.22)
		{
			blastIndex = BLAST_INDEX_TINY; // depleted layer — only a faint puff
		}
		if (blastIndex < 0)
			return;

		// small arms: the flash hider vents the gas out of its SIDE ports, so the dust
		// wash is a generalized area in and around the muzzle's own ground point — no
		// forward displacement; the wind takes over from there
		vector traceOrigin = muzzlePos;

		vector groundPos;
		string matName2;
		vector groundNormal;
		ResourceName blastRes = BER_SurfaceUtil.GetBlastResourceAt(world, traceOrigin, 3.0, weaponEntity, blastIndex, groundPos, matName2, groundNormal);
		if (blastRes == ResourceName.Empty)
			return;

		float lifeMult;
		bool allowDrift;
		ComputeEnvFactors(world, muzzlePos, weaponEntity, lifeMult, allowDrift);

		// the dedup entry paces heavy-weapon effects and the rifle WAFT accumulation —
		// the rifle splash itself deliberately spawns for every round
		if (scale >= 1.0 || waftDue)
		{
			s_aRecentPos.Insert(muzzlePos);
			s_aRecentTime.Insert(now);
		}

		// small arms spawn the BER-authored rifle dust instead of the surface's blast
		// effect: the authored footprint is muzzle-gas sized by construction (elongated
		// along the barrel so the whole wash reads as one gas-driven event), scaled only
		// slightly per caliber (5.56 < default < 7.62; pistols smallest). The splash is
		// dense, ground-hugging and short — one per ROUND, the rhythm of the weapon —
		// while the separate light waft accumulates at the dedup cadence and carries the
		// lingering, wind-driven haze.
		bool riflePuff = (scale < 1.0);
		float sizeMult = 1.0;
		float birthMult = 1.0;
		ResourceName spawnRes = blastRes;
		if (riflePuff)
		{
			spawnRes = RIFLE_DUST;
			sizeMult = 0.75 + 0.7 * scale; // pistol ~0.9, 5.56 ~0.96, 7.62 ~1.04
			if (blastIndex == BLAST_INDEX_TINY)
			{
				// depleted thin layer — only a faint wisp left to rip loose
				sizeMult = sizeMult * 0.75;
				birthMult = 0.5;
			}
		}

		vector puffFwd = vector.Zero;
		if (riflePuff)
			puffFwd = fwdFlat;
		ParticleEffectEntity puff = SpawnPuff(spawnRes, groundPos, groundNormal, sizeMult, birthMult, lifeMult, allowDrift && !riflePuff, vector.Zero, 1.0, 1.0, puffFwd);

		// the per-round splash IS the effect that grows into the larger sweeping dust:
		// dense at birth hugging the ground, its particles live on, grow and lighten, and
		// the same entity is then DRIVEN — wind drift (short ramp, almost no pressure
		// hold) plus the radial shoves of follow-up shots and explosions sweep it away.
		if (puff && riflePuff)
		{
			s_aRiflePuffs.Insert(puff);
			s_aRiflePuffTime.Insert(now);
			s_aRiflePuffBirth.Insert(birthMult);
			s_aRiflePuffLife.Insert(lifeMult);

			if (allowDrift)
				BER_WindDriftAnimator.GetInstance().Register(puff, 2.5, birthMult, lifeMult, 0.15);
		}

		// heavier weapons cover a wider overpressure zone — extra small puffs forward/sideways
		if (withOffsets && fwdFlat != vector.Zero)
		{
			vector side = Vector(fwdFlat[2], 0, -fwdFlat[0]);
			float spread = 0.8 + 0.6 * scale;

			SpawnOffsetPuff(world, weaponEntity, groundPos + fwdFlat * spread + side * spread * 0.5, lifeMult, allowDrift);
			SpawnOffsetPuff(world, weaponEntity, groundPos + fwdFlat * spread - side * spread * 0.5, lifeMult, allowDrift);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Count small-arms shots fired from (roughly) one position; sustained high-volume fire
	//! on dusty ground raises an enveloping cloud around the SHOOTER — the accumulated
	//! kicked-up dust rising around the firing position, refreshed while the fire continues.
	protected static void TrackVolley(BaseWorld world, vector muzzlePos, vector fwdFlat, IEntity weaponEntity, float scale, float now)
	{
		// a new firing position starts a new volley
		if (vector.DistanceSq(s_vVolleyPos, muzzlePos) > VOLLEY_MOVE_RESET * VOLLEY_MOVE_RESET)
			s_aVolleyTime.Clear();
		s_vVolleyPos = muzzlePos;

		for (int i = s_aVolleyTime.Count() - 1; i >= 0; i--)
		{
			if (now - s_aVolleyTime[i] > VOLLEY_WINDOW)
				s_aVolleyTime.Remove(i);
		}
		s_aVolleyTime.Insert(now);

		if (s_aVolleyTime.Count() < VOLLEY_SHOTS)
			return;
		if (now - s_fLastCloudTime < CLOUD_COOLDOWN)
			return;

		// same conditions as the per-shot kickup: muzzle low over genuinely dusty ground
		array<IEntity> excludes = {};
		if (weaponEntity)
		{
			excludes.Insert(weaponEntity);
			IEntity root = weaponEntity.GetRootParent();
			if (root && ChimeraCharacter.Cast(root))
				excludes.Insert(root);
		}

		vector hitPos;
		string matName;
		IEntity hitRoot;
		if (!BER_SurfaceUtil.TraceGroundEx(world, muzzlePos, 3.0, excludes, hitPos, matName, hitRoot))
			return;
		if (hitRoot && Vehicle.Cast(hitRoot))
			return; // hull path has its own kickoff
		if (muzzlePos[1] - hitPos[1] > 0.7 + 0.65 * scale)
			return;
		if (matName == "")
			return;
		if (BER_SurfaceUtil.GetDustFactor(matName, hitPos[1]) * BER_SurfaceUtil.GetRainFactor(world) < 1.0)
			return;

		float lifeMult;
		bool allowDrift;
		ComputeEnvFactors(world, muzzlePos, weaponEntity, lifeMult, allowDrift);

		s_fLastCloudTime = now;

		// centered on the shooter's body, not the muzzle — the cloud envelops the firer
		vector cloudPos = Vector(muzzlePos[0], hitPos[1], muzzlePos[2]);
		if (fwdFlat != vector.Zero)
			cloudPos = cloudPos - fwdFlat * 0.5;

		ParticleEffectEntity cloud = SpawnPuff(RIFLE_CLOUD, cloudPos, vector.Up, 1.0, 1.0, lifeMult, false);

		// the envelop cloud lives seconds — long enough for wind to visibly carry it, and
		// tracked so concussions (more shots, explosions) sweep it like the ground puffs
		if (cloud)
		{
			s_aRiflePuffs.Insert(cloud);
			s_aRiflePuffTime.Insert(now);
			s_aRiflePuffBirth.Insert(1.0);
			s_aRiflePuffLife.Insert(lifeMult);

			if (allowDrift)
				BER_WindDriftAnimator.GetInstance().Register(cloud, 4.0, 1.0, lifeMult, 0.25);

			// overlap field: a fresh envelop cloud spawned onto a standing one folds into
			// it (the survivor thickens) instead of stacking a second overlapping cloud.
			// The per-round splashes stay OUT of the field — their pulse-per-shot rhythm
			// is the effect, and the concussion shoves already govern their overlap.
			BER_CloudField.GetInstance().Register(cloud, BER_CloudField.FAMILY_RIFLE_CLOUD, !allowDrift, BER_CloudField.NewGroup(), 1.0, 1.0, lifeMult);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The concussion of a fresh shot shoves every still-living small-arms puff near the
	//! muzzle FORWARD along the barrel (and thins it out via the animator's travel
	//! dissipation) — the muzzle gas of sustained fire drives the dust cloud outbound in
	//! the direction of fire, never sideways or upwind.
	protected static void PushRecentRiflePuffs(vector muzzlePos, vector fwdFlat, float now)
	{
		// the flash hider vents to the sides — each shot pushes the standing dust RADIALLY
		// away from the muzzle (gently, the gas pressure fades within a couple of meters),
		// not downrange; the wind carries it from there
		ShovePuffsInternal(muzzlePos, CONCUSSION_RADIUS, vector.Zero, 0.9, now);
	}

	//------------------------------------------------------------------------------------------------
	//! Shove every tracked small-arms puff around center: dir = the push direction, or
	//! vector.Zero for a radial blast wave away from center (explosion concussion).
	static void ShovePuffs(BaseWorld world, vector center, float radius, vector dir, float speed)
	{
		if (!world)
			return;
		ShovePuffsInternal(center, radius, dir, speed, world.GetWorldTime() * 0.001);
	}

	//------------------------------------------------------------------------------------------------
	protected static void ShovePuffsInternal(vector center, float radius, vector dir, float speed, float now)
	{
		for (int i = s_aRiflePuffs.Count() - 1; i >= 0; i--)
		{
			ParticleEffectEntity pfx = s_aRiflePuffs[i];
			if (!pfx || now - s_aRiflePuffTime[i] > RIFLE_PUFF_TRACK_TIME)
			{
				s_aRiflePuffs.Remove(i);
				s_aRiflePuffTime.Remove(i);
				s_aRiflePuffBirth.Remove(i);
				s_aRiflePuffLife.Remove(i);
				continue;
			}

			vector puffPos = pfx.GetOrigin();
			float dx = puffPos[0] - center[0];
			float dz = puffPos[2] - center[2];
			float dist = Math.Pow(dx * dx + dz * dz, 0.5);
			if (dist > radius)
				continue;

			vector shoveDir = dir;
			if (shoveDir == vector.Zero)
			{
				if (dist < 0.05)
					continue; // directly at the epicenter — no meaningful radial direction
				shoveDir = Vector(dx / dist, 0, dz / dist);
			}

			float shove = speed * (1.0 - dist / radius);
			if (shove < speed * 0.3)
				shove = speed * 0.3;

			BER_WindDriftAnimator.GetInstance().AddImpulse(pfx, shoveDir * shove, s_aRiflePuffBirth[i], s_aRiflePuffLife[i]);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Recover the true incoming direction of the shot that caused an impact at impactPos:
	//! newest recorded shot ray the impact point lies on (in front of the muzzle, within
	//! range, closest perpendicular distance — tolerance widens slightly with flight
	//! distance to cover ballistic drop and spread). False when no ray matches.
	static bool GetIncomingShotDir(BaseWorld world, vector impactPos, out vector outDir)
	{
		float scale;
		return GetIncomingShotInfo(world, impactPos, outDir, scale);
	}

	//------------------------------------------------------------------------------------------------
	//! Like GetIncomingShotDir, but also recovers the matched shot's weapon-class scale
	//! (0.2 pistol ... 0.36 5.56 ... 2.2 autocannon) so an impact can size its dust and
	//! structural-dust buildup by the round's caliber/destructive power.
	static bool GetIncomingShotInfo(BaseWorld world, vector impactPos, out vector outDir, out float outScale)
	{
		outDir = vector.Zero;
		outScale = 0;
		if (!world)
			return false;

		float now = world.GetWorldTime() * 0.001;
		float bestScore = 999;

		for (int i = s_aShotTime.Count() - 1; i >= 0; i--)
		{
			if (now - s_aShotTime[i] > SHOT_RAY_KEEP)
				continue; // pruning happens on record — just skip stale leftovers here

			vector toImpact = impactPos - s_aShotPos[i];
			float t = vector.Dot(toImpact, s_aShotDir[i]);
			if (t < 0.4 || t > SHOT_RAY_RANGE)
				continue;

			vector offRay = toImpact - s_aShotDir[i] * t;
			float perp = offRay.Length();
			float tolerance = 0.9 + 0.004 * t;
			if (perp > tolerance)
				continue;

			if (perp < bestScore)
			{
				bestScore = perp;
				outDir = s_aShotDir[i];
				outScale = s_aShotScale[i];
			}
		}

		return outDir != vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	//! Stationary hull blanket. A vehicle-mounted heavy weapon firing from a standstill
	//! wraps its own hull in kicked-up ground dust:
	//!  - the muzzle gas slams the foreground dirt ahead of the bow — a wall of fine sand
	//!    rises there and climbs the front armor (GLACIS_WASH: rising sand wall + fast
	//!    mist + grey gas wisps hugging the side skirts),
	//!  - every round of the burst is a bellows stroke: an impulse pumps the standing wash
	//!    up and over the hull toward the hatches/optics, alternating slightly left/right
	//!    so the cloud also piles along the sides,
	//!  - forward movement outruns the cloud, so everything fades with vehicle speed and
	//!    is gone above BLANKET_MAX_SPEED; wet/hard ground has nothing to give.
	//! The rear stays clear by construction — the wash only ever spawns along the muzzle
	//! direction. Runs pre-dedup so every shot pumps even when nothing new spawns.
	protected static void HullBlanket(BaseWorld world, Vehicle veh, IEntity weaponEntity, vector muzzlePos, vector fwdFlat, float scale, float now)
	{
		if (fwdFlat == vector.Zero)
			return;

		float speed = 0;
		Physics phys = veh.GetPhysics();
		if (phys)
			speed = phys.GetVelocity().Length();
		float speedFactor = 1.0 - speed / BLANKET_MAX_SPEED;
		if (speedFactor <= 0.05)
			return;

		// bellows pulse for every tracked wash near this muzzle
		vector side = Vector(fwdFlat[2], 0, -fwdFlat[0]);
		s_iWashPulse++;
		float sideSign = 1.0;
		if (s_iWashPulse % 2 == 0)
			sideSign = -1.0;
		vector pulseDir = fwdFlat * -0.55 + Vector(0, 0.5, 0) + side * (0.35 * sideSign);

		bool freshNearby = false;
		for (int i = s_aWashPfx.Count() - 1; i >= 0; i--)
		{
			ParticleEffectEntity wash = s_aWashPfx[i];
			if (!wash || now - s_aWashTime[i] > WASH_TRACK_TIME)
			{
				s_aWashPfx.Remove(i);
				s_aWashTime.Remove(i);
				s_aWashBirth.Remove(i);
				s_aWashLife.Remove(i);
				continue;
			}

			float distSq = vector.DistanceSq(wash.GetOrigin(), muzzlePos);
			if (distSq > 36.0)
				continue;

			if (now - s_aWashTime[i] < WASH_SPAWN_INTERVAL)
				freshNearby = true;

			BER_WindDriftAnimator.GetInstance().AddImpulse(wash, pulseDir * (1.5 * speedFactor), s_aWashBirth[i], s_aWashLife[i]);
		}

		if (freshNearby)
			return; // a wash is already rising here — this shot only pumped it

		// where the muzzle gas slams the dirt: first probe ahead of the muzzle whose
		// downward trace clears the vehicle's own hull and lands on the ground
		array<IEntity> excludes = {};
		excludes.Insert(weaponEntity);

		vector groundPos;
		string matName;
		IEntity hitRoot;
		bool found = false;
		for (int k = 0; k < 3; k++)
		{
			vector probe = muzzlePos + fwdFlat * (0.9 + 0.8 * k);
			if (!BER_SurfaceUtil.TraceGroundEx(world, probe, 4.0, excludes, groundPos, matName, hitRoot))
				continue;
			if (hitRoot && Vehicle.Cast(hitRoot))
				continue; // still over a hull — the gas hits armor here, not dirt
			found = true;
			break;
		}
		if (!found || matName == "")
			return;

		float dust = BER_SurfaceUtil.GetDustFactor(matName, groundPos[1]) * BER_SurfaceUtil.GetRainFactor(world);
		if (dust < 0.7)
			return; // wet or hard ground — nothing to blanket the hull with

		float lifeMult;
		bool allowDrift;
		ComputeEnvFactors(world, muzzlePos, weaponEntity, lifeMult, allowDrift);

		float sizeMult = BER_SurfaceUtil.ClampF(0.7 + 0.25 * scale, 0.8, 1.4);
		float birthMult = BER_SurfaceUtil.ClampF(dust, 0.7, 1.5) * speedFactor;

		ParticleEffectEntity wash = SpawnPuff(GLACIS_WASH, groundPos, vector.Up, sizeMult, birthMult, lifeMult, false, vector.Zero, 1.0, 1.0, fwdFlat);
		if (!wash)
			return;

		s_aWashPfx.Insert(wash);
		s_aWashTime.Insert(now);
		s_aWashBirth.Insert(birthMult);
		s_aWashLife.Insert(lifeMult);
	}

	//------------------------------------------------------------------------------------------------
	//! Heavy rounds (12.7mm and up) striking a vehicle shake accumulated dust off its hull
	//! just like a mounted gun's own concussion does — same kickoff, centered on the impact
	//! point. Shares the dedup list so an MG burst doesn't stack a kickoff per hit.
	static void KickoffOnVehicle(notnull Vehicle vehicle, vector center, float strength)
	{
		if (!VEHICLE_DUST_ENABLED)
			return; // parked — see the master switch note

		BaseWorld world = vehicle.GetWorld();
		if (!world)
			return;

		float now = world.GetWorldTime() * 0.001;
		for (int i = s_aRecentTime.Count() - 1; i >= 0; i--)
		{
			if (now - s_aRecentTime[i] > DEDUP_TIME)
			{
				s_aRecentTime.Remove(i);
				s_aRecentPos.Remove(i);
				continue;
			}
			if (vector.DistanceSq(s_aRecentPos[i], center) < DEDUP_RADIUS_SQ)
				return;
		}

		float ripAmount = BER_SurfaceUtil.ClampF(0.2 + 0.2 * strength, 0.1, 1.0);
		float avail = BER_DustReservoir.TakeVehicle(vehicle, world, ripAmount);
		bool faint = (avail < 0.55);
		if (avail < 0.22 && !TEST_HULL_DUST_ALWAYS_ON)
			return;
		if (TEST_HULL_DUST_ALWAYS_ON)
			faint = false;

		float lifeMult;
		bool allowDrift;
		ComputeEnvFactors(world, center + Vector(0, 0.5, 0), vehicle, lifeMult, allowDrift);

		s_aRecentPos.Insert(center);
		s_aRecentTime.Insert(now);

		array<IEntity> excludes = {};
		SpawnHullKickoff(world, vehicle, excludes, faint, center, vector.Zero, center, strength, lifeMult);
	}

	//------------------------------------------------------------------------------------------------
	//! Environment factors shared by every puff of one firing event: under a roof the dust
	//! hangs in still air (longer lifetime, no drift); outdoors the wind that will carry it
	//! away also dissipates it faster.
	protected static void ComputeEnvFactors(BaseWorld world, vector muzzlePos, IEntity exclude, out float lifeMult, out bool allowDrift)
	{
		if (BER_SurfaceUtil.IsRoofed(world, muzzlePos, exclude, 25.0))
		{
			lifeMult = 2.0;
			allowDrift = false;
			return;
		}

		float windNorm = BER_SurfaceUtil.ClampF(BER_SurfaceUtil.GetWindSpeed(world) / 10.0, 0, 1);
		lifeMult = 1.0 - 0.45 * windNorm;
		allowDrift = true;
	}

	//------------------------------------------------------------------------------------------------
	//! Kick dust off the vehicle as ONE continuous overpressure event:
	//!  - chassis: a SINGLE authored dust sheet covering the whole deck (two footprint
	//!    sizes; runtime emission-shape scaling is a dead API), spawned flat at the deck
	//!    plane — the median height of a probe grid, read dynamically so custom vehicles
	//!    work unmodified. No per-spot bursts: the film lifts off uniformly everywhere
	//!    and slides gently away from the muzzle as one front.
	//!  - muzzle region: traced surface puffs that do NOT exclude the weapon, so dust
	//!    sits on whatever is really there — gun barrel, muzzle brake, mantlet — and
	//!    rides the directed gas jet forward.
	protected static void SpawnHullKickoff(BaseWorld world, notnull IEntity vehicle, notnull array<IEntity> excludes, bool faint, vector muzzlePos, vector muzzleFwdFlat, vector hullHitPos, float weaponScale, float lifeMult)
	{
		float mainScale = 0.8 + 0.25 * weaponScale;
		if (mainScale > 1.6)
			mainScale = 1.6;
		if (faint)
			mainScale = mainScale * 0.7;

		// ---- muzzle region — the concussion is strongest right here, and the vented gas
		// is DIRECTED: puffs down the barrel line get shoved forward with the gas jet ----
		vector gasImpulse = vector.Zero;
		if (muzzleFwdFlat != vector.Zero)
			gasImpulse = muzzleFwdFlat * 0.8;

		SpawnSurfacePuff(world, vehicle, muzzlePos, mainScale, lifeMult, hullHitPos, gasImpulse);
		if (muzzleFwdFlat != vector.Zero)
		{
			// forward along the gas jet (only lands if hull is actually under the barrel)
			SpawnSurfacePuff(world, vehicle, muzzlePos + muzzleFwdFlat * 0.7, mainScale * 0.7, lifeMult, vector.Zero, gasImpulse * 1.2);
			SpawnSurfacePuff(world, vehicle, muzzlePos + muzzleFwdFlat * 1.4, mainScale * 0.5, lifeMult, vector.Zero, gasImpulse * 1.2);
			// back under the barrel/receiver
			SpawnSurfacePuff(world, vehicle, muzzlePos - muzzleFwdFlat * 0.7, mainScale * 0.55, lifeMult, vector.Zero, gasImpulse * 0.6);
			SpawnSurfacePuff(world, vehicle, muzzlePos - muzzleFwdFlat * 1.4, mainScale * 0.4, lifeMult, vector.Zero, gasImpulse * 0.4);
		}

		// ---- chassis scatter ----
		vector mins;
		vector maxs;
		vehicle.GetWorldBounds(mins, maxs);

		float width = maxs[0] - mins[0];
		float length = maxs[2] - mins[2];
		float topY = maxs[1];

		float insetX = width * 0.08 + 0.1;
		float insetZ = length * 0.08 + 0.1;
		float usableX = width - 2.0 * insetX;
		float usableZ = length - 2.0 * insetZ;
		if (usableX < 0.4)
			usableX = 0.4;
		if (usableZ < 0.4)
			usableZ = 0.4;

		int nx = Math.Ceil(usableX / 1.0);
		int nz = Math.Ceil(usableZ / 1.0);
		if (nx < 1) nx = 1;
		if (nx > 4) nx = 4;
		if (nz < 1) nz = 1;
		if (nz > 6) nz = 6;

		float cellX = usableX / nx;
		float cellZ = usableZ / nz;

		// probe every cell, keep the exact surface point + normal it lands on
		array<vector> probePos = {};
		array<vector> probeNorm = {};
		array<float> probeHeight = {};
		for (int ix = 0; ix < nx; ix++)
		{
			for (int iz = 0; iz < nz; iz++)
			{
				vector start = Vector(
					mins[0] + insetX + (ix + 0.5) * cellX,
					topY + 0.6,
					mins[2] + insetZ + (iz + 0.5) * cellZ);

				TraceParam tp = new TraceParam();
				tp.Start = start;
				tp.End = start - Vector(0, 3.5, 0);
				tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
				tp.ExcludeArray = excludes;
				float frac = world.TraceMove(tp, null);
				if (frac >= 1.0)
					continue;
				if (!tp.TraceEnt || tp.TraceEnt.GetRootParent() != vehicle)
					continue; // probe is off the hull (AABB corner, gun barrel gap, ...)

				vector dir = tp.End - tp.Start;
				vector hit = tp.Start + dir * frac;
				probePos.Insert(hit);
				probeNorm.Insert(tp.TraceNorm);
				probeHeight.Insert(hit[1]);
			}
		}
		if (probePos.IsEmpty())
			return;

		// the main chassis deck = the median probe height (probes on micro details — turret
		// roof, hatches, stowage — sit off this plane and don't skew the median much)
		array<float> sortedHeights = {};
		foreach (float h : probeHeight)
			sortedHeights.Insert(h);
		sortedHeights.Sort();
		float deckY = sortedHeights[sortedHeights.Count() / 2];

		// ---- ONE continuous dust sheet across the whole deck — uniform by construction ----
		ResourceName sheetRes = HULL_SHEET_M;
		if (length >= 5.6)
			sheetRes = HULL_SHEET_L;

		// aligned to the vehicle so the authored footprint (narrow X, long Z) lies along
		// the hull; positioned at the deck plane
		vector vmat[4];
		vehicle.GetWorldTransform(vmat);
		vector center = (mins + maxs) * 0.5;

		ParticleEffectEntitySpawnParams sheetParams = new ParticleEffectEntitySpawnParams();
		sheetParams.UseFrameEvent = true;
		sheetParams.PlayOnSpawn = false;
		sheetParams.Transform[0] = vmat[0];
		sheetParams.Transform[1] = vmat[1];
		sheetParams.Transform[2] = vmat[2];
		sheetParams.Transform[3] = Vector(center[0], deckY + 0.04, center[2]);

		ParticleEffectEntity sheet = ParticleEffectEntity.SpawnParticleEffect(sheetRes, sheetParams);
		if (!sheet)
			return;

		BER_OwnedEffects.MarkOwned(sheet); // the striking round's warhead must not adopt/reorient the deck sheet

		float sheetSize = 0.8 + 0.15 * weaponScale;
		if (sheetSize > 1.2)
			sheetSize = 1.2;
		if (faint)
			sheetSize = sheetSize * 0.6; // depleted film — uniformly thinner, never holes

		Particles sheetParticles = sheet.GetParticles();
		if (sheetParticles)
		{
			int n = sheetParticles.GetNumEmitters();
			for (int i = 0; i < n; i++)
			{
				sheetParticles.MultParam(i, EmitterParam.SIZE, sheetSize);
				sheetParticles.MultParam(i, EmitterParam.LIFETIME, lifeMult);
				sheetParticles.MultParam(i, EmitterParam.LIFETIME_RND, lifeMult);
			}
		}
		sheet.Play();

		// the whole film slides gently away from the muzzle as one pressure front
		float dxc = center[0] - muzzlePos[0];
		float dzc = center[2] - muzzlePos[2];
		float distC = Math.Pow(dxc * dxc + dzc * dzc, 0.5);
		if (distC > 0.05)
			BER_WindDriftAnimator.GetInstance().AddImpulse(sheet, Vector(dxc / distC, 0, dzc / distC) * 0.35, 1.0, lifeMult);
	}

	//------------------------------------------------------------------------------------------------
	//! One kickoff puff on the exact vehicle surface under probePos. The trace deliberately
	//! excludes NOTHING, so the puff can sit on the gun itself (barrel, muzzle brake) —
	//! only people are rejected. Falls back to a pre-traced hull point if given. A non-zero
	//! impulse shoves the fresh puff along the vented-gas direction.
	protected static void SpawnSurfacePuff(BaseWorld world, notnull IEntity vehicle, vector probePos, float sizeScale, float lifeMult, vector fallbackPos, vector impulse = vector.Zero)
	{
		TraceParam tp = new TraceParam();
		tp.Start = probePos + Vector(0, 0.3, 0);
		tp.End = tp.Start - Vector(0, 2.8, 0);
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		float frac = world.TraceMove(tp, null);

		vector pos = vector.Zero;
		vector norm = vector.Up;
		IEntity hitEnt = tp.TraceEnt;
		bool onVehicle = false;
		if (frac < 1.0 && hitEnt && !ChimeraCharacter.Cast(hitEnt) && !ChimeraCharacter.Cast(hitEnt.GetRootParent()))
			onVehicle = (hitEnt.GetRootParent() == vehicle);

		if (onVehicle)
		{
			pos = tp.Start + (tp.End - tp.Start) * frac + Vector(0, 0.01, 0);
			norm = tp.TraceNorm;
		}
		else if (fallbackPos != vector.Zero)
		{
			pos = fallbackPos + Vector(0, 0.01, 0);
		}
		else
		{
			return;
		}

		ParticleEffectEntity pfx = SpawnPuff(HULL_KICKOFF, pos, norm, sizeScale, 1.0, lifeMult, false);
		if (pfx && impulse != vector.Zero)
			BER_WindDriftAnimator.GetInstance().AddImpulse(pfx, impulse, 1.0, lifeMult);
	}

	//------------------------------------------------------------------------------------------------
	//! Re-resolve surface and ground height at the offset position, then spawn a small puff
	protected static void SpawnOffsetPuff(BaseWorld world, IEntity exclude, vector approxPos, float lifeMult, bool allowDrift)
	{
		vector groundPos;
		string matName;
		vector groundNormal;
		ResourceName blastRes = BER_SurfaceUtil.GetBlastResourceAt(world, approxPos + Vector(0, 1.0, 0), 3.6, exclude, BLAST_INDEX_SMALL, groundPos, matName, groundNormal);
		if (blastRes == ResourceName.Empty)
			return;

		SpawnPuff(blastRes, groundPos, groundNormal, 1.0, 1.0, lifeMult, allowDrift);
	}

	//------------------------------------------------------------------------------------------------
	//! Spawns a puff aligned to the surface normal (slopes stop producing horizontal disks),
	//! paused so size/count/lifetime scaling is applied before the first particle exists.
	//! A non-zero emitBox resizes every emitter's emission shape. A non-zero fwdDir also
	//! yaws the effect so its local Z axis points along fwdDir projected onto the surface —
	//! effects with an elongated authored footprint stretch along the muzzle-gas direction.
	protected static ParticleEffectEntity SpawnPuff(ResourceName blastRes, vector pos, vector surfaceNormal, float sizeMult, float birthMult, float lifeMult, bool allowDrift, vector emitBox = vector.Zero, float shapeMult = 1.0, float velMult = 1.0, vector fwdDir = vector.Zero)
	{
		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		spawnParams.PlayOnSpawn = false;

		vector up = surfaceNormal;
		if (up == vector.Zero)
			up = vector.Up;

		bool oriented = false;
		if (fwdDir != vector.Zero)
		{
			// full basis: Y = surface normal, Z = gas direction flattened onto the surface
			float dot = fwdDir[0] * up[0] + fwdDir[1] * up[1] + fwdDir[2] * up[2];
			vector fwdProj = fwdDir - up * dot;
			float len = fwdProj.Length();
			if (len > 0.01)
			{
				vector fwdN = fwdProj * (1.0 / len);
				vector side = Vector(
					up[1] * fwdN[2] - up[2] * fwdN[1],
					up[2] * fwdN[0] - up[0] * fwdN[2],
					up[0] * fwdN[1] - up[1] * fwdN[0]);
				spawnParams.Transform[0] = side;
				spawnParams.Transform[1] = up;
				spawnParams.Transform[2] = fwdN;
				oriented = true;
			}
		}
		if (!oriented && up != vector.Up)
			SCR_EntityHelper.OrientUpToVector(up, spawnParams.Transform);
		spawnParams.Transform[3] = pos;

		ParticleEffectEntity pfx = ParticleEffectEntity.SpawnParticleEffect(blastRes, spawnParams);
		if (!pfx)
			return null;

		BER_OwnedEffects.MarkOwned(pfx); // muzzle dust tunes itself — warhead adoption must not touch it

		if (sizeMult != 1.0 || birthMult != 1.0 || lifeMult != 1.0 || emitBox != vector.Zero || shapeMult != 1.0 || velMult != 1.0)
		{
			Particles particles = pfx.GetParticles();
			if (particles)
			{
				int emitterCount = particles.GetNumEmitters();
				for (int i = 0; i < emitterCount; i++)
				{
					particles.MultParam(i, EmitterParam.SIZE, sizeMult);
					particles.MultParam(i, EmitterParam.BIRTH_RATE, birthMult);
					particles.MultParam(i, EmitterParam.LIFETIME, lifeMult);
					particles.MultParam(i, EmitterParam.LIFETIME_RND, lifeMult);
					if (emitBox != vector.Zero)
						particles.SetParam(i, EmitterParam.SHAPE_SIZE, emitBox);
					if (shapeMult != 1.0)
						particles.MultParam(i, EmitterParam.SHAPE_SIZE, shapeMult);
					if (velMult != 1.0)
						particles.MultParam(i, EmitterParam.VELOCITY, velMult);
				}
			}
		}

		pfx.Play();

		// BER-overridden blast effects are wind-free/local-space — give them the same
		// pressure-hold + gradual wind acceleration as explosion dust; under a roof the
		// still air holds them in place instead
		if (allowDrift)
			BER_WindDriftAnimator.GetInstance().Register(pfx, 12, birthMult, lifeMult);

		return pfx;
	}
}
