//------------------------------------------------------------------------------------------------
// Better VFX Realism — surface-aware AP impact effect resolver
//
// AP rounds (25mm APDS-T M791 and anything else referencing Explosion_APDS.ptc) hardcode
// one spark effect for every impact, so they shower sparks even on soil. The BER same-GUID
// override of that .ptc spawns THIS prefab instead (single invisible prefab-particle at the
// impact point). The component probes for the nearest surface around itself and spawns the
// appropriate reaction, oriented to the surface normal:
//  - metal / armor / vehicle hulls  -> brief flash, gravity-driven sparks and a short aerosol tail
//  - rock / concrete / hard mineral -> stone chips + grey dust, NO sparks
//  - soil / sand / vegetation / etc -> dirt thrown out by the round boring in, NO sparks
//  - water                          -> nothing (vanilla splash already handles it)
//------------------------------------------------------------------------------------------------

[EntityEditorProps(category: "GameScripted/BetterEffectsRealism", description: "Resolves AP round impact effect by surface material")]
class BER_APDSImpactResolverComponentClass : ScriptComponentClass
{
}

class BER_APDSImpactResolverComponent : ScriptComponent
{
	[Attribute(defvalue: "", UIWidgets.ResourcePickerThumbnail, desc: "Spark effect for metal/armor impacts", params: "ptc")]
	protected ResourceName m_rSparkEffect;

	[Attribute(defvalue: "", UIWidgets.ResourcePickerThumbnail, desc: "Dirt displacement effect for soft-surface impacts", params: "ptc")]
	protected ResourceName m_rDirtEffect;

	[Attribute(defvalue: "", UIWidgets.ResourcePickerThumbnail, desc: "Stone chip effect for rocky/hard-mineral impacts", params: "ptc")]
	protected ResourceName m_rRockEffect;

	protected bool m_bResolved;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (m_bResolved)
			return;
		m_bResolved = true;
		ClearEventMask(owner, EntityEvent.FRAME);

		Resolve(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void Resolve(IEntity owner)
	{
		BaseWorld world = owner.GetWorld();
		if (!world)
			return;

		vector mat[4];
		owner.GetWorldTransform(mat);
		vector pos = mat[3];

		// the impact point sits on (or within centimeters of) the struck surface — probe all
		// six axis directions and take the nearest hit so wall impacts resolve to the wall,
		// not the ground below it
		vector bestNorm;
		string bestMat;
		IEntity bestRoot;
		float bestDist = 1000;
		vector hitNorm;
		string hitMat;
		IEntity hitRoot;
		float dist;

		if (Probe(world, owner, pos, Vector(0, -1.3, 0), hitNorm, hitMat, hitRoot, dist) && dist < bestDist)
		{
			bestDist = dist;
			bestNorm = hitNorm;
			bestMat = hitMat;
			bestRoot = hitRoot;
		}
		if (Probe(world, owner, pos, Vector(0, 1.0, 0), hitNorm, hitMat, hitRoot, dist) && dist < bestDist)
		{
			bestDist = dist;
			bestNorm = hitNorm;
			bestMat = hitMat;
			bestRoot = hitRoot;
		}
		if (Probe(world, owner, pos, Vector(1.0, 0, 0), hitNorm, hitMat, hitRoot, dist) && dist < bestDist)
		{
			bestDist = dist;
			bestNorm = hitNorm;
			bestMat = hitMat;
			bestRoot = hitRoot;
		}
		if (Probe(world, owner, pos, Vector(-1.0, 0, 0), hitNorm, hitMat, hitRoot, dist) && dist < bestDist)
		{
			bestDist = dist;
			bestNorm = hitNorm;
			bestMat = hitMat;
			bestRoot = hitRoot;
		}
		if (Probe(world, owner, pos, Vector(0, 0, 1.0), hitNorm, hitMat, hitRoot, dist) && dist < bestDist)
		{
			bestDist = dist;
			bestNorm = hitNorm;
			bestMat = hitMat;
			bestRoot = hitRoot;
		}
		if (Probe(world, owner, pos, Vector(0, 0, -1.0), hitNorm, hitMat, hitRoot, dist) && dist < bestDist)
		{
			bestDist = dist;
			bestNorm = hitNorm;
			bestMat = hitMat;
			bestRoot = hitRoot;
		}

		if (bestDist > 999)
		{
			// nothing close — airburst/despawn edge case; metal spark is the least wrong
			// visual for an AP round breaking up mid-air, but with no surface just skip
			return;
		}

		// Prefer a matched incoming ray and its actual surface over axis probes at corners.
		vector incoming, impactPos, impactNormal;
		string impactMaterial;
		IEntity impactRoot;
		float shotScale;
		bool directional = BER_MuzzleBlastDust.GetIncomingShotInfo(world, pos, incoming, shotScale)
			&& BER_SurfaceUtil.TraceImpact(world, pos, incoming, owner, impactPos, impactNormal, impactMaterial, impactRoot);
		if (directional)
		{
			pos = impactPos;
			bestNorm = impactNormal;
			bestMat = impactMaterial;
			bestRoot = impactRoot;
		}

		// a 25mm AP slug slamming into a vehicle also shakes the dust off its hull
		if (bestRoot)
		{
			Vehicle struckVehicle = Vehicle.Cast(bestRoot);
			if (struckVehicle)
				BER_MuzzleBlastDust.KickoffOnVehicle(struckVehicle, pos, 1.4);
		}

		ResourceName res = PickEffect(bestMat, bestRoot);
		if (res == ResourceName.Empty)
			return;

		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.UseFrameEvent = true;
		spawnParams.PlayOnSpawn = false;

		vector up = bestNorm;
		if (directional)
			up = BER_SurfaceUtil.GetImpactEjectaDirection(incoming, bestNorm);
		if (up != vector.Zero)
			SCR_EntityHelper.OrientUpToVector(up, spawnParams.Transform);
		spawnParams.Transform[3] = pos + bestNorm * 0.025;

		ParticleEffectEntity pfx = ParticleEffectEntity.SpawnParticleEffect(res, spawnParams);
		if (!pfx)
			return;

		BER_OwnedEffects.MarkOwned(pfx); // already surface-resolved/oriented — adoption must not retune it

		bool indoor = BER_SurfaceUtil.IsRoofed(world, pos + bestNorm * 0.15, owner, 25.0);
		Particles particles = pfx.GetParticles();
		if (particles)
		{
			float dust = BER_SurfaceUtil.GetDustAvailability(world, pos, bestMat, indoor);
			BER_SurfaceUtil.TuneDust(particles, dust, indoor);
			if (directional)
				BER_SurfaceUtil.TuneImpactCone(particles, up, bestNorm);
			// Keep hot impact aerosol/sparks on wet metal; only loose mineral dust is gated.
			if (indoor)
				particles.SetParam(-1, EmitterParam.WIND, false);
		}

		pfx.Play();
	}

	//------------------------------------------------------------------------------------------------
	protected ResourceName PickEffect(string matName, IEntity hitRoot)
	{
		if (hitRoot && Vehicle.Cast(hitRoot))
			return m_rSparkEffect;

		matName.ToLower();

		if (matName.Contains("metal") || matName.Contains("armor") || matName.Contains("armour"))
			return m_rSparkEffect;

		if (matName.Contains("water") || matName.Contains("seaweed"))
			return ResourceName.Empty;

		if (matName.Contains("stone") || matName.Contains("rock") || matName.Contains("concrete")
			|| matName.Contains("asphalt") || matName.Contains("brick") || matName.Contains("cobble")
			|| matName.Contains("tiles") || matName.Contains("gravel") || matName.Contains("pebbles"))
			return m_rRockEffect;

		return m_rDirtEffect;
	}

	//------------------------------------------------------------------------------------------------
	protected bool Probe(BaseWorld world, IEntity exclude, vector pos, vector offset, out vector norm, out string matName, out IEntity hitRoot, out float dist)
	{
		TraceParam tp = new TraceParam();
		tp.Start = pos;
		tp.End = pos + offset;
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		tp.Exclude = exclude;

		float frac = world.TraceMove(tp, null);
		if (frac >= 1.0)
			return false;

		dist = offset.Length() * frac;
		norm = tp.TraceNorm;
		matName = "";
		hitRoot = null;
		if (tp.SurfaceProps)
			matName = tp.SurfaceProps.GetName();
		if (tp.TraceEnt)
			hitRoot = tp.TraceEnt.GetRootParent();
		return true;
	}
}
