//------------------------------------------------------------------------------------------------
// Better Effects Realism — vehicle driving effects
//
// 1) Hard dust cutoff at 10 km/h — below it no dust renders at all; above it the buildup
//    scales aggressively with speed: birth rate AND particle starting size grow with
//    speed (runtime SIZE MultParam), so at high speed the dust engulfs the wheelbase.
// 2) Paved roads shed NO dust cloud — only a very subtle rock kick, COLOR-MATCHED to the
//    surface (no runtime color param exists, so three authored stone variants are picked
//    by material name). The decision must be script-side: concrete/tiles share the
//    "default" dust .ptc and cobblestone shares "stone" with genuinely dusty terrain.
// 3) Dusty tires: switching from a dusty surface onto clean pavement keeps shedding the
//    previous surface's dust for a short moment before the rock kick takes over.
// 4) Tail wake: the low-pressure zone right behind the tail end drags kicked-up dust
//    along — a wake emitter follows behind the rear bumper on dusty ground, its
//    particles inheriting part of the vehicle's velocity (dragged, then released).
// 5) Exhaust models real engine behavior, all of it PIPE-RELATIVE: more gas at higher
//    ejection velocity as RPM rises (with air resistance coupled so the jet stalls
//    instead of sailing), cool idle exhaust lingering while hot high-RPM gas thins
//    fast (EGT), and a dense soot cloud when lugging (high load at low RPM).
//    An earlier flat velocity boost (uncoupled from drag, VELOCITY_RND included)
//    launched particles visibly forward — never boost exhaust velocity without drag.
// 6) Wheel-cloud merge share: wheels whose dust clouds overlap (same effect, contact
//    points close together — paired axles, tandem wheels in one track) stop emitting at
//    full rate each; the group redistributes into fewer-but-larger particles with the
//    total dust mass conserved, so the merged trail keeps the correct size and density
//    instead of stacking N identical overlapping clouds.
// 7) Rain: light rain thins the dust cloud with intensity; past the mud threshold the
//    wheels sling dark wet MUD clods instead (chunk size grows with speed), the tail
//    wake stays off, and muddy tires carried onto pavement keep slinging shrinking mud
//    until the grey paved rock kick takes over.
//------------------------------------------------------------------------------------------------

modded class VehicleDust
{
	int m_iBerDustyUntil;             // tick (ms) until which dusty-tire carryover keeps shedding
	ResourceName m_BerLastDustyRes;   // the dust effect of the last dusty surface driven on
	ResourceName m_BerCurrentRes;     // what this wheel's effect entity is actually playing
	bool m_bBerCarryActive;           // currently shedding carryover dust on pavement (fades out)
}

modded class SCR_VehicleDustPerWheel
{
	protected ParticleEffectEntity m_BerTailWake;
	protected bool m_bBerWakeOffsetComputed;
	protected vector m_vBerWakeOffset;

	protected const float BER_DUST_CUTOFF_KMH = 10.0;  // below this no dust renders at all
	protected const int BER_TIRE_CARRY_MS = 7000;      // dusty tires shed this long on pavement, fading out
	protected const float BER_WAKE_MIN_SPEED = 18.0;   // km/h below which no tail wake forms
	protected const float BER_WHEEL_MERGE_DIST_SQ = 4.84; // 2.2 m — wheel contacts closer than this share one merged cloud

	protected const ResourceName BER_TAIL_WAKE = "{BE20250903AC0032}Particles/BER/BER_TailWake.ptc";
	protected const ResourceName BER_KICK_DARK = "{BE20250903AC0033}Particles/BER/BER_PavedRockKick.ptc";
	protected const ResourceName BER_KICK_LIGHT = "{BE20250903AC0034}Particles/BER/BER_PavedRockKick_Light.ptc";
	protected const ResourceName BER_KICK_RED = "{BE20250903AC0035}Particles/BER/BER_PavedRockKick_Red.ptc";

	// rain-soaked unpaved ground kicks up dark wet mud clods instead of a dust cloud —
	// scaled-up rock chunks in a wet mud color, chunk size growing with speed. Gated on
	// the persistent GROUND WETNESS (not falling rain): the ground has to soak before it
	// turns to mud, and it stays muddy through the whole drying time after the rain ends.
	protected const ResourceName BER_MUD_KICK = "{BE20250903AC0036}Particles/BER/BER_MudKick.ptc";
	protected const float BER_MUD_WETNESS_MIN = 0.25; // ground wetness at which dusty ground turns to mud

	//------------------------------------------------------------------------------------------------
	//! Rock kick color-matched to the paved surface: dark asphalt, light concrete/tiles,
	//! reddish brick; cobblestone sits between — the dark set reads best on it.
	protected ResourceName GetPavedKickVariant(string matName)
	{
		matName.ToLower();
		if (matName.Contains("brick"))
			return BER_KICK_RED;
		if (matName.Contains("concrete") || matName.Contains("tiles") || matName.Contains("pavement")
			|| matName.Contains("paving") || matName.Contains("sidewalk"))
			return BER_KICK_LIGHT;
		return BER_KICK_DARK; // asphalt, cobblestone
	}

	//------------------------------------------------------------------------------------------------
	//! Reimplemented: hard 10 km/h cutoff, own resource state machine (paved surfaces get
	//! the color-matched rock kick instead of a dust cloud; a fresh transition from dusty
	//! ground keeps shedding the tires' dust for a short carryover window).
	override protected void UpdateEffect(VehicleDust vehicleDust, int index, float speed, float distanceFromCamera)
	{
		int wheelIdx = m_ComponentData.m_aWheels[index];

		if (speed < BER_DUST_CUTOFF_KMH || distanceFromCamera >= m_ComponentData.m_fMaxDistanceVisibleSqr)
		{
			vehicleDust.m_Material = null;
			if (vehicleDust.m_pParticleEffectEntity)
			{
				vehicleDust.m_pParticleEffectEntity.StopEmission();
				UpdatePosition(vehicleDust, wheelIdx);
			}
			return;
		}

		int ticks = System.GetTickCount();
		int dif = Math.AbsInt(ticks - vehicleDust.m_iLastSwap);

		if (vehicleDust.m_pParticleEffectEntity && dif < UPDATE_TIMEOUT)
		{
			UpdateCurrent(vehicleDust, speed, wheelIdx);
			return;
		}

		bool isLiquid = m_Simulation.WheelGetContactLiquidState(wheelIdx) > 0;
		GameMaterial newMaterial;
		if (isLiquid)
			newMaterial = m_Simulation.WheelGetContactLiquidMaterial(wheelIdx);
		else
			newMaterial = m_Simulation.WheelGetContactMaterial(wheelIdx);

		vehicleDust.m_bWheelHasContact = m_Simulation.WheelHasContact(wheelIdx);
		vehicleDust.m_Material = newMaterial;

		// resolve what this wheel SHOULD be playing right now
		ResourceName wantResource;
		vehicleDust.m_bBerCarryActive = false;
		if (newMaterial)
		{
			string matName = newMaterial.GetName();
			if (!isLiquid && BER_SurfaceUtil.IsPavedSurface(matName))
			{
				if (vehicleDust.m_iBerDustyUntil != 0 && ticks < vehicleDust.m_iBerDustyUntil
					&& vehicleDust.m_BerLastDustyRes != ResourceName.Empty)
				{
					// dusty tires still shedding — fades out as a gradient, not a hard cut
					wantResource = vehicleDust.m_BerLastDustyRes;
					vehicleDust.m_bBerCarryActive = true;
				}
				else
				{
					wantResource = GetPavedKickVariant(matName);
				}
			}
			else
			{
				ParticleEffectInfo effectInfo = newMaterial.GetParticleEffectInfo();
				if (effectInfo)
					wantResource = effectInfo.GetVehicleDustResource(m_ComponentData.m_iVehicleIndex);

				// soaked ground has no dust to give — the wheels sling wet mud clods
				// instead. Recorded as the "dusty" carryover resource too, so muddy tires
				// keep slinging mud onto pavement, shrinking until the grey rock kick
				// takes over.
				if (!isLiquid && wantResource != ResourceName.Empty
					&& BER_SurfaceUtil.GetGroundWetness(GetOwner().GetWorld()) >= BER_MUD_WETNESS_MIN)
					wantResource = BER_MUD_KICK;

				// remember the dust/mud the tires just picked up (liquids don't dust the tires)
				if (!isLiquid && wantResource != ResourceName.Empty)
				{
					vehicleDust.m_BerLastDustyRes = wantResource;
					vehicleDust.m_iBerDustyUntil = ticks + BER_TIRE_CARRY_MS;
				}
			}
		}

		if (vehicleDust.m_pParticleEffectEntity && vehicleDust.m_BerCurrentRes == wantResource)
		{
			UpdateCurrent(vehicleDust, speed, wheelIdx);
			return;
		}

		if (vehicleDust.m_pParticleEffectEntity)
		{
			vehicleDust.m_pParticleEffectEntity.StopEmission();
			vehicleDust.m_pParticleEffectEntity = null;
		}
		vehicleDust.m_BerCurrentRes = wantResource;

		if (wantResource && wantResource.Length() > 0)
		{
			ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
			spawnParams.TargetWorld = GetOwner().GetWorld();
			spawnParams.Parent = GetOwner();
			spawnParams.UseFrameEvent = true;
			vehicleDust.m_pParticleEffectEntity = ParticleEffectEntity.SpawnParticleEffect(wantResource, spawnParams);
			vehicleDust.m_iLastSwap = ticks;
		}

		UpdateCurrent(vehicleDust, speed, wheelIdx);
	}

	//------------------------------------------------------------------------------------------------
	//! Reimplemented: steeper speed-driven buildup than vanilla, and the particle STARTING
	//! size grows with speed too — at high speed the dust engulfs the whole wheelbase.
	override protected void UpdateVehicleDustEffect(VehicleDust vehicleDust, float speed, int wheelIdx)
	{
		if (!vehicleDust.m_pParticleEffectEntity)
			return;

		float endSpeed = m_ComponentData.m_fDustTopSpeed;
		float speedCoef = 0;
		float birthCoef = 0;
		float gravityCoef = 0;
		float sizeCoef = 1;

		float longitudinalSlip = m_Simulation.WheelGetLongitudinalSlip(wheelIdx);
		float lateralSlip = m_Simulation.WheelGetLateralSlip(wheelIdx);
		float effectiveSlip = Math.Clamp(longitudinalSlip + lateralSlip, 0, 1);

		bool isMud = (vehicleDust.m_BerCurrentRes == BER_MUD_KICK);
		bool isRockKick = (vehicleDust.m_BerCurrentRes == BER_KICK_DARK
			|| vehicleDust.m_BerCurrentRes == BER_KICK_LIGHT
			|| vehicleDust.m_BerCurrentRes == BER_KICK_RED);

		if (vehicleDust.m_bWheelHasContact)
		{
			float speedNorm = Math.Clamp(speed / endSpeed, 0, 1.2);
			speedCoef = Math.AbsFloat(0.5 + speedNorm * 0.5 + effectiveSlip);
			birthCoef = Math.AbsFloat(0.35 + speedNorm * 1.15 + effectiveSlip * 2);
			gravityCoef = Math.AbsFloat(0.8 + speedNorm * 0.2);
			sizeCoef = 0.7 + speedNorm * 0.8;

			// mud clods grow harder with speed than dust does — a crawling wheel drops
			// small spatter, a speeding one slings full clods
			if (isMud)
				sizeCoef = 0.55 + speedNorm * 1.25;
		}

		// wet ground generates NO dust: the cloud fades out linearly over the wetness
		// range below the mud threshold and is fully gone exactly where the mud kick
		// takes over. Mud and rock kicks are wet/solid effects and stay.
		if (!isMud && !isRockKick && vehicleDust.m_bWheelHasContact)
		{
			BaseWorld dustWorld = GetOwner().GetWorld();
			float dustWet = BER_SurfaceUtil.GetRainIntensity(dustWorld);
			float dustGround = BER_SurfaceUtil.GetGroundWetness(dustWorld);
			if (dustGround > dustWet)
				dustWet = dustGround;
			birthCoef = birthCoef * BER_SurfaceUtil.ClampF(1.0 - dustWet / BER_MUD_WETNESS_MIN, 0, 1);
		}

		// merge share: wheels shedding the SAME dust with contact points close enough
		// that their clouds fully overlap (a side-by-side pair, tandem axles in one
		// track) are one merged cloud — each member emits fewer particles (n^-0.5) but
		// bigger ones (n^0.18, ~mass-conserving: count x size^3 stays put), so the
		// group reads as one correctly sized and dense trail instead of n stacked ones
		if (vehicleDust.m_bWheelHasContact && vehicleDust.m_BerCurrentRes != ResourceName.Empty)
		{
			vector myContact = m_Simulation.WheelGetContactPosition(wheelIdx);
			int group = 1;
			foreach (int otherIndex, VehicleDust other : m_aVehicleDusts)
			{
				if (other == vehicleDust || !other.m_pParticleEffectEntity || !other.m_bWheelHasContact)
					continue;
				if (other.m_BerCurrentRes != vehicleDust.m_BerCurrentRes)
					continue;
				int otherWheelIdx = m_ComponentData.m_aWheels[otherIndex];
				if (vector.DistanceSq(m_Simulation.WheelGetContactPosition(otherWheelIdx), myContact) < BER_WHEEL_MERGE_DIST_SQ)
					group++;
			}
			if (group > 1)
			{
				birthCoef = birthCoef * Math.Pow(group, -0.5);
				sizeCoef = sizeCoef * Math.Pow(group, 0.18);
			}
		}

		// carryover onto pavement, live per tick (not the 1 Hz resource cadence):
		//  - muddy tires keep slinging mud whose SIZE shrinks over the carry window; when
		//    it bottoms out the resource state machine swaps to the grey paved rock kick
		//  - dusty tires thin their cloud out via birth rate, as before
		if (vehicleDust.m_bBerCarryActive)
		{
			float remainMs = vehicleDust.m_iBerDustyUntil - System.GetTickCount();
			float remainFrac = BER_SurfaceUtil.ClampF(remainMs / BER_TIRE_CARRY_MS, 0, 1);
			if (isMud)
				sizeCoef = sizeCoef * (0.4 + 0.6 * remainFrac);
			else
				birthCoef = birthCoef * remainFrac;
		}

		Particles particles = vehicleDust.m_pParticleEffectEntity.GetParticles();
		particles.MultParam(-1, EmitterParam.BIRTH_RATE, birthCoef);
		particles.MultParam(-1, EmitterParam.GRAVITY_SCALE_RND, gravityCoef);
		particles.MultParam(-1, EmitterParam.VELOCITY, speedCoef);
		particles.MultParam(-1, EmitterParam.VELOCITY_RND, speedCoef);
		particles.MultParam(-1, EmitterParam.SIZE, sizeCoef);
	}

	//------------------------------------------------------------------------------------------------
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);
		UpdateTailWake();
	}

	//------------------------------------------------------------------------------------------------
	//! Maintain the low-pressure tail wake: alive while the vehicle moves at speed over
	//! dusty (non-paved) ground, stopped otherwise. Intensity follows speed.
	protected void UpdateTailWake()
	{
		if (!m_Simulation || !m_Simulation.IsValid())
			return;

		float speed = m_Simulation.GetSpeedKmh();
		bool want = false;

		if (speed >= BER_WAKE_MIN_SPEED && m_Simulation.WheelHasContact(0))
		{
			GameMaterial mat = m_Simulation.WheelGetContactMaterial(0);
			if (mat)
			{
				string matName = mat.GetName();
				if (!BER_SurfaceUtil.IsPavedSurface(matName)
					&& BER_SurfaceUtil.GetDustFactor(matName, GetOwner().GetOrigin()[1])
						* BER_SurfaceUtil.GetRainFactor(GetOwner().GetWorld()) >= 1.15)
					want = true; // rain-wet ground drags no dust wake
			}
		}

		if (!want)
		{
			if (m_BerTailWake)
			{
				m_BerTailWake.StopEmission();
				m_BerTailWake = null;
			}
			return;
		}

		if (!m_BerTailWake)
		{
			if (!m_bBerWakeOffsetComputed)
			{
				m_bBerWakeOffsetComputed = true;
				vector mins, maxs;
				GetOwner().GetBounds(mins, maxs);
				m_vBerWakeOffset = Vector(0, 0.35, mins[2] - 0.5); // just behind the tail end
			}

			ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
			spawnParams.UseFrameEvent = true;
			spawnParams.FollowParent = GetOwner();
			spawnParams.Transform[3] = m_vBerWakeOffset; // LOCAL to the followed vehicle
			m_BerTailWake = ParticleEffectEntity.SpawnParticleEffect(BER_TAIL_WAKE, spawnParams);
			if (!m_BerTailWake)
				return;
			BER_OwnedEffects.MarkOwned(m_BerTailWake);
		}

		Particles particles = m_BerTailWake.GetParticles();
		if (particles)
		{
			float coef = BER_SurfaceUtil.ClampF(0.35 + (speed - BER_WAKE_MIN_SPEED) / 60.0, 0.35, 1.3);
			particles.MultParam(-1, EmitterParam.BIRTH_RATE, coef);
			particles.MultParam(-1, EmitterParam.VELOCITY, coef);
		}
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_MotorExhaustEffectGeneralComponent
{
	//------------------------------------------------------------------------------------------------
	//! Reimplemented with real engine response, everything relative to the pipe (no vehicle
	//! velocity is ever added):
	//!  - volume: more fuel burned per minute at high RPM = birth rate rises harder than
	//!    vanilla's 0.5+0.5, so idle wisps and redline gushes actually read differently
	//!  - ejection velocity: floored at idle (vanilla's RPMx3 leaves idle exhaust dead),
	//!    rising with RPM but capped below vanilla's max, with air resistance coupled to
	//!    RPM alongside it — the jet punches out of the pipe then stalls in the air.
	//!    The old flat x3.6 boost (VELOCITY_RND included, drag untouched) is what made
	//!    exhaust particles sail visibly forward.
	//!  - EGT: cool idle exhaust is condensing vapor that lingers; hot high-RPM gas
	//!    thins out quicker
	//!  - lugging: high engine load at low RPM dumps unburned fuel = a much denser,
	//!    longer-hanging soot cloud (no runtime color param exists to blacken it; the
	//!    vanilla load-based stage pick already selects the heavier authored emitters)
	override protected void AdjustEngineEffects(notnull ParticleEffectEntity effectEntity, array<ref array<int>> stageIndexes)
	{
		Particles particles = effectEntity.GetParticles();
		if (!particles)
			return;

		float birthCoef = 0.35 + 1.05 * m_fRPMScaled;
		float velCoef = 0.5 + 1.6 * m_fRPMScaled;
		float velRndCoef = 0.5 + 0.8 * m_fRPMScaled;
		float dragCoef = 0.4 + 0.6 * m_fRPMScaled;
		float lifeCoef = 1.15 - 0.45 * m_fRPMScaled;

		if (m_fEngineLoad >= 0.7 && m_fRPMScaled <= 0.35)
		{
			birthCoef = birthCoef * 1.6;
			lifeCoef = lifeCoef * 1.3;
		}

		if (stageIndexes && !stageIndexes.IsEmpty())
		{
			particles.SetParam(-1, EmitterParam.BIRTH_RATE, 0.0);
			particles.SetParam(-1, EmitterParam.BIRTH_RATE_RND, 0.0);

			int iMaxStage = stageIndexes.Count();
			int stage = Math.ClampInt(Math.Ceil(iMaxStage * m_fEngineLoad), 1, iMaxStage) - 1;
			array<int> stageEmitterIDs = stageIndexes[stage];

			for (int i; i < stageEmitterIDs.Count(); i++)
			{
				particles.MultParam(stageEmitterIDs[i], EmitterParam.BIRTH_RATE, birthCoef);
				particles.MultParam(stageEmitterIDs[i], EmitterParam.BIRTH_RATE_RND, birthCoef);
				particles.MultParam(stageEmitterIDs[i], EmitterParam.VELOCITY, velCoef);
				particles.MultParam(stageEmitterIDs[i], EmitterParam.VELOCITY_RND, velRndCoef);
				particles.MultParam(stageEmitterIDs[i], EmitterParam.AIR_RESISTANCE, dragCoef);
				particles.MultParam(stageEmitterIDs[i], EmitterParam.AIR_RESISTANCE_RND, dragCoef);
				particles.MultParam(stageEmitterIDs[i], EmitterParam.LIFETIME, lifeCoef);
				particles.MultParam(stageEmitterIDs[i], EmitterParam.LIFETIME_RND, lifeCoef);
			}
		}
		else
		{
			// unstaged effects: vanilla multiplies LIFETIME by m_fLifetimeScale here, but that
			// field is never assigned (0) — using the EGT coefficient alone instead
			particles.MultParam(-1, EmitterParam.BIRTH_RATE, birthCoef);
			particles.MultParam(-1, EmitterParam.BIRTH_RATE_RND, birthCoef);
			particles.MultParam(-1, EmitterParam.LIFETIME, lifeCoef);
			particles.MultParam(-1, EmitterParam.LIFETIME_RND, lifeCoef);
			particles.MultParam(-1, EmitterParam.VELOCITY, velCoef);
			particles.MultParam(-1, EmitterParam.VELOCITY_RND, velRndCoef);
			particles.MultParam(-1, EmitterParam.AIR_RESISTANCE, dragCoef);
			particles.MultParam(-1, EmitterParam.AIR_RESISTANCE_RND, dragCoef);
		}
	}
}
