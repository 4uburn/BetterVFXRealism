// Better VFX Realism: bounded advection of the local-space muzzle dust effects only.
// Explosion/impact dust uses native world-space particle drag. A short visual response
// delay is not a model of blast pressure cancelling ambient wind. Weather is refreshed
// twice a second; concussion impulses decay and are blocked by intervening surfaces.

class BER_WindDriftEntry
{
	ParticleEffectEntity m_Pfx;
	float m_fAge;
	bool m_bWindDriven;
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
	vector m_vBlockPoint;
	vector m_vBlockNormal;   // surface blocking the drift path (zero = clear)
	float m_fBlockCheckIn;   // s until the next obstruction probe
}

class BER_WindDriftAnimator
{
	protected static ref BER_WindDriftAnimator s_Instance;

	protected ref array<ref BER_WindDriftEntry> m_aEntries = {};
	protected bool m_bTicking;
	protected float m_fLastTickTime;
	protected BaseWorld m_World;
	protected float m_fWeatherIn;

	protected const float MAX_DT = 0.25;                // clamp huge frame hitches so clouds don't teleport
	protected const float HOLD_MAX = 0.08;               // maximum visual onset delay (not a blast-pressure duration)
	protected const float WIND_OVERCOME_SPEED = 10.0;   // m/s at which the visual onset delay reaches zero
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
		BaseWorld world = GetGame().GetWorld();
		if (s_Instance.m_World && (s_Instance.m_World != world
			|| (world && world.GetWorldTime() < s_Instance.m_fLastTickTime)))
		{
			GetGame().GetCallqueue().Remove(s_Instance.Tick);
			s_Instance.m_aEntries.Clear();
			s_Instance.m_bTicking = false;
			s_Instance.m_World = world;
		}
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! birthBase/lifeBase: the BIRTH_RATE / LIFETIME multipliers (vs the authored original)
	//! the caller has already applied, so travel decay can scale them down without undoing them.
	//! holdScale scales the brief visual onset delay for lightweight muzzle dust.
	void Register(ParticleEffectEntity pfx, float rampTime, float birthBase = 1.0, float lifeBase = 1.0, float holdScale = 1.0)
	{
		if (!pfx || pfx.GetParent() || rampTime < 0.05 || m_aEntries.Count() >= 256)
			return;

		foreach (BER_WindDriftEntry existing : m_aEntries)
		{
			if (existing.m_Pfx == pfx)
				return;
		}

		BaseWorld world = pfx.GetWorld();
		float wind = BER_SurfaceUtil.GetWindSpeed(world);
		// Keep calm-weather entries: a later gust must still pick them up.

		vector dir = BER_SurfaceUtil.GetWindDirection(world, pfx.GetOrigin());


		float holdFraction = 1.0 - wind / WIND_OVERCOME_SPEED;
		if (holdFraction < 0)
			holdFraction = 0;

		BER_WindDriftEntry entry = new BER_WindDriftEntry();
		entry.m_Pfx = pfx;
		entry.m_fAge = 0;
		entry.m_bWindDriven = true;
		entry.m_fHold = HOLD_MAX * holdFraction * holdScale;
		entry.m_fRamp = BER_SurfaceUtil.ClampF(rampTime, 0.1, 3.0);
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
	//! visual onset delay and with no wind at all; the shoved effect switches to the
	//! short small-puff dissipation distances so being blown away also thins it out.
	//! birthBase/lifeBase: as in Register, only used when a new entry must be created.
	void AddImpulse(ParticleEffectEntity pfx, vector vel, float birthBase = 1.0, float lifeBase = 1.0)
	{
		if (!pfx || pfx.GetParent() || vel == vector.Zero)
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
		if (!world || m_aEntries.Count() >= 256)
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
	protected void StartTicking(BaseWorld world)
	{
		if (m_bTicking)
			return;
		m_World = world;
		m_fLastTickTime = world.GetWorldTime();
		GetGame().GetCallqueue().CallLater(Tick, 0, true); // delay 0 + repeat = every frame
		m_bTicking = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world || world != m_World || world.GetWorldTime() < m_fLastTickTime)
		{
			m_aEntries.Clear();
			GetGame().GetCallqueue().Remove(Tick);
			m_bTicking = false;
			return;
		}

		float now = world.GetWorldTime();
		float dt = (now - m_fLastTickTime) * 0.001;
		m_fLastTickTime = now;
		if (dt <= 0)
			return;
		if (dt > MAX_DT)
			dt = MAX_DT;

		m_fWeatherIn -= dt;
		bool refreshWeather = m_fWeatherIn <= 0;
		float wind;
		vector windDirection;
		if (refreshWeather)
		{
			m_fWeatherIn = 0.5;
			wind = BER_SurfaceUtil.GetWindSpeed(world);
			windDirection = BER_SurfaceUtil.GetWindDirection(world, vector.Zero);
		}
		for (int i = m_aEntries.Count() - 1; i >= 0; i--)
		{
			BER_WindDriftEntry entry = m_aEntries[i];
			if (!entry.m_Pfx || entry.m_fAge > MAX_LIFETIME || entry.m_Pfx.GetState() == EParticleEffectState.STOPPED)
			{
				m_aEntries.Remove(i);
				continue;
			}

			entry.m_fAge += dt;
			if (refreshWeather && entry.m_bWindDriven)
			{
				entry.m_fWindSpeed = wind;
				entry.m_vDir = windDirection;
				entry.m_fBlockCheckIn = 0;
			}
			float impulseSpeed = entry.m_vImpulse.Length();
			if (impulseSpeed > 5.0)
				entry.m_vImpulse *= 5.0 / impulseSpeed;

			vector move = vector.Zero;
			float stepLen = 0;

			// concussion impulse: applies immediately (even during the visual onset delay),
			// decaying exponentially so a shove travels a short, sharp distance
			if (entry.m_vImpulse != vector.Zero)
			{
				float keep = BER_SurfaceUtil.Decay(1, dt, 1.0 / IMPULSE_DECAY);
				move = entry.m_vImpulse * ((1.0 - keep) / IMPULSE_DECAY);
				stepLen = move.Length();
				entry.m_vImpulse = entry.m_vImpulse * keep;
				if (entry.m_vImpulse.Length() < IMPULSE_MIN)
					entry.m_vImpulse = vector.Zero;
			}

			// wind response after the short visual onset delay
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
			// (throttled — one short ray every 0.2 s per moving entry) and, when a wall
			// blocks the path, strip the movement component INTO it so the cloud slides
			// along the facade instead of the entity passing through while its collided
			// particles pile up and stick to the exterior wall
			vector pos = entry.m_Pfx.GetOrigin();
			entry.m_fBlockCheckIn -= dt;
			if (entry.m_fBlockCheckIn <= 0)
			{
				entry.m_fBlockCheckIn = 0.2;
				entry.m_vBlockNormal = ProbeObstruction(world, pos, move, 0.1 + (entry.m_fWindSpeed + 5.0) * (0.2 + dt), entry.m_vBlockPoint);
			}
			if (entry.m_vBlockNormal != vector.Zero)
			{
				move = BER_SurfaceUtil.ClipDisplacementToPlane(pos, move, entry.m_vBlockPoint, entry.m_vBlockNormal);
				stepLen = move.Length();
				if (stepLen <= 0.0001)
					continue; // pinned flat against the obstacle — no drift this frame
			}

			entry.m_Pfx.SetOrigin(pos + move);
			entry.m_fTraveled += move.Length();

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
	protected vector ProbeObstruction(BaseWorld world, vector pos, vector move, float lookAhead, out vector hitPoint)
	{
		float moveLen = move.Length();
		if (moveLen < 0.0001)
			return vector.Zero;

		vector dir = move * (1.0 / moveLen);

		TraceParam tp = new TraceParam();
		tp.Start = pos;
		tp.End = tp.Start + dir * lookAhead; // covers travel until the next probe, even in strong wind
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;

		float fraction = world.TraceMove(tp, null);
		if (fraction >= 1.0)
			return vector.Zero;
		hitPoint = tp.Start + (tp.End - tp.Start) * fraction;

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
			particles.MultParam(i, EmitterParam.BIRTH_RATE_RND, entry.m_fBirthBase * birthFactor);
			particles.MultParam(i, EmitterParam.LIFETIME, entry.m_fLifeBase * lifeFactor);
			particles.MultParam(i, EmitterParam.LIFETIME_RND, entry.m_fLifeBase * lifeFactor);
		}
	}
}
