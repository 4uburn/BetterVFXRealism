// Better VFX Realism: opt-in native review fixture; all modes default off.
// Bounded regression, firearm and fragment checks; deletion cancels callbacks and test entities.

[EntityEditorProps(category: "GameScripted/BetterVFXRealism", description: "Review: opt-in fragment visual test spawner")]
class BER_FragTestSpawnerClass : ScriptComponentClass
{
}

class BER_FragTestSpawner : ScriptComponent
{
	[Attribute("0", desc: "Enable one test detonation after entering play mode (regression mode must be off)")]
	protected bool m_bEnabled;
	[Attribute("0", desc: "Run bounded native VFX regression checks in play mode (60-120 seconds)")]
	protected bool m_bRunRegressionTests;
	[Attribute("0", desc: "Check the walls/roof in BER_Review.ent and use its inspection camera")]
	protected bool m_bReviewRoom;
	[Attribute("0", desc: "Fire a native rifle and suppressed variant in the review room (requires regression mode)")]
	protected bool m_bFirearmTest;
	[Attribute("0", desc: "Move a burning native smoke grenade through the review room boundary")]
	protected bool m_bSmokeDeviceTest;
	protected IEntity m_TestSmoke;
	protected ParticleEffectEntity m_NativeSmoke;
	protected int m_iSmokeStage;
	protected float m_fSmokeClock;
	protected IEntity m_TestShooter;
	protected BaseMuzzleComponent m_TestMuzzle;
	protected int m_iFireTicks;
	protected int m_iAmmoStart;
	protected int m_iPlainShots;
	protected int m_iSuppressedShots;
	protected int m_iPlainGas;
	protected int m_iSuppressedGas;
	protected float m_fPlainGasRate;
	protected float m_fSuppressedGasRate;
	protected bool m_bSuppressedStage;
	protected ref array<ParticleEffectEntity> m_ObservedGas = {};
	[Attribute("60", desc: "Native regression sampling duration in seconds", params: "60 120 1")]
	protected float m_fRegressionDuration;
	protected int m_iFailures;
	protected IEntity m_TestWarhead;
	protected ParticleEffectEntity m_TestDrift;
	protected vector m_vDriftStart;
	protected bool m_bDriftChecked;
	protected int m_iFragmentSamples;
	protected int m_iFragmentPeak;
	protected int m_iDebrisPeak;
	protected int m_iFragmentSprites;
	protected int m_iDebrisSprites;
	protected ref array<ParticleEffectEntity> m_ObservedFragments = {};
	protected int m_iSamples;
	protected int m_iHazeCreated;
	protected int m_iHazePeak;
	protected int m_iHazeSpritesPeak;
	protected vector m_vTestHit;
	protected vector m_vTestNormal;
	protected ref array<ParticleEffectEntity> m_RoomLayers = {};
	protected float m_fTestStart;
	protected ref array<ParticleEffectEntity> m_TestEffects = {};
	protected ref array<int> m_PeakCounts = {};
	protected ref BER_RoomDustState m_TestRoom;
	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (m_bRunRegressionTests)
		{
			GetGame().GetCallqueue().CallLater(RunRegressionTests, 1000, false);
			return; // keep the destructive grenade demonstration separate from the live rifle checks
		}
		if (!m_bEnabled)
			return;
		// CallLater never ticks in edit mode, so this only ever fires in play
		GetGame().GetCallqueue().CallLater(SpawnTestWarhead, 8000, false);
		Print("BER DIAG frag: TEST spawner armed (detonation in 8 s)");
	}

	//------------------------------------------------------------------------------------------------
	void ~BER_FragTestSpawner()
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(SpawnTestWarhead);
			GetGame().GetCallqueue().Remove(SampleFragmentBurst);
			GetGame().GetCallqueue().Remove(RunRegressionTests);
			GetGame().GetCallqueue().Remove(SampleRegressionEffects);
			GetGame().GetCallqueue().Remove(SampleFirearm);
		}
		if (m_TestShooter)
			SCR_EntityHelper.DeleteEntityAndChildren(m_TestShooter);
		if (m_TestSmoke)
			SCR_EntityHelper.DeleteEntityAndChildren(m_TestSmoke);
		if (m_TestWarhead)
			SCR_EntityHelper.DeleteEntityAndChildren(m_TestWarhead);
		if (m_TestDrift)
			SCR_EntityHelper.DeleteEntityAndChildren(m_TestDrift);
		foreach (ParticleEffectEntity layer : m_RoomLayers)
		{
			if (layer)
				SCR_EntityHelper.DeleteEntityAndChildren(layer);
		}
		foreach (ParticleEffectEntity effect : m_TestEffects)
		{
			if (effect)
				SCR_EntityHelper.DeleteEntityAndChildren(effect);
		}
	}

	protected void SpawnTestWarhead()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		vector pos = owner.GetOrigin() + Vector(0, 0.25, 0);
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = pos;

		Resource res = Resource.Load("{9C7B7B7ECDC3A596}Prefabs/Weapons/Warheads/Warhead_Grenade_M67.et");
		if (!res || !res.IsValid())
			return;
		if (m_bReviewRoom && !m_bRunRegressionTests)
			CheckReviewRoom();
		m_TestWarhead = GetGame().SpawnEntityPrefab(res, owner.GetWorld(), params);
		PrintFormat("BER DIAG frag: TEST spawned warhead=%1 at %2", m_TestWarhead != null, pos);
		if (m_TestWarhead)
			GetGame().GetCallqueue().CallLater(SampleFragmentBurst, 50, true);
	}

	// Tests call production helpers; no copied implementations or production auto-run.
	protected void Check(bool passed, string label)
	{
		if (!passed)
			m_iFailures++;
		PrintFormat("BER TEST %1: %2", passed, label);
	}

	protected void RunRegressionTests()
	{
		m_iFailures = 0;
		m_iSamples = 0;
		m_iHazeCreated = 0;
		m_iHazePeak = 0;
		m_iHazeSpritesPeak = 0;
		array<float> angles = {0, 30, 60, 85, 89.9};
		foreach (float angle : angles)
		{
			float radians = angle * Math.DEG2RAD;
			vector incoming = Vector(Math.Sin(radians), 0, Math.Cos(radians));
			vector normal = Vector(0, 0, -1);
			vector ejecta = BER_SurfaceUtil.GetImpactEjectaDirection(incoming, normal);
			Check(Math.AbsFloat(ejecta.Length() - 1) < 0.001 && vector.Dot(ejecta, normal) > 0, string.Format("outward ejecta at %1 degrees", angle));
			Check(vector.Distance(ejecta, BER_SurfaceUtil.GetImpactEjectaDirection(incoming, -normal)) < 0.001, "two-sided normal invariant");
			if (angle > 0)
				Check(ejecta[0] > 0, "grazing impact preserves tangent");
		}
		Check(vector.Distance(BER_SurfaceUtil.GetImpactEjectaDirection("0 -1 0", vector.Up), vector.Up) < 0.001, "floor ejecta");
		Check(vector.Distance(BER_SurfaceUtil.GetImpactEjectaDirection("0 1 0", -vector.Up), -vector.Up) < 0.001, "ceiling ejecta");
		float whole = BER_SurfaceUtil.Decay(1, 1, 0.6666667);
		float partitioned = 1;
		for (int step = 0; step < 100; step++)
			partitioned = BER_SurfaceUtil.Decay(partitioned, 0.01, 0.6666667);
		Check(Math.AbsFloat(whole - partitioned) < 0.0001, "decay independent of step partition");
		vector move = BER_SurfaceUtil.ClipDisplacementToPlane("0 0 0", "0.5 0 0.2", "2 0 0", "-1 0 0");
		Check(vector.Distance(move, "0.5 0 0.2") < 0.001, "look-ahead wall does not stop approach");
		move = BER_SurfaceUtil.ClipDisplacementToPlane("1.9 0 0", "0.5 0 0.2", "2 0 0", "-1 0 0");
		Check(Math.AbsFloat(move[0] - 0.07) < 0.001 && Math.AbsFloat(move[2] - 0.2) < 0.001, "contact preserves slide and skin");
		move = BER_SurfaceUtil.ClipDisplacementToPlane("1.9 0 0", "-0.5 0 0", "2 0 0", "-1 0 0");
		Check(vector.Distance(move, "-0.5 0 0") < 0.001, "cloud can leave wall");
		BER_ShotSample shot = new BER_ShotSample();
		shot.m_Position = vector.Zero;
		shot.m_Direction = "0 0 1";
		shot.m_fSpeed = 800;
		Check(BER_MuzzleBlastDust.ScoreShot("0 0 400", shot, 0.5) >= 0, "plausible flight accepted");
		Check(BER_MuzzleBlastDust.ScoreShot("0 0 400", shot, 0.1) < 0, "future flight rejected");
		Check(BER_MuzzleBlastDust.ScoreShot("3 0 10", shot, 0.1) < 0, "adjacent ray rejected");
		Check(BER_MuzzleBlastDust.ScoreShot("0 0 -1", shot, 0.1) < 0, "behind muzzle rejected");
		m_TestRoom = new BER_RoomDustState();
		m_TestRoom.Accumulate(0, 100);
		Check(m_TestRoom.m_fPending == 4, "room source backlog capped");
		m_TestRoom.Accumulate(20, 0);
		Check(m_TestRoom.m_fPending > 1.47 && m_TestRoom.m_fPending < 1.48, "unemitted room source decays");
		m_TestRoom.m_Layers.Insert(null);
		m_TestRoom.PruneLayers();
		Check(m_TestRoom.m_Layers.IsEmpty(), "expired room layer frees budget");

		if (m_bReviewRoom)
		{
			CheckReviewRoom();
			StartDriftTest();
		}
		vector origin = GetOwner().GetOrigin();
		SpawnRegressionEffect("{BE20260905000001}Particles/BER/BER_ActionGas.ptc", origin + Vector(-1, 0, 0), 0.0882);
		SpawnRegressionEffect("{BE20260905000001}Particles/BER/BER_ActionGas.ptc", origin, 0.49);
		SpawnRegressionEffect("{BE20260905000001}Particles/BER/BER_ActionGas.ptc", origin + Vector(1, 0, 0), 0.98);
		SpawnRegressionEffect("{BE20250902AC0027}Particles/BER/BER_RoomFog.ptc", origin + Vector(0, 0, 1), 1);
		SpawnRegressionEffect("Particles/Weapon/Smoke_grenade_M18_Green.ptc", origin + Vector(2, 0, 1), 1);
		SpawnRegressionEffect("{BE20260905000001}Particles/BER/BER_ActionGas.ptc", origin + Vector(-2, 0, 0), 0);
		CheckDirectionalAssets();
		m_TestRoom.m_Layers.Insert(m_TestEffects[3]);
		if (m_bFirearmTest && m_bReviewRoom)
			StartFirearmTest();
		if (m_bSmokeDeviceTest && m_bReviewRoom)
			StartSmokeDeviceTest();
		m_fTestStart = GetOwner().GetWorld().GetWorldTime() * 0.001;
		GetGame().GetCallqueue().CallLater(SampleRegressionEffects, 50, true);
	}

	protected void SpawnRegressionEffect(ResourceName resource, vector position, float strength)
	{
		ParticleEffectEntitySpawnParams params = new ParticleEffectEntitySpawnParams();
		params.UseFrameEvent = true;
		params.DeleteWhenStopped = false;
		params.Transform[3] = position;
		ParticleEffectEntity effect = BER_OwnedEffects.SpawnPaused(resource, params);
		m_TestEffects.Insert(effect);
		m_PeakCounts.Insert(0);
		Check(effect != null, string.Format("spawn %1", resource));
		if (!effect)
			return;
		Particles particles = effect.GetParticles();
		Check(particles != null, "native emitter handle");
		if (particles)
		{
			Check(particles.GetNumParticles() == 0 && effect.GetTotalSimulationTime() == 0, "initialized before first particle");
			particles.MultParam(-1, EmitterParam.BIRTH_RATE, strength);
			if (resource.Contains("Smoke_grenade_M18_Green"))
			{
				array<string> names = {};
				particles.GetEmitterNames(names);
				foreach (int index, string name : names)
				{
					if (name.StartsWith("smoke_01_long"))
					{
						float velocity, drag, lift;
						particles.GetParamOrig(index, EmitterParam.VELOCITY, velocity);
						particles.GetParamOrig(index, EmitterParam.AIR_RESISTANCE, drag);
						particles.GetParamOrig(index, EmitterParam.GRAVITY_SCALE, lift);
						PrintFormat("BER SMOKE outer=%1 velocity=%2 drag=%3 gravity=%4", name, velocity, drag, lift);
						Check(Math.AbsFloat(velocity - 0.95) < 0.001 && Math.AbsFloat(drag - 0.3) < 0.001 && Math.AbsFloat(lift + 0.003) < 0.001, "authored outer plume loaded");
					}
				}
			}
		}
		effect.Play();
	}

	protected void SampleRegressionEffects()
	{
		m_iSamples++;
		float elapsed = GetOwner().GetWorld().GetWorldTime() * 0.001 - m_fTestStart;
		if (m_bReviewRoom && !m_bDriftChecked && elapsed > 3)
		{
			m_bDriftChecked = true;
			Check(m_TestDrift != null, "native drifting cloud retained");
			if (m_TestDrift)
			{
				vector position = m_TestDrift.GetOrigin();
				Check(position[2] > m_vDriftStart[2] + 1.2 && position[2] < m_vTestHit[2] + 0.05 && position[0] > m_vDriftStart[0] + 0.2, "native cloud reaches wall and retains sideways travel");
				PrintFormat("BER DRIFT start=%1 end=%2 wall=%3", m_vDriftStart, position, m_vTestHit);
			}
		}
		if (m_bSmokeDeviceTest && m_iSamples % 10 == 0)
			SampleSmokeDevice(elapsed);
		if (m_bReviewRoom && m_iSamples % 4 == 0)
		{
			if (elapsed < 75)
			{
				vector source = m_vTestHit + m_vTestNormal * 0.08;
				ParticleEffectEntity layer = BER_EffectTuningComponent.AddRoomDust(GetOwner().GetWorld(), source, GetOwner(), 0.7, 0.2, "{BE20250902AC0027}Particles/BER/BER_RoomFog.ptc");
				if (layer)
				{
					m_RoomLayers.Insert(layer);
					m_iHazeCreated++;
				}
			}
			int sprites = 0;
			for (int layerIndex = m_RoomLayers.Count() - 1; layerIndex >= 0; layerIndex--)
			{
				ParticleEffectEntity live = m_RoomLayers[layerIndex];
				if (!live || live.GetState() == EParticleEffectState.STOPPED)
					m_RoomLayers.Remove(layerIndex);
				else if (live.GetParticles())
					sprites += live.GetParticles().GetNumParticles();
			}
			m_iHazePeak = Math.Max(m_iHazePeak, m_RoomLayers.Count());
			m_iHazeSpritesPeak = Math.Max(m_iHazeSpritesPeak, sprites);
		}
		foreach (int index, ParticleEffectEntity effect : m_TestEffects)
		{
			if (!effect || !effect.GetParticles())
				continue;
			int count = effect.GetParticles().GetNumParticles();
			m_PeakCounts[index] = Math.Max(m_PeakCounts[index], count);
			if (m_iSamples == 1 || m_iSamples % 100 == 0)
				PrintFormat("BER SAMPLE t=%1 source=%2 state=%3 count=%4 simulation=%5", elapsed, index, effect.GetState(), count, effect.GetTotalSimulationTime());
			if (index == 4 && m_iSamples == 100)
			{
				float before = effect.GetTotalSimulationTime();
				effect.GetParticles().SetParam(-1, EmitterParam.WIND, false);
				Check(effect.GetTotalSimulationTime() >= before && effect.GetState() == EParticleEffectState.PLAYING, "wind shelter does not restart smoke");
			}
		}
		if (elapsed < m_fRegressionDuration)
			return;
		GetGame().GetCallqueue().Remove(SampleRegressionEffects);
		for (int i = 0; i < m_PeakCounts.Count(); i++)
			PrintFormat("BER PEAK source=%1 count=%2", i, m_PeakCounts[i]);
		Check(m_PeakCounts[5] == 0, "zero birth suppresses all particles");
		Check(m_PeakCounts[0] > 0, "fractional magwell burst emits");
		Check(m_PeakCounts[1] > 0 && m_PeakCounts[2] > 0, "receiver gas emits");
		Check(m_PeakCounts[3] > 0 && m_PeakCounts[3] <= 12, "room fog active and bounded");
		m_TestRoom.PruneLayers();
		Check(m_TestRoom.m_Layers.IsEmpty(), "native room layer completes and releases budget");
		if (m_fRegressionDuration >= 120)
			Check(m_TestEffects[4] && m_TestEffects[4].GetState() == EParticleEffectState.STOPPED, "smoke completes original burn and tail");
		foreach (ParticleEffectEntity cleanup : m_TestEffects)
		{
			if (cleanup)
				cleanup.Stop();
		}
		if (m_bReviewRoom && m_fRegressionDuration >= 120)
		{
			PrintFormat("BER ROOM created=%1 peakLayers=%2 peakSprites=%3 remaining=%4", m_iHazeCreated, m_iHazePeak, m_iHazeSpritesPeak, m_RoomLayers.Count());
			Check(m_iHazeCreated > 4, "sustained fire replenishes expired room layers");
			Check(m_iHazePeak <= 4 && m_iHazeSpritesPeak <= 48, "production room layer and sprite caps");
			Check(m_RoomLayers.IsEmpty(), "production room haze clears after firing stops");
		}
		if (m_bSmokeDeviceTest)
			Check(m_iSmokeStage == 5, "native source completed indoor/outdoor/indoor checks");
		PrintFormat("BER RESULT failures=%1 samples=%2 duration=%3", m_iFailures, m_iSamples, elapsed);
	}

	protected void CheckReviewRoom()
	{
		BaseWorld world = GetOwner().GetWorld();
		vector origin = GetOwner().GetOrigin();
		int camera = world.GetCurrentCameraId();
		world.SetCamera(camera, origin + Vector(0, 0.7, -0.8), vector.Zero);
		world.SetCameraVerticalFOV(camera, 65);
		Check(BER_SurfaceUtil.IsRoofed(world, origin, GetOwner(), 12), "native room roof detected");
		vector ground, groundNormal;
		string material;
		Check(BER_SurfaceUtil.TraceGround(world, origin, 3, GetOwner(), ground, material, groundNormal), "native room floor detected");
		PrintFormat("BER GEOMETRY floor=%1 normal=%2 material=%3", ground, groundNormal, material);
		Check(ground[1] < origin[1] - 0.1, "fixture sources above floor");
		TraceParam trace = new TraceParam();
		trace.Start = origin;
		trace.End = origin + Vector(0, 0, 8);
		trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		trace.Exclude = GetOwner();
		float fraction = world.TraceMove(trace, null);
		Check(fraction < 1, "native wall collision present");
		if (fraction < 1)
		{
			vector point = trace.Start + (trace.End - trace.Start) * fraction;
			m_vTestHit = point;
			m_vTestNormal = trace.TraceNorm;
			PrintFormat("BER GEOMETRY wall=%1 normal=%2", point, trace.TraceNorm);
			vector hit, normal;
			string struck;
			IEntity root;
			array<float> angles = {0, 30, 60, 85, 89.9};
			foreach (float angle : angles)
			{
				float rad = angle * Math.DEG2RAD;
				vector incoming = Vector(Math.Sin(rad), 0, Math.Cos(rad));
				bool resolved = BER_SurfaceUtil.TraceImpact(world, point, incoming, GetOwner(), hit, normal, struck, root);
				Check(resolved, string.Format("native wall hit resolved at %1 degrees", angle));
				if (resolved)
					Check(vector.Dot(BER_SurfaceUtil.GetImpactEjectaDirection(incoming, normal), normal) > 0, "native wall ejecta leaves surface");
			}
			vector clipped = BER_SurfaceUtil.ClipCloudPosition(world, origin, point + Vector(0, 0, 1), GetOwner());
			Check(clipped[2] < point[2] && clipped[2] > origin[2], "haze placement stops before wall");
			Check(!BER_SurfaceUtil.HasClearPath(world, origin, point + Vector(0, 0, 1), GetOwner()), "adjacent rooms separated by collision");
		}
	}
	// Native weapon integration: input goes through the character controller and
	// vanilla muzzle hook. No direct call to BER_ActionGas is made by this test.
	protected void StartFirearmTest()
	{
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		vector ground, normal;
		string material;
		vector point = GetOwner().GetOrigin() + Vector(-1, 0, -0.4);
		BER_SurfaceUtil.TraceGround(GetOwner().GetWorld(), point, 3, GetOwner(), ground, material, normal);
		params.Transform[3] = ground + Vector(0, 0.05, 0);
		Resource resource = Resource.Load("Prefabs/Characters/Factions/BLUFOR/US_Army/Character_US_Rifleman.et");
		if (resource && resource.IsValid())
			m_TestShooter = GetGame().SpawnEntityPrefab(resource, GetOwner().GetWorld(), params);
		Check(m_TestShooter != null, "native rifleman spawned");
		if (m_TestShooter)
			GetGame().GetCallqueue().CallLater(SampleFirearm, 100, true);
	}

	protected void SampleFirearm()
	{
		m_iFireTicks++;
		if (!m_TestShooter)
		{
			Check(false, "test shooter still available");
			GetGame().GetCallqueue().Remove(SampleFirearm);
			return;
		}
		CharacterControllerComponent controller = CharacterControllerComponent.Cast(m_TestShooter.FindComponent(CharacterControllerComponent));
		BaseWeaponManagerComponent manager = BaseWeaponManagerComponent.Cast(m_TestShooter.FindComponent(BaseWeaponManagerComponent));
		if (!controller || !manager)
		{
			Check(false, "native firearm controller present");
			GetGame().GetCallqueue().Remove(SampleFirearm);
			return;
		}
		if (m_iFireTicks == 10 || m_iFireTicks == 210)
		{
			array<WeaponSlotComponent> slots = {};
			manager.GetWeaponsSlots(slots);
			foreach (WeaponSlotComponent slot : slots)
			{
				if (slot.GetWeaponSlotIndex() != 0)
					continue;
				if (m_iFireTicks == 210)
				{
					m_bSuppressedStage = true;
					controller.SetHeadingAngle(30 * Math.DEG2RAD, true);
					Resource suppressed = Resource.Load("Prefabs/Weapons/Rifles/M16/Variants/Rifle_M16A2_suppressor_4x20_OliveGreen_Sand_Stripes.et");
					EntitySpawnParams params = new EntitySpawnParams();
					params.TransformMode = ETransformMode.WORLD;
					params.Transform[3] = m_TestShooter.GetOrigin();
					IEntity replacement;
					if (suppressed && suppressed.IsValid())
						replacement = GetGame().SpawnEntityPrefab(suppressed, GetOwner().GetWorld(), params);
					Check(replacement != null, "native suppressed rifle spawned");
					if (replacement)
					{
						IEntity old = manager.SetSlotWeapon(slot, replacement);
						if (old)
							SCR_EntityHelper.DeleteEntityAndChildren(old);
					}
				}
				controller.SelectWeapon(slot);
				m_TestMuzzle = slot.GetCurrentMuzzle();
				if (m_TestMuzzle)
				{
					m_iAmmoStart = m_TestMuzzle.GetAmmoCount();
					Check(m_TestMuzzle.IsMuzzleSuppressed() == m_bSuppressedStage, "native suppression state");
					IEntity gun = m_TestMuzzle.GetOwner();
					Animation animation = gun.GetAnimation();
					PointInfo point = new PointInfo();
					vector identity[4], resolved[4];
					Math3D.MatrixIdentity4(identity);
					point.Set(gun, "barrel_chamber", identity);
					point.GetWorldTransform(resolved);
					vector boneMatrix[4];
					Check(animation && animation.GetBoneMatrix(point.GetNodeId(), boneMatrix), "native signed chamber node resolves");
					PrintFormat("BER PIVOT node=%1 position=%2", point.GetNodeId(), resolved[3]);
				}
				break;
			}
		}
		controller.SetWeaponRaised(true);
		controller.SetSafety(false, false);
		bool firing = (m_iFireTicks >= 50 && m_iFireTicks < 150) || (m_iFireTicks >= 250 && m_iFireTicks < 350);
		controller.SetFireWeaponWanted(firing && m_iFireTicks % 5 == 0);
		if (firing)
			GetOwner().GetWorld().QueryEntitiesBySphere(m_TestShooter.GetOrigin(), 3, ObserveActionGas, null, EQueryEntitiesFlags.ALL);
		if (m_iFireTicks == 149 || m_iFireTicks == 349)
			CheckFiredShotAssociation(manager);
		if (m_iFireTicks == 160 && m_TestMuzzle)
			m_iPlainShots = m_iAmmoStart - m_TestMuzzle.GetAmmoCount();
		if (m_iFireTicks % 50 == 0 && m_TestMuzzle)
			PrintFormat("BER FIRE tick=%1 canFire=%2 raised=%3 ammo=%4 gas=%5/%6", m_iFireTicks, controller.CanFire(), controller.IsWeaponRaised(), m_TestMuzzle.GetAmmoCount(), m_iPlainGas, m_iSuppressedGas);
		if (m_iFireTicks < 360)
			return;
		controller.SetFireWeaponWanted(false);
		if (m_TestMuzzle)
			m_iSuppressedShots = m_iAmmoStart - m_TestMuzzle.GetAmmoCount();
		Check(m_iPlainShots > 0 && m_iSuppressedShots > 0, "native rifle and suppressed shots consumed ammunition");
		Check(m_iPlainGas == 2 * m_iPlainShots && m_iSuppressedGas == 2 * m_iSuppressedShots, "native muzzle hook emits chamber and magwell once per shot");
		Check(m_fSuppressedGasRate > 1.8 * m_fPlainGasRate && m_fPlainGasRate > 0, "native suppressed source has stronger discharge");
		PrintFormat("BER FIRE RESULT shots=%1/%2 gasEffects=%3/%4 rateSum=%5/%6", m_iPlainShots, m_iSuppressedShots, m_iPlainGas, m_iSuppressedGas, m_fPlainGasRate, m_fSuppressedGasRate);
		GetGame().GetCallqueue().Remove(SampleFirearm);
		SCR_EntityHelper.DeleteEntityAndChildren(m_TestShooter);
	}

	protected bool ObserveActionGas(IEntity entity)
	{
		ParticleEffectEntity effect = ParticleEffectEntity.Cast(entity);
		if (!effect || !effect.GetParticles() || effect.GetState() == EParticleEffectState.STOPPED || m_TestEffects.Find(effect) != -1 || m_ObservedGas.Find(effect) != -1)
			return true;
		array<string> names = {};
		effect.GetParticles().GetEmitterNames(names);
		if (names.Find("noscope_action_gas") == -1)
			return true;
		m_ObservedGas.Insert(effect);
		float rate;
		effect.GetParticles().GetParam(0, EmitterParam.BIRTH_RATE, rate);
		if (m_bSuppressedStage)
		{
			m_iSuppressedGas++;
			m_fSuppressedGasRate += rate;
		}
		else
		{
			m_iPlainGas++;
			m_fPlainGasRate += rate;
		}
		PrintFormat("BER FIRE gas source=%1 particles=%2 suppressed=%3", effect.GetOrigin(), effect.GetParticles().GetNumParticles(), m_bSuppressedStage);
		return true;
	}

	protected void StartSmokeDeviceTest()
	{
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = GetOwner().GetOrigin() + Vector(0, 0, -1);
		Resource resource = Resource.Load("Prefabs/Weapons/Grenades/M18/Smoke_M18_Green.et");
		if (resource && resource.IsValid())
			m_TestSmoke = GetGame().SpawnEntityPrefab(resource, GetOwner().GetWorld(), params);
		Check(m_TestSmoke != null, "native smoke device spawned");
		if (!m_TestSmoke)
			return;
		TimerTriggerComponent trigger = TimerTriggerComponent.Cast(m_TestSmoke.FindComponent(TimerTriggerComponent));
		Check(trigger != null, "native smoke trigger present");
		if (trigger)
		{
			trigger.SetLive();
			trigger.OnUserTrigger(m_TestSmoke);
		}
	}

	protected void SampleSmokeDevice(float elapsed)
	{
		if (!m_TestSmoke)
			return;
		if (!m_NativeSmoke)
		{
			IEntity child = m_TestSmoke.GetChildren();
			while (child && !m_NativeSmoke)
			{
				m_NativeSmoke = ParticleEffectEntity.Cast(child);
				child = child.GetSibling();
			}
		}
		if (!m_NativeSmoke || !m_NativeSmoke.GetParticles())
			return;
		if (m_iSmokeStage == 0 && elapsed > 3)
		{
			CheckSmokeSource(false);
			m_iSmokeStage = 1;
		}
		else if (m_iSmokeStage == 1 && elapsed > 10)
		{
			m_TestSmoke.SetOrigin(GetOwner().GetOrigin() + Vector(0, 0, -8));
			m_TestSmoke.Update();
			m_iSmokeStage = 2;
		}
		else if (m_iSmokeStage == 2 && elapsed > 12)
		{
			CheckSmokeSource(true);
			m_iSmokeStage = 3;
		}
		else if (m_iSmokeStage == 3 && elapsed > 20)
		{
			m_TestSmoke.SetOrigin(GetOwner().GetOrigin() + Vector(0, 0, -1));
			m_TestSmoke.Update();
			m_iSmokeStage = 4;
		}
		else if (m_iSmokeStage == 4 && elapsed > 22)
		{
			CheckSmokeSource(false);
			m_iSmokeStage = 5;
		}
	}

	protected void CheckSmokeSource(bool expectedWind)
	{
		Particles particles = m_NativeSmoke.GetParticles();
		array<string> names = {};
		particles.GetEmitterNames(names);
		int checked = 0;
		foreach (int index, string name : names)
		{
			if (!name.StartsWith("smoke_"))
				continue;
			bool wind;
			particles.GetParam(index, EmitterParam.WIND, wind);
			Check(wind == expectedWind, string.Format("native device wind=%1 emitter=%2", expectedWind, name));
			checked++;
		}
		Check(checked > 0, "native smoke emitters found");
		Check(m_NativeSmoke.GetTotalSimulationTime() > m_fSmokeClock, "native moving source retains burn clock");
		Check(vector.Distance(m_NativeSmoke.GetOrigin(), m_TestSmoke.GetOrigin()) < 1, "native smoke source follows moved device");
		m_fSmokeClock = m_NativeSmoke.GetTotalSimulationTime();
		PrintFormat("BER DEVICE stage=%1 time=%2 position=%3", m_iSmokeStage, m_fSmokeClock, m_NativeSmoke.GetOrigin());
	}

	protected void CheckFiredShotAssociation(BaseWeaponManagerComponent manager)
	{
		vector muzzle[4];
		if (!manager.GetCurrentMuzzleTransform(muzzle))
		{
			Check(false, "native firing transform available");
			return;
		}
		TraceParam trace = new TraceParam();
		trace.Start = muzzle[3];
		trace.End = muzzle[3] + muzzle[2] * 8;
		trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		trace.Exclude = m_TestShooter;
		float fraction = GetOwner().GetWorld().TraceMove(trace, null);
		Check(fraction < 1, "native fired ray meets review wall");
		if (fraction >= 1)
			return;
		vector point = trace.Start + (trace.End - trace.Start) * fraction;
		vector incoming;
		float scale;
		bool matched = BER_MuzzleBlastDust.GetIncomingShotInfo(GetOwner().GetWorld(), point, incoming, scale);
		Check(matched && vector.Dot(incoming, muzzle[2]) > 0.95 && Math.AbsFloat(scale - 0.36) < 0.001, "native shot history recovers rifle direction and scale");
		if (matched)
		{
			vector ejecta = BER_SurfaceUtil.GetImpactEjectaDirection(incoming, trace.TraceNorm);
			Check(vector.Dot(ejecta, trace.TraceNorm) > 0, "native fired wall ejecta faces incident room");
			PrintFormat("BER SHOT incoming=%1 normal=%2 ejecta=%3", incoming, trace.TraceNorm, ejecta);
		}
	}

	protected void CheckDirectionalAssets()
	{
		array<ResourceName> resources = {
			"{BE20250902AC0007}Particles/BER/BER_APDS_Rock.ptc",
			"{BE20250902AC0006}Particles/BER/BER_APDS_Dirt.ptc",
			"{BE20250902AC0005}Particles/BER/BER_APDS_Spark.ptc"
		};
		foreach (ResourceName resource : resources)
		{
			ParticleEffectEntitySpawnParams params = new ParticleEffectEntitySpawnParams();
			params.UseFrameEvent = true;
			params.Transform[3] = GetOwner().GetOrigin();
			ParticleEffectEntity effect = BER_OwnedEffects.SpawnPaused(resource, params);
			Check(effect != null, "native directional asset initialized");
			if (!effect)
				continue;
			Particles particles = effect.GetParticles();
			vector normal = "0 0 -1";
			vector incoming = Vector(Math.Sin(85 * Math.DEG2RAD), 0, Math.Cos(85 * Math.DEG2RAD));
			BER_SurfaceUtil.TuneImpactCone(particles, BER_SurfaceUtil.GetImpactEjectaDirection(incoming, normal), normal);
			array<string> names = {};
			particles.GetEmitterNames(names);
			foreach (int index, string name : names)
			{
				vector cone;
				particles.GetParam(index, EmitterParam.CONEANGLE, cone);
				if (name == "Light" || name == "Flash" || name.StartsWith("ber_smoke_"))
				{
					vector original;
					particles.GetParamOrig(index, EmitterParam.CONEANGLE, original);
					Check(vector.Distance(cone, original) < 0.001, "directional tuning preserves smoke/light cone");
				}
				else
					Check(cone[1] == 0 && cone[2] > 0 && cone[2] < 15, string.Format("grazing solid/dust cone bounded: %1", name));
			}
			SCR_EntityHelper.DeleteEntityAndChildren(effect);
		}
	}

	protected void SampleFragmentBurst()
	{
		m_iFragmentSamples++;
		m_iFragmentSprites = 0;
		m_iDebrisSprites = 0;
		GetOwner().GetWorld().QueryEntitiesBySphere(GetOwner().GetOrigin(), 16, ObserveFragmentEffect, null, EQueryEntitiesFlags.ALL);
		m_iFragmentPeak = Math.Max(m_iFragmentPeak, m_iFragmentSprites);
		m_iDebrisPeak = Math.Max(m_iDebrisPeak, m_iDebrisSprites);
		if (m_iFragmentSamples < 400)
			return;
		GetGame().GetCallqueue().Remove(SampleFragmentBurst);
		Check(m_ObservedFragments.Count() > 0 && m_iFragmentPeak > 0, "native grenade emitted material fragment wisps");
		Check(m_iDebrisPeak > 0, "native grenade emitted the separate solid-debris effect");
		Check(m_ObservedFragments.Count() <= 64 && m_iFragmentPeak <= 192, "native fragment wisp budget");
		PrintFormat("BER FRAGMENT RESULT effects=%1 wispPeak=%2 debrisEffectPeak=%3 failures=%4", m_ObservedFragments.Count(), m_iFragmentPeak, m_iDebrisPeak, m_iFailures);
	}

	protected bool ObserveFragmentEffect(IEntity entity)
	{
		ParticleEffectEntity effect = ParticleEffectEntity.Cast(entity);
		if (!effect || !effect.GetParticles())
			return true;
		Particles particles = effect.GetParticles();
		array<string> names = {};
		particles.GetEmitterNames(names);
		if (names.Count() == 1 && names[0] == "ber_dust_whisp")
		{
			if (m_ObservedFragments.Find(effect) == -1)
				m_ObservedFragments.Insert(effect);
			m_iFragmentSprites += particles.GetNumParticles();
		}
		if (names.Find("stone_chips") != -1 || names.Find("dirt_clumps") != -1)
			m_iDebrisSprites += particles.GetNumParticles();
		return true;
	}

	protected void StartDriftTest()
	{
		m_vDriftStart = GetOwner().GetOrigin();
		ParticleEffectEntitySpawnParams params = new ParticleEffectEntitySpawnParams();
		params.UseFrameEvent = true;
		params.DeleteWhenStopped = false;
		params.Transform[3] = m_vDriftStart;
		m_TestDrift = BER_OwnedEffects.SpawnPaused("{BE20250902AC0026}Particles/BER/BER_RifleDustCloud.ptc", params);
		if (!m_TestDrift)
			return;
		BER_OwnedEffects.MarkOwned(m_TestDrift);
		m_TestDrift.GetParticles().MultParam(-1, EmitterParam.BIRTH_RATE, 0.3);
		m_TestDrift.Play();
		BER_WindDriftAnimator.GetInstance().AddImpulse(m_TestDrift, "1 0 4", 0.3, 1);
	}

}
