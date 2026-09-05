//------------------------------------------------------------------------------------------------
// Better VFX Realism — surface-oriented particle contact component
//
// Drop-in replacement for SCR_ParticleContactComponent on the invisible blast trigger
// prefabs (same-GUID prefab overrides swap the component type). Behavior is identical to
// vanilla except the spawned per-surface effect is ORIENTED TO THE SURFACE NORMAL at the
// contact point — vanilla spawns with identity rotation, which makes ground dust form a
// horizontal disk sticking out of any slope. Vanilla's own m_bParticleOriented path feeds
// the normal into Math3D.AnglesToMatrix as if it were angles, so it cannot be used.
//------------------------------------------------------------------------------------------------

class BER_OrientedContactComponentClass : SCR_ParticleContactComponentClass
{
}

class BER_OrientedContactComponent : SCR_ParticleContactComponent
{
	//------------------------------------------------------------------------------------------------
	override void EOnContact(IEntity owner, IEntity other, Contact contact)
	{
		SCR_ParticleContactComponentClass prefabData = SCR_ParticleContactComponentClass.Cast(GetComponentData(owner));
		if (!prefabData)
		{
			ClearEventMask(owner, EntityEvent.CONTACT);
			return;
		}

		PlayContactSound(owner, prefabData, contact);

		if (prefabData.m_bPlayParticle)
		{
			ResourceName res = prefabData.m_Particle;
			GameMaterial material = contact.Material2;
			if (material && prefabData.m_iGameMaterialEffect != 0)
			{
				ParticleEffectInfo effectInfo = material.GetParticleEffectInfo();
				if (effectInfo)
				{
					if (prefabData.m_iGameMaterialEffect == 1)
						res = effectInfo.GetVehicleDustResource(prefabData.m_iEffectIndex);
					else if (prefabData.m_iGameMaterialEffect == 2)
						res = effectInfo.GetBlastResource(prefabData.m_iEffectIndex);
				}
			}

			if (res != string.Empty)
			{
				ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
				spawnParams.UseFrameEvent = true;

				vector up = contact.Normal;
				if (vector.Dot(up, owner.GetOrigin() - contact.Position) < 0)
					up = -up; // face the incident side, including ceilings and walls
				if (up != vector.Zero)
					SCR_EntityHelper.OrientUpToVector(up, spawnParams.Transform);

				spawnParams.Transform[3] = contact.Position;
				ParticleEffectEntity pfx = BER_OwnedEffects.SpawnPaused(res, spawnParams);
				if (pfx)
				{
					BER_OwnedEffects.MarkOwned(pfx);
					string materialName;
					if (material)
						materialName = material.GetName();
					BaseWorld world = owner.GetWorld();
					bool indoor = BER_SurfaceUtil.IsRoofed(world, contact.Position + up * 0.15, owner, 25);
					float dust = BER_SurfaceUtil.GetDustAvailability(world, contact.Position, materialName, indoor);
					Particles particles = pfx.GetParticles();
					if (particles)
						BER_SurfaceUtil.TuneDust(particles, dust * 1.35, indoor, 0.7, 1.8);
					pfx.Play();
				}
			}
		}

		if (prefabData.m_bFirstContactOnly)
			ClearEventMask(owner, EntityEvent.CONTACT);
	}

	//------------------------------------------------------------------------------------------------
	//! Vanilla's PlaySound is private — replicated so audio-configured prefabs keep their sound
	protected void PlayContactSound(IEntity owner, SCR_ParticleContactComponentClass prefabData, Contact contact)
	{
		if (!prefabData.m_AudioSourceConfiguration || !prefabData.m_AudioSourceConfiguration.IsValid())
			return;

		SCR_SoundManagerModule soundManager = SCR_SoundManagerModule.GetInstance(owner.GetWorld());
		if (!soundManager)
			return;

		SCR_AudioSource audioSource = soundManager.CreateAudioSource(owner, prefabData.m_AudioSourceConfiguration, contact.Position);
		if (!audioSource)
			return;

		if (prefabData.m_bSurfaceSignal)
		{
			GameMaterial material = contact.Material2;
			if (material && material.GetSoundInfo())
				audioSource.SetSignalValue(SCR_AudioSource.SURFACE_SIGNAL_NAME, material.GetSoundInfo().GetSignalValue());
		}

		soundManager.PlayAudioSource(audioSource);
	}
}
