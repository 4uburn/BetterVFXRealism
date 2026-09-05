//------------------------------------------------------------------------------------------------
// Better Effects Realism — wind drift animator
//
// Emitter-level wind cannot be modulated after an effect exists, so BER-owned effects have
// engine wind disabled, simulate in local space, and are drifted by MOVING THE EFFECT
// ENTITY here instead:
//  - HOLD phase: the blast pressure wave locally cancels the wind — the cloud stands
//    still. hold = HOLD_MAX * (1 - windSpeed / WIND_OVERCOME_SPEED), clamped >= 0, so
//    stronger wind overcomes the pressure wave sooner.
//  - RAMP phase: drift velocity accelerates along the real wind direction with a
//    smoothstep curve up to the actual wind speed over the ramp time.
//  - After the ramp the cloud keeps drifting at wind speed until the effect dies.
//  - CONCUSSION IMPULSE: AddImpulse gives an effect an instant shove that decays away
//    within a fraction of a second — a fresh muzzle blast blowing the previous shots'
//    dust outbound. Impulses apply even during the pressure-hold phase and work with no
//    wind at all (an impulse-only entry is created on demand), and impulse-shoved
//    effects use much shorter dissipation distances so the blown-away dust thins out
//    at small-arms scale instead of explosion-cloud scale.
//  - TRAVEL DISSIPATION: the further the cloud has drifted, the faster it thins out —
//    replenishment (BIRTH_RATE) and the lifetime of newly born particles decay with the
//    distance travelled, so a cloud standing still (indoors, becalmed, pressure hold)
//    stays thick and long-lived while a wind-carried one falls apart on the way. Decay
//    multiplies the caller's applied baseline (MultParam is absolute vs the authored
//    original), so the surface/weather tuning stays intact. Particles already alive keep
//    their alpha curves — the engine normalizes them over each particle's own lifetime.
// Runs on a per-frame CallQueue tick (delay 0) with real elapsed world time, so the
// drift is as smooth as the user's framerate; independent of the short-lived warhead
// that registered it.
//------------------------------------------------------------------------------------------------

class BER_WindDriftEntry
{
	ParticleEffectEntity m_Pfx;
	float m_fAge;
	float m_fHold;
	float m_fRamp;
	float m_fWindSpeed;
	vector m_vDir;
	float m_fBirthBase;
	float m_fLifeBase;
	float m_fTraveled;
	float m_fAppliedFactor;
	vector m_vImpulse;
	float m_fBirthDist;
	float m_fLifeDist;
	vector m_vBlockNormal;   // surface blocking the drift path (zero = clear)
	float m_fBlockCheckIn;   // s until the next obstruction probe
}

class BER_WindDriftAnimator
{
	protected static ref BER_WindDriftAnimator s_Instance;

	protected ref array<ref BER_WindDriftEntry> m_aEntries = {};
	protected bool m_bTicking;
	protected float m_fLastTickTime;

	protected const float MAX_DT = 0.25;                // clamp huge frame hitches so clouds don't teleport
	protected const float HOLD_MAX = 3.0;               // seconds of standstill in calm air
	protected const float WIND_OVERCOME_SPEED = 10.0;   // m/s of wind that fully overcomes the pressure wave
	protected const float MAX_LIFETIME = 120.0;         // hard cap so entries can never leak

	protected const float DISSIPATE_BIRTH_DIST = 28.0;  // m of travel over which replenishment fades out
	protected const float DISSIPATE_BIRTH_FLOOR = 0.12;
	protected const float DISSIPATE_LIFE_DIST = 40.0;   // m of travel over which new-particle lifetime shortens
	protected const float DISSIPATE_LIFE_FLOOR = 0.35;
	protected const float DISSIPATE_STEP = 0.04;        // reapply only when the factor moved this much

	protected const float IMPULSE_DECAY = 1.5;          // 1/s — vented gas keeps gliding, it never stops abruptly
	protected const float IMPULSE_MIN = 0.05;           // m/s below which the impulse is dropped
	protected const float IMPULSE_BIRTH_DIST = 2.5;     // m — concussion-blown small puffs thin out fast
	protected const float IMPULSE_LIFE_DIST = 4.0;

	//------------------------------------------------------------------------------------------------
	static BER_WindDriftAnimator GetInstance()
	{
		if (!s_Instance)
			s_Instance = new BER_WindDriftAnimator();
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! birthBase/lifeBase: the BIRTH_RATE / LIFETIME multipliers (vs the authored original)
	//! the caller has already applied, so travel decay can scale them down without undoing them.
	//! holdScale scales the pressure-hold standstill: 1.0 for explosion clouds, near 0 for
	//! light short-lived gas (rifle puffs) the wind should grab almost immediately.
	void Register(ParticleEffectEntity pfx, float rampTime, float birthBase = 1.0, float lifeBase = 1.0, float holdScale = 1.0)
	{
		if (!pfx || rampTime < 0.05)
			return;

		foreach (BER_WindDriftEntry existing : m_aEntries)
		{
			if (existing.m_Pfx == pfx)
				return;
		}

		BaseWorld world = pfx.GetWorld();
		float wind = BER_SurfaceUtil.GetWindSpeed(world);
		if (wind < 0.05)
			return; // no wind, nothing to animate

		vector dir = BER_SurfaceUtil.GetWindDirection(world, pfx.GetOrigin());
		if (dir == vector.Zero)
			return;

		float holdFraction = 1.0 - wind / WIND_OVERCOME_SPEED;
		if (holdFraction < 0)
			holdFraction = 0;

		BER_WindDriftEntry entry = new BER_WindDriftEntry();
		entry.m_Pfx = pfx;
		entry.m_fAge = 0;
		entry.m_fHold = HOLD_MAX * holdFraction * holdScale;
		entry.m_fRamp = rampTime;
		entry.m_fWindSpeed = wind;
		entry.m_vDir = dir;
		entry.m_fBirthBase = birthBase;
		entry.m_fLifeBase = lifeBase;
		entry.m_fTraveled = 0;
		entry.m_fAppliedFactor = 1.0;
		entry.m_vImpulse = vector.Zero;
		entry.m_fBirthDist = DISSIPATE_BIRTH_DIST;
		entry.m_fLifeDist = DISSIPATE_LIFE_DIST;
		m_aEntries.Insert(entry);

		StartTicking(world);
	}

	//------------------------------------------------------------------------------------------------
	//! An instant shove (world-space velocity) that decays away within a fraction of a
	//! second — a muzzle concussion blowing existing dust outbound. Works during the
	//! pressure-hold phase and with no wind at all; the shoved effect switches to the
	//! short small-puff dissipation distances so being blown away also thins it out.
	//! birthBase/lifeBase: as in Register, only used when a new entry must be created.
	void AddImpulse(ParticleEffectEntity pfx, vector vel, float birthBase = 1.0, float lifeBase = 1.0)
	{
		if (!pfx || vel == vector.Zero)
			return;

		foreach (BER_WindDriftEntry existing : m_aEntries)
		{
			if (existing.m_Pfx == pfx)
			{
				existing.m_vImpulse = existing.m_vImpulse + vel;
				existing.m_fBirthDist = IMPULSE_BIRTH_DIST;
				existing.m_fLifeDist = IMPULSE_LIFE_DIST;
				return;
			}
		}

		BaseWorld world = pfx.GetWorld();
		if (!world)
			return;

		// no wind entry exists (indoors or calm air) — impulse-only entry, never wind-driven
		BER_WindDriftEntry entry = new BER_WindDriftEntry();
		entry.m_Pfx = pfx;
		entry.m_fAge = 0;
		entry.m_fHold = MAX_LIFETIME;
		entry.m_fRamp = 1.0;
		entry.m_fWindSpeed = 0;
		entry.m_vDir = vector.Zero;
		entry.m_fBirthBase = birthBase;
		entry.m_fLifeBase = lifeBase;
		entry.m_fTraveled = 0;
		entry.m_fAppliedFactor = 1.0;
		entry.m_vImpulse = vel;
		entry.m_fBirthDist = IMPULSE_BIRTH_DIST;
		entry.m_fLifeDist = IMPULSE_LIFE_DIST;
		m_aEntries.Insert(entry);

		StartTicking(world);
	}

	//------------------------------------------------------------------------------------------------
	//! Like AddImpulse, but for the cloud field's separation shoves: it must NOT switch the
	//! effect to the short small-puff dissipation distances — two big clouds shouldering
	//! each other apart keep dissipating at their own scale. A new entry (indoor / calm air)
	//! gets the default long distances for the same reason.
	void AddSeparationImpulse(ParticleEffectEntity pfx, vector vel, float birthBase = 1.0, float lifeBase = 1.0)
	{
		if (!pfx || vel == vector.Zero)
			return;

		foreach (BER_WindDriftEntry existing : m_aEntries)
		{
			if (existing.m_Pfx == pfx)
			{
				existing.m_vImpulse = existing.m_vImpulse + vel;
				return;
			}
		}

		BaseWorld world = pfx.GetWorld();
		if (!world)
			return;

		BER_WindDriftEntry entry = new BER_WindDriftEntry();
		entry.m_Pfx = pfx;
		entry.m_fAge = 0;
		entry.m_fHold = MAX_LIFETIME;
		entry.m_fRamp = 1.0;
		entry.m_fWindSpeed = 0;
		entry.m_vDir = vector.Zero;
		entry.m_fBirthBase = birthBase;
		entry.m_fLifeBase = lifeBase;
		entry.m_fTraveled = 0;
		entry.m_fAppliedFactor = 1.0;
		entry.m_vImpulse = vel;
		entry.m_fBirthDist = DISSIPATE_BIRTH_DIST;
		entry.m_fLifeDist = DISSIPATE_LIFE_DIST;
		m_aEntries.Insert(entry);

		StartTicking(world);
	}

	//------------------------------------------------------------------------------------------------
	//! The cloud field's merge boosts must compose with the travel dissipation instead of
	//! being overwritten by its next BIRTH_RATE write: when an entry exists for pfx, its
	//! birth baseline is replaced with the new total and reapplied immediately at the
	//! current travel factor. Returns false when this animator does not own the effect —
	//! the caller applies its boost directly then.
	bool SetBirthBase(ParticleEffectEntity pfx, float birthBase)
	{
		foreach (BER_WindDriftEntry entry : m_aEntries)
		{
			if (entry.m_Pfx != pfx)
				continue;

			entry.m_fBirthBase = birthBase;

			float birthFactor = 1.0 - entry.m_fTraveled / entry.m_fBirthDist;
			if (birthFactor < DISSIPATE_BIRTH_FLOOR)
				birthFactor = DISSIPATE_BIRTH_FLOOR;

			Particles particles = pfx.GetParticles();
			if (particles)
			{
				int emitterCount = particles.GetNumEmitters();
				for (int i = 0; i < emitterCount; i++)
					particles.MultParam(i, EmitterParam.BIRTH_RATE, birthBase * birthFactor);
			}
			return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! An explosion's pressure wave shoving every animated cloud within reach radially away
	//! from the detonation, strongest up close. Unlike AddImpulse this leaves the entries'
	//! dissipation distances alone — a swept explosion cloud keeps its own thinning scale.
	void ImpulseSweep(vector center, float radius, float speed)
	{
		foreach (BER_WindDriftEntry entry : m_aEntries)
		{
			if (!entry.m_Pfx)
				continue;

			vector pos = entry.m_Pfx.GetOrigin();
			vector away = Vector(pos[0] - center[0], 0, pos[2] - center[2]);
			float dist = away.Length();
			if (dist > radius)
				continue;

			if (dist < 0.2)
				continue; // the detonation's own cloud at the epicenter — nowhere to push it

			away = away * (1.0 / dist);
			float shove = speed * (1.0 - dist / radius);
			if (shove < 0.3)
				shove = 0.3;

			entry.m_vImpulse = entry.m_vImpulse + away * shove;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void StartTicking(BaseWorld world)
	{
		if (m_bTicking)
			return;
		m_fLastTickTime = world.GetWorldTime();
		GetGame().GetCallqueue().CallLater(Tick, 0, true); // delay 0 + repeat = every frame
		m_bTicking = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		float now = world.GetWorldTime();
		float dt = (now - m_fLastTickTime) * 0.001;
		m_fLastTickTime = now;
		if (dt <= 0)
			return;
		if (dt > MAX_DT)
			dt = MAX_DT;

		for (int i = m_aEntries.Count() - 1; i >= 0; i--)
		{
			BER_WindDriftEntry entry = m_aEntries[i];
			if (!entry.m_Pfx || entry.m_fAge > MAX_LIFETIME)
			{
				m_aEntries.Remove(i);
				continue;
			}

			entry.m_fAge += dt;

			vector move = vector.Zero;
			float stepLen = 0;

			// concussion impulse: applies immediately (even during the pressure hold),
			// decaying exponentially so a shove travels a short, sharp distance
			if (entry.m_vImpulse != vector.Zero)
			{
				move = entry.m_vImpulse * dt;
				stepLen = entry.m_vImpulse.Length() * dt;
				float keep = 1.0 - IMPULSE_DECAY * dt;
				if (keep < 0.1)
					keep = 0.1;
				entry.m_vImpulse = entry.m_vImpulse * keep;
				if (entry.m_vImpulse.Length() < IMPULSE_MIN)
					entry.m_vImpulse = vector.Zero;
			}

			// wind drift once the pressure wave no longer dominates
			if (entry.m_fWindSpeed > 0 && entry.m_fAge > entry.m_fHold)
			{
				float t = (entry.m_fAge - entry.m_fHold) / entry.m_fRamp;
				if (t > 1.0)
					t = 1.0;
				float f = t * t * (3.0 - 2.0 * t); // smoothstep
				float step = entry.m_fWindSpeed * f * dt;
				move = move + entry.m_vDir * step;
				stepLen = stepLen + step;
			}

			if (stepLen <= 0)
				continue;

			// the wind cannot push a cloud through a building: probe ahead of the drift
			// (throttled — one short ray every 0.4 s per moving entry) and, when a wall
			// blocks the path, strip the movement component INTO it so the cloud slides
			// along the facade instead of the entity passing through while its collided
			// particles pile up and stick to the exterior wall
			vector pos = entry.m_Pfx.GetOrigin();
			entry.m_fBlockCheckIn -= dt;
			if (entry.m_fBlockCheckIn <= 0)
			{
				entry.m_fBlockCheckIn = 0.4;
				entry.m_vBlockNormal = ProbeObstruction(world, pos, move);
			}
			if (entry.m_vBlockNormal != vector.Zero)
			{
				float into = vector.Dot(move, entry.m_vBlockNormal);
				if (into < 0)
					move = move - entry.m_vBlockNormal * into;
				stepLen = move.Length();
				if (stepLen <= 0.0001)
					continue; // pinned flat against the obstacle — no drift this frame
			}

			entry.m_Pfx.SetOrigin(pos + move);
			entry.m_fTraveled += stepLen;

			ApplyTravelDissipation(entry);
		}

		if (m_aEntries.IsEmpty() && m_bTicking)
		{
			GetGame().GetCallqueue().Remove(Tick);
			m_bTicking = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Short look-ahead ray along the current drift direction: returns the blocking
	//! surface's normal (oriented against the movement), or zero when the path is clear.
	protected vector ProbeObstruction(BaseWorld world, vector pos, vector move)
	{
		float moveLen = move.Length();
		if (moveLen < 0.0001)
			return vector.Zero;

		vector dir = move * (1.0 / moveLen);

		TraceParam tp = new TraceParam();
		tp.Start = pos + Vector(0, 0.5, 0);
		tp.End = tp.Start + dir * 1.5; // roughly the cloud's leading edge
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;

		if (world.TraceMove(tp, null) >= 1.0)
			return vector.Zero;

		vector n = tp.TraceNorm;
		if (vector.Dot(n, dir) > 0)
			n = n * -1.0; // face the normal against the movement
		return n;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyTravelDissipation(BER_WindDriftEntry entry)
	{
		float birthFactor = 1.0 - entry.m_fTraveled / entry.m_fBirthDist;
		if (birthFactor < DISSIPATE_BIRTH_FLOOR)
			birthFactor = DISSIPATE_BIRTH_FLOOR;

		if (entry.m_fAppliedFactor - birthFactor < DISSIPATE_STEP)
			return; // hasn't travelled meaningfully further since the last application
		entry.m_fAppliedFactor = birthFactor;

		float lifeFactor = 1.0 - entry.m_fTraveled / entry.m_fLifeDist;
		if (lifeFactor < DISSIPATE_LIFE_FLOOR)
			lifeFactor = DISSIPATE_LIFE_FLOOR;

		Particles particles = entry.m_Pfx.GetParticles();
		if (!particles)
			return;

		int emitterCount = particles.GetNumEmitters();
		for (int i = 0; i < emitterCount; i++)
		{
			particles.MultParam(i, EmitterParam.BIRTH_RATE, entry.m_fBirthBase * birthFactor);
			particles.MultParam(i, EmitterParam.LIFETIME, entry.m_fLifeBase * lifeFactor);
		}
	}
}
