//------------------------------------------------------------------------------------------------
// Better Effects Realism — cloud overlap field
//
// The engine exposes no per-particle bounds, enumeration or merging, so overlap control
// lives at the EFFECT-ENTITY level: every BER-tracked gas cloud carries two virtual
// bounding volumes derived from its authored footprint, growth and applied size scaling:
//  - INNER volume (the cloud's current radius): drives "collision" between clouds — two
//    clouds pressed into each other get gentle opposing shoves (outdoors) so standing
//    effects shoulder each other apart instead of stacking into one over-dense blob.
//  - OUTER volume (1.4x): does not collide — it is the merge-eligibility bound. Only
//    pairs whose outer volumes overlap are considered at all, and when they end up
//    DEEPLY overlapped (inside the merge fraction of their combined inner radii) the
//    two clouds MERGE: the smaller one stops emitting (its born particles fade on their
//    own curves — no pop) and the survivor is boosted to represent the union:
//      - density (BIRTH_RATE) always rises with the absorbed volume,
//      - SIZE rises only OUTDOORS (cube-root, volume-conserving). Indoors the size must
//        stay exactly as authored — an indoor cloud that grows on merge starts poking
//        through the walls of the room that contains it; indoor merges are density-only,
//        which is also what the indoor debris/impact-dust buildup wants: repeated hits
//        thicken the standing cloud instead of stacking new overlapping ones.
//
// Effects spawned by the SAME event (a detonation's central cloud plus its scattered
// contact dust) share a group id and never interact — that composition is authored.
// Explosion clouds also get a short protected age so a fresh detonation's burst always
// plays out before its lingering cloud can be absorbed by a neighbor.
//
// Deliberately NOT in the field: smoke grenades (colored signal smoke — suppressing one
// plume would change gameplay), room fog (its per-room dedup/escalation already merges),
// per-round rifle splashes (their pulse-per-shot rhythm is the effect; the existing
// concussion shove system already governs their overlap), and anything parented.
//
// LIMIT of this tick-based merge: it can only absorb effects that are still EMITTING at
// tick time. The bullet-impact haze emits for 0.2 s while the first tick sees it at
// >=400 ms — StopEmission/BIRTH boosts there are no-ops on it. Impact anti-stacking is
// therefore done at SPAWN in the tuning component (every-Nth-round haze gate, with the
// authored haze alpha raised xN to conserve the summed occlusion); only the kept hazes
// register here, which also keeps the O(n^2) pair scan small under sustained fire.
//------------------------------------------------------------------------------------------------

class BER_CloudEntry
{
	ParticleEffectEntity m_Pfx;
	int m_iFamily;
	int m_iGroup;
	bool m_bIndoor;
	float m_fBorn;         // world time (s) at registration
	float m_fLife;         // s after which the entry falls out of the field
	float m_fBaseRadius;
	float m_fMaxRadius;
	float m_fGrowTime;
	float m_fMinAbsorbAge; // s before this cloud may be absorbed (lets a burst play out)
	float m_fBirthBase;    // BIRTH_RATE multiplier the spawner already applied
	float m_fSizeBase;     // SIZE multiplier the spawner already applied
	float m_fLifeBase;     // LIFETIME multiplier the spawner already applied
	float m_fMergeBirth;   // cumulative merge density boost
	float m_fMergeSize;    // cumulative merge size boost (outdoor only)
	float m_fRadiusScale;  // cumulative bound growth from absorbing others (outdoor only)
}

class BER_CloudField
{
	static const int FAMILY_EXPLOSION = 0;
	static const int FAMILY_IMPACT = 1;
	static const int FAMILY_RIFLE_CLOUD = 2;

	protected static ref BER_CloudField s_Instance;
	protected static int s_iNextGroup = 1;

	protected ref array<ref BER_CloudEntry> m_aEntries = {};
	protected bool m_bTicking;

	protected const int TICK_MS = 400;           // overlap checks are coarse by nature
	protected const float OUTER_FACTOR = 1.6;    // merge-eligibility bound vs the inner radius
	protected const float MERGE_FRACTION = 0.72; // centers inside this fraction of combined inner radii merge
	protected const float SEP_SPEED = 0.45;      // m/s shove at full interpenetration
	protected const float MERGE_BIRTH_CAP = 2.5; // survivor density boost ceiling
	protected const float MERGE_SIZE_CAP = 1.35; // survivor size boost ceiling (outdoor)
	protected const float RADIUS_SCALE_CAP = 1.6;

	// original lifetime below which an emitter is flash/sparks — merge boosts must not
	// touch those (same gate the tuning component uses)
	protected const float FLASH_LIFETIME_THRESHOLD = 0.6;

	//------------------------------------------------------------------------------------------------
	static BER_CloudField GetInstance()
	{
		if (!s_Instance)
			s_Instance = new BER_CloudField();
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! One group per spawning event — effects sharing a group never merge with or shove
	//! each other (their overlap is the authored composition of that one event).
	static int NewGroup()
	{
		s_iNextGroup++;
		return s_iNextGroup;
	}

	//------------------------------------------------------------------------------------------------
	//! birthBase/sizeBase/lifeBase: the multipliers (vs the authored originals) the spawner
	//! already applied, so merge boosts compose with them instead of undoing them.
	void Register(ParticleEffectEntity pfx, int family, bool indoor, int group, float birthBase, float sizeBase, float lifeBase)
	{
		if (!pfx || pfx.GetParent())
			return; // parented effects stay glued to their device — never shoved, never merged

		foreach (BER_CloudEntry existing : m_aEntries)
		{
			if (existing.m_Pfx == pfx)
				return;
		}

		BER_CloudEntry entry = new BER_CloudEntry();
		entry.m_Pfx = pfx;
		entry.m_iFamily = family;
		entry.m_iGroup = group;
		entry.m_bIndoor = indoor;
		entry.m_fBorn = pfx.GetWorld().GetWorldTime() * 0.001;
		entry.m_fBirthBase = birthBase;
		entry.m_fSizeBase = sizeBase;
		entry.m_fLifeBase = lifeBase;
		entry.m_fMergeBirth = 1.0;
		entry.m_fMergeSize = 1.0;
		entry.m_fRadiusScale = 1.0;

		// virtual bound parameters per family: authored footprint -> grown cloud
		if (family == FAMILY_EXPLOSION)
		{
			entry.m_fBaseRadius = 1.6;
			entry.m_fMaxRadius = 4.2;
			entry.m_fGrowTime = 8.0;
			entry.m_fLife = 30.0;
			entry.m_fMinAbsorbAge = 0.8; // the detonation burst always plays out first
		}
		else if (family == FAMILY_RIFLE_CLOUD)
		{
			entry.m_fBaseRadius = 1.4;
			entry.m_fMaxRadius = 2.4;
			entry.m_fGrowTime = 5.0;
			entry.m_fLife = 12.0;
			entry.m_fMinAbsorbAge = 0; // a fresh volley cloud folding into the standing one IS the point
		}
		else
		{
			entry.m_fBaseRadius = 0.85;
			entry.m_fMaxRadius = 2.2;
			entry.m_fGrowTime = 3.0;
			entry.m_fLife = 40.0; // the full 30+10 s haze life — a standing haze keeps absorbing fresh hits as long as any of its particles can be alive
			entry.m_fMinAbsorbAge = 0.05;
		}

		// the applied size scaling scales the physical cloud too
		entry.m_fBaseRadius = entry.m_fBaseRadius * sizeBase;
		entry.m_fMaxRadius = entry.m_fMaxRadius * sizeBase;

		m_aEntries.Insert(entry);
		StartTicking();
	}

	//------------------------------------------------------------------------------------------------
	protected void StartTicking()
	{
		if (m_bTicking)
			return;
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
		m_bTicking = true;
	}

	//------------------------------------------------------------------------------------------------
	protected float CurrentRadius(BER_CloudEntry entry, float now)
	{
		float t = (now - entry.m_fBorn) / entry.m_fGrowTime;
		if (t > 1.0)
			t = 1.0;
		return (entry.m_fBaseRadius + (entry.m_fMaxRadius - entry.m_fBaseRadius) * t) * entry.m_fRadiusScale;
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;
		float now = world.GetWorldTime() * 0.001;

		for (int i = m_aEntries.Count() - 1; i >= 0; i--)
		{
			BER_CloudEntry entry = m_aEntries[i];
			if (!entry.m_Pfx || now - entry.m_fBorn > entry.m_fLife)
				m_aEntries.Remove(i);
		}

		// keep rescanning until a pass completes without a merge: sustained fire can queue
		// several absorptions inside one 400 ms window, and stopping after the first left
		// the backlog growing faster than it merged. Each merge removes one entry, so the
		// entry count bounds the rescans; shoves are applied on the first pass only so a
		// rescan cannot double a pair's push within the same tick.
		int guard = m_aEntries.Count();
		bool merged = ScanPairs(now, true);
		while (merged && guard > 0)
		{
			merged = ScanPairs(now, false);
			guard--;
		}

		if (m_aEntries.IsEmpty() && m_bTicking)
		{
			GetGame().GetCallqueue().Remove(Tick);
			m_bTicking = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One full pair scan. Separation shoves are applied only when applyShoves is set; the
	//! first successful merge mutates the entry array and returns true so the caller rescans
	//! with fresh indices instead of walking stale ones.
	protected bool ScanPairs(float now, bool applyShoves)
	{
		int count = m_aEntries.Count();
		for (int a = 0; a < count; a++)
		{
			BER_CloudEntry ea = m_aEntries[a];
			if (!ea.m_Pfx)
				continue;
			float ra = CurrentRadius(ea, now);
			vector pa = ea.m_Pfx.GetOrigin();

			for (int b = a + 1; b < count; b++)
			{
				BER_CloudEntry eb = m_aEntries[b];
				if (!eb.m_Pfx)
					continue;
				if (ea.m_iGroup == eb.m_iGroup)
					continue; // one event's own composition
				if (ea.m_bIndoor != eb.m_bIndoor)
					continue; // an indoor cloud and an outdoor one are in different air

				float rb = CurrentRadius(eb, now);
				vector pb = eb.m_Pfx.GetOrigin();
				float dist = vector.Distance(pa, pb);

				// outer bounds must overlap at all before the pair is considered
				if (dist > OUTER_FACTOR * (ra + rb))
					continue;

				if (dist < MERGE_FRACTION * (ra + rb))
				{
					if (TryMerge(ea, eb, ra, rb, now))
						return true;
				}

				// inner "collision": pressed together but not merged — shove apart gently.
				// Outdoors only: indoors a shoved cloud would be pushed toward/through the
				// room's walls, and the indoor answer to crowding is the density merge above.
				if (applyShoves && !ea.m_bIndoor && dist < ra + rb)
					SeparatePair(ea, eb, pa, pb, dist, ra + rb);
			}
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Absorb the smaller cloud into the larger: the absorbed effect stops emitting (its
	//! living particles fade on their own curves), the survivor's replenishment — and
	//! outdoors its particle size — rises to represent the union.
	protected bool TryMerge(BER_CloudEntry ea, BER_CloudEntry eb, float ra, float rb, float now)
	{
		BER_CloudEntry survivor = ea;
		BER_CloudEntry absorbed = eb;
		float rs = ra;
		float rx = rb;
		if (rb > ra)
		{
			survivor = eb;
			absorbed = ea;
			rs = rb;
			rx = ra;
		}

		if (now - absorbed.m_fBorn < absorbed.m_fMinAbsorbAge)
			return false;

		// absorbed volume relative to the survivor, capped at "equal partners"
		float ratio = (rx * rx * rx) / (rs * rs * rs);
		if (ratio > 1.0)
			ratio = 1.0;

		absorbed.m_Pfx.StopEmission();
		int idx = m_aEntries.Find(absorbed);
		if (idx != -1)
			m_aEntries.Remove(idx);

		survivor.m_fMergeBirth = survivor.m_fMergeBirth * (1.0 + 0.6 * ratio);
		if (survivor.m_fMergeBirth > MERGE_BIRTH_CAP)
			survivor.m_fMergeBirth = MERGE_BIRTH_CAP;

		if (!survivor.m_bIndoor)
		{
			// volume-conserving growth: r^3 union -> cube-root size step
			float growth = Math.Pow(1.0 + ratio, 0.3333);
			survivor.m_fMergeSize = survivor.m_fMergeSize * growth;
			if (survivor.m_fMergeSize > MERGE_SIZE_CAP)
				survivor.m_fMergeSize = MERGE_SIZE_CAP;
			survivor.m_fRadiusScale = survivor.m_fRadiusScale * growth;
			if (survivor.m_fRadiusScale > RADIUS_SCALE_CAP)
				survivor.m_fRadiusScale = RADIUS_SCALE_CAP;
		}

		ApplyBoost(survivor);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Write the survivor's boosted density/size. When the drift animator owns the effect's
	//! BIRTH_RATE (it rewrites it on travel), the boost goes through its baseline so the two
	//! systems compose instead of overwriting each other; otherwise it is applied directly.
	protected void ApplyBoost(BER_CloudEntry entry)
	{
		Particles particles = entry.m_Pfx.GetParticles();
		if (!particles)
			return;

		float birthTotal = entry.m_fBirthBase * entry.m_fMergeBirth;
		bool animatorOwnsBirth = BER_WindDriftAnimator.GetInstance().SetBirthBase(entry.m_Pfx, birthTotal);

		float sizeTotal = entry.m_fSizeBase * entry.m_fMergeSize;

		int emitterCount = particles.GetNumEmitters();
		float origLifetime;
		for (int i = 0; i < emitterCount; i++)
		{
			origLifetime = 0;
			particles.GetParamOrig(i, EmitterParam.LIFETIME, origLifetime);
			if (origLifetime < FLASH_LIFETIME_THRESHOLD)
				continue; // flash/sparks never get merge-thickened

			if (!animatorOwnsBirth)
				particles.MultParam(i, EmitterParam.BIRTH_RATE, birthTotal);
			if (!entry.m_bIndoor)
				particles.MultParam(i, EmitterParam.SIZE, sizeTotal);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Opposing horizontal shoves proportional to interpenetration — reapplied every tick
	//! while the inner bounds still overlap, so crowding resolves as a sustained gentle
	//! pressure, not a kick. Goes through the impulse channel that leaves each cloud's own
	//! dissipation scale untouched.
	protected void SeparatePair(BER_CloudEntry ea, BER_CloudEntry eb, vector pa, vector pb, float dist, float sumR)
	{
		vector axis = Vector(pb[0] - pa[0], 0, pb[2] - pa[2]);
		float flat = axis.Length();
		if (flat < 0.05)
			return; // exactly stacked — the merge path will resolve this pair instead

		axis = axis * (1.0 / flat);
		float push = SEP_SPEED * (1.0 - dist / sumR);
		if (push <= 0)
			return;

		BER_WindDriftAnimator animator = BER_WindDriftAnimator.GetInstance();
		animator.AddSeparationImpulse(ea.m_Pfx, -axis * push, ea.m_fBirthBase * ea.m_fMergeBirth, ea.m_fLifeBase);
		animator.AddSeparationImpulse(eb.m_Pfx, axis * push, eb.m_fBirthBase * eb.m_fMergeBirth, eb.m_fLifeBase);
	}
}
