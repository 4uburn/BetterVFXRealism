//------------------------------------------------------------------------------------------------
// Better VFX Realism — smoking ejected casings
//
// Large hot cases keep smoking after ejection:
//  - 25x137 (M242) and 14.5x114 (KPVT) casing prefabs get this component via same-GUID
//    override: a tiny grey smoke trail (muzzle-smoke colored) follows the tumbling case.
//    The trail effect FollowParents the case so it tracks it without dying with it.
//  - 40mm UGL (M203): the spent case only smokes if it is extracted while still hot —
//    the reload must START within 1 second of firing. The case eject itself is animation
//    driven with no script hook, so BER_UGLCaseSmoke watches the shooter: fire time is
//    recorded by the muzzle hook, and if the character begins reloading in time, a small
//    smoke wisp is spawned at the breech at the moment the case comes out.
//------------------------------------------------------------------------------------------------

[EntityEditorProps(category: "GameScripted/BetterVFXRealism", description: "Attaches a smoke trail to an ejected casing prefab-particle")]
class BER_CasingSmokeComponentClass : ScriptComponentClass
{
}

class BER_CasingSmokeComponent : ScriptComponent
{
	[Attribute(defvalue: "", UIWidgets.ResourcePickerThumbnail, desc: "Smoke trail effect that follows the casing", params: "ptc")]
	protected ResourceName m_rTrailEffect;

	protected bool m_bSpawned;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	// first frame: the prefab-particle transform is valid by now
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (m_bSpawned)
			return;
		m_bSpawned = true;
		ClearEventMask(owner, EntityEvent.FRAME);

		if (m_rTrailEffect == ResourceName.Empty)
			return;

		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		spawnParams.FollowParent = owner; // tracks the case, keeps fading after the case despawns
		// with FollowParent the Transform is LOCAL to the followed entity — the identity
		// transform puts the trail exactly on the case (a world position here lands off-map)

		BER_OwnedEffects.MarkOwned(ParticleEffectEntity.SpawnParticleEffect(m_rTrailEffect, spawnParams));
	}
}

//------------------------------------------------------------------------------------------------
//! 40mm UGL hot-case watcher. NotifyFired() is called from the muzzle hook for every M203
//! shot; the case then smokes only when the reload begins within HOT_WINDOW seconds.
class BER_UGLCaseSmoke
{
	protected const ResourceName CASE_SMOKE = "{BE20250902AC0019}Particles/BER/BER_CaseSmoke40mm.ptc";

	protected const float HOT_WINDOW = 1.0;    // reload must start this soon after the shot
	protected const float EJECT_DELAY = 1.1;   // seconds from reload start to the case leaving the breech

	protected static ref array<IEntity> s_aWeapons;
	protected static ref array<float> s_aFireTimes;
	protected static ref array<float> s_aSpawnAt;   // <0 = not armed yet
	protected static bool s_bTicking;
	protected static BaseWorld s_World;
	protected static float s_fClock;
	protected static void EnsureState(BaseWorld world)
	{
		float now = world.GetWorldTime();
		if (!s_aWeapons || s_World != world || now < s_fClock)
		{
			GetGame().GetCallqueue().Remove(Tick);
			s_bTicking = false;
			s_aWeapons = {};
			s_aFireTimes = {};
			s_aSpawnAt = {};
			s_World = world;
		}
		s_fClock = now;
	}

	//------------------------------------------------------------------------------------------------
	static void NotifyFired(IEntity weaponEntity)
	{
		if (!weaponEntity)
			return;

		BaseWorld world = weaponEntity.GetWorld();
		if (!world)
			return;

		EnsureState(world);
		float now = world.GetWorldTime() * 0.001;

		// one pending entry per weapon
		int existing = s_aWeapons.Find(weaponEntity);
		if (existing != -1)
		{
			s_aFireTimes[existing] = now;
			s_aSpawnAt[existing] = -1;
		}
		else
		{
			if (s_aWeapons.Count() >= 128)
				return;
			s_aWeapons.Insert(weaponEntity);
			s_aFireTimes.Insert(now);
			s_aSpawnAt.Insert(-1);
		}

		if (!s_bTicking)
		{
			s_bTicking = true;
			GetGame().GetCallqueue().CallLater(Tick, 100, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void Tick()
	{
		BaseWorld activeWorld = GetGame().GetWorld();
		if (!activeWorld || activeWorld != s_World || activeWorld.GetWorldTime() < s_fClock)
		{
			GetGame().GetCallqueue().Remove(Tick);
			s_bTicking = false;
			s_aWeapons.Clear();
			s_aFireTimes.Clear();
			s_aSpawnAt.Clear();
			return;
		}
		s_fClock = activeWorld.GetWorldTime();
		for (int i = s_aWeapons.Count() - 1; i >= 0; i--)
		{
			IEntity weapon = s_aWeapons[i];
			if (!weapon)
			{
				RemoveEntry(i);
				continue;
			}

			BaseWorld world = weapon.GetWorld();
			if (!world)
			{
				RemoveEntry(i);
				continue;
			}
			float now = world.GetWorldTime() * 0.001;

			if (s_aSpawnAt[i] >= 0)
			{
				// armed — wait for the case-out moment of the reload animation
				if (now >= s_aSpawnAt[i])
				{
					vector mat[4];
					weapon.GetWorldTransform(mat);
					SpawnWisp(mat[3]);
					RemoveEntry(i);
				}
				continue;
			}

			float sinceFire = now - s_aFireTimes[i];
			if (sinceFire > HOT_WINDOW + 0.2)
			{
				// case cooled off in the breech — no smoke on a late reload
				RemoveEntry(i);
				continue;
			}

			ChimeraCharacter character = ChimeraCharacter.Cast(weapon.GetRootParent());
			if (!character)
				continue;

			CharacterControllerComponent controller = CharacterControllerComponent.Cast(character.FindComponent(CharacterControllerComponent));
			BaseWeaponManagerComponent manager = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
			BaseWeaponComponent current;
			if (manager)
				current = manager.GetCurrentWeapon();
			if (controller && controller.IsReloading() && current && current.GetOwner() == weapon)
				s_aSpawnAt[i] = now + EJECT_DELAY;
		}

		if (s_aWeapons.IsEmpty())
		{
			GetGame().GetCallqueue().Remove(Tick);
			s_bTicking = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void RemoveEntry(int i)
	{
		s_aWeapons.Remove(i);
		s_aFireTimes.Remove(i);
		s_aSpawnAt.Remove(i);
	}

	//------------------------------------------------------------------------------------------------
	protected static void SpawnWisp(vector pos)
	{
		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		spawnParams.Transform[3] = pos - Vector(0, 0.15, 0); // breech sits just under the barrel
		BER_OwnedEffects.MarkOwned(ParticleEffectEntity.SpawnParticleEffect(CASE_SMOKE, spawnParams));
	}
}
