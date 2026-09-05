//------------------------------------------------------------------------------------------------
// Better VFX Realism — shared surface/weather classification
//
// One classifier used by every BER feature (explosion tuning, muzzle overpressure dust)
// so all effects are driven through the same logic.
//------------------------------------------------------------------------------------------------

class BER_SurfaceUtil
{
	//------------------------------------------------------------------------------------------------
	//! Trace straight down for the ground surface. Returns true on hit.
	static bool TraceGround(BaseWorld world, vector pos, float depth, IEntity exclude, out vector groundPos, out string matName, out vector groundNormal)
	{
		TraceParam tp = new TraceParam();
		tp.Start = pos + Vector(0, 0.3, 0);
		tp.End = pos - Vector(0, depth, 0);
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		tp.Exclude = exclude;

		float frac = world.TraceMove(tp, null);
		if (frac >= 1.0)
			return false;

		vector dir = tp.End - tp.Start;
		groundPos = tp.Start + dir * frac;
		matName = "";
		groundNormal = tp.TraceNorm;
		if (tp.SurfaceProps)
			matName = tp.SurfaceProps.GetName();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Trace straight down with multiple excludes; also reports the root entity that was hit
	//! (null for terrain) so callers can detect vehicle hulls.
	static bool TraceGroundEx(BaseWorld world, vector pos, float depth, notnull array<IEntity> excludes, out vector groundPos, out string matName, out IEntity hitRoot)
	{
		TraceParam tp = new TraceParam();
		tp.Start = pos + Vector(0, 0.3, 0);
		tp.End = pos - Vector(0, depth, 0);
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		tp.ExcludeArray = excludes;

		float frac = world.TraceMove(tp, null);
		if (frac >= 1.0)
			return false;

		vector dir = tp.End - tp.Start;
		groundPos = tp.Start + dir * frac;
		matName = "";
		hitRoot = null;
		if (tp.SurfaceProps)
			matName = tp.SurfaceProps.GetName();
		if (tp.TraceEnt)
			hitRoot = tp.TraceEnt.GetRootParent();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Trace down and resolve the surface's gamemat BLAST particle effect (the per-surface
	//! table the vanilla mortar ground blast uses). Returns an empty ResourceName on miss.
	static ResourceName GetBlastResourceAt(BaseWorld world, vector pos, float depth, IEntity exclude, int effectIndex, out vector groundPos, out string matName, out vector groundNormal)
	{
		TraceParam tp = new TraceParam();
		tp.Start = pos + Vector(0, 0.3, 0);
		tp.End = pos - Vector(0, depth, 0);
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		tp.Exclude = exclude;

		float frac = world.TraceMove(tp, null);
		if (frac >= 1.0)
			return ResourceName.Empty;

		vector dir = tp.End - tp.Start;
		groundPos = tp.Start + dir * frac;
		matName = "";
		groundNormal = tp.TraceNorm;

		if (!tp.SurfaceProps)
			return ResourceName.Empty;

		matName = tp.SurfaceProps.GetName();

		GameMaterial material = tp.SurfaceProps;
		if (!material)
			return ResourceName.Empty;

		ParticleEffectInfo effectInfo = material.GetParticleEffectInfo();
		if (!effectInfo)
			return ResourceName.Empty;

		return effectInfo.GetBlastResource(effectIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! Maps a .gamemat resource name to a dustiness factor. 1.0 = neutral,
	//! above = dry/dusty (denser, lingering dust), below = wet/hard (thin, short-lived).
	static float GetDustFactor(string matName, float posY)
	{
		matName.ToLower();

		// wet / water-adjacent first
		if (matName.Contains("water") || matName.Contains("seaweed"))
			return 0;
		if (matName.Contains("ice"))
			return 0;
		if (matName.Contains("snow"))
			return 0.45; // loose snow can be entrained; ice cannot

		// Altitude alone does not establish wetness: dry sand can occur at sea level.
		if (matName.Contains("mud_dry") || matName.Contains("dried_mud"))
			return 1.6;
		if (matName.Contains("wet") || matName.Contains("mud"))
			return 0;
		if (matName.Contains("sand"))
			return 1.6;

		// dry unpaved surfaces — lots of dried mud and dust
		if (matName.Contains("dirt_road"))
			return 1.6;
		if (matName.Contains("dirt") || matName.Contains("soil_forest"))
			return 1.4;
		if (matName.Contains("soil"))
			return 1.45;
		if (matName.Contains("gravel") || matName.Contains("pebbles") || matName.Contains("debris_rock"))
			return 1.35;

		// vegetation
		if (matName.Contains("grass_dry"))
			return 1.15;
		if (matName.Contains("grass") || matName.Contains("moss") || matName.Contains("foliage"))
			return 0.85;

		// Friable wall finishes powder more readily than bare structural masonry.
		if (matName.Contains("plaster") || matName.Contains("gypsum") || matName.Contains("drywall"))
			return 1.35;

		// hard man-made surfaces — little loose dust
		if (matName.Contains("asphalt") || matName.Contains("concrete") || matName.Contains("brick")
			|| matName.Contains("cobblestone") || matName.Contains("stone") || matName.Contains("tiles"))
			return 0.7;

		if (matName.Contains("metal") || matName.Contains("armor"))
			return 0.6;
		if (matName.Contains("wood"))
			return 0.8;

		return 1.0;
	}

	//! Visual loose solid availability; wetness suppresses dust, not wet clods.
	static float GetSolidDebrisAvailability(string material)
	{
		material.ToLower();
		if (material.Contains("water") || material.Contains("seaweed") || material.Contains("snow")
			|| material.Contains("metal") || material.Contains("armor") || material.Contains("armour")
			|| material.Contains("wood") || material.Contains("glass") || material.Contains("ice"))
			return 0;
		if (material.Contains("rock") || material.Contains("stone") || material.Contains("concrete")
			|| material.Contains("asphalt") || material.Contains("brick") || material.Contains("tiles")
			|| material.Contains("cobble") || material.Contains("gravel") || material.Contains("pebbles"))
			return 0.6;
		return 1.0;
	}

	//! Artistic event scale, not explosive mass/energy. Native particles integrate flight.
	static float GetDebrisSpeedScale(float eventScale)
	{
		return Math.Pow(ClampF(eventScale, 0.25, 3.24), 0.5);
	}

	//! No-drag upward-flight estimate plus a settling margin; bounded cleanup timer.
	static float GetDebrisLifetime(float maximumLaunchSpeed)
	{
		return ClampF(2.0 * maximumLaunchSpeed / 9.81 + 0.6, 1.0, 6.0);
	}

	//------------------------------------------------------------------------------------------------
	//! Man-made paved driving surfaces — they shed no dust cloud when driven on.
	static bool IsPavedSurface(string matName)
	{
		matName.ToLower();
		return matName.Contains("asphalt") || matName.Contains("concrete") || matName.Contains("cobble")
			|| matName.Contains("tiles") || matName.Contains("paving") || matName.Contains("pavement")
			|| matName.Contains("sidewalk") || matName.Contains("brick");
	}

	// ---- ground wetness model ----
	// The engine's own wetness/water-accumulation API (LocalWeatherSituation.GetWetness)
	// is documented as WIP and never set, so BER integrates its own: the ground SOAKS
	// while it rains (harder rain soaks faster) and DRIES OUT slowly after it stops.
	// These two constants are the tuning levers: how long full rain needs to soak the
	// ground, and how long soaked ground needs to dry back out.
	protected const float WETNESS_SOAK_TIME = 240.0;   // s of full-intensity rain to fully soak
	protected const float WETNESS_DRYING_TIME = 2400.0; // s for soaked ground to dry out completely (40 min)

	protected static float s_fWetness = -1; // -1 = not yet initialized this session
	protected static float s_fWetnessLastTime;

	//------------------------------------------------------------------------------------------------
	//! Persistent ground wetness 0..1. Unlike the instantaneous rain intensity this keeps
	//! rising while rain falls and only decays over WETNESS_DRYING_TIME once it stops —
	//! a forest road stays muddy long after the downpour ends.
	//! JIP-safe: whenever a game mode is up, the value is the SERVER's replicated
	//! integrator (the server has the full weather history; a client that joined after
	//! the rain ended could not know about it). The local integrator below only serves
	//! the server itself and the no-game-mode fallback; its first sample assumes the
	//! ground matches the current sky.
	static float GetGroundWetness(BaseWorld world)
	{
		float authoritative = SCR_BaseGameMode.BER_GetAuthoritativeWetness();
		if (authoritative >= 0)
			return authoritative;
		return AdvanceLocalWetness(world);
	}

	//------------------------------------------------------------------------------------------------
	//! Advance and return the LOCAL wetness integrator. Called by the game-mode authority
	//! every publish tick, and directly only as the fallback when no authority exists.
	static float AdvanceLocalWetness(BaseWorld world)
	{
		float now = world.GetWorldTime() * 0.001;
		float rain = GetRainIntensity(world);

		if (s_fWetness < 0 || now < s_fWetnessLastTime)
		{
			s_fWetness = rain;
			s_fWetnessLastTime = now;
			return s_fWetness;
		}

		float dt = now - s_fWetnessLastTime;
		if (dt <= 0)
			return s_fWetness; // same frame — every caller shares one advance
		if (dt > 120)
			dt = 120; // long unsampled gaps advance capped, sky may have changed meanwhile
		s_fWetnessLastTime = now;

		if (rain > 0.02)
		{
			s_fWetness = s_fWetness + dt * rain / WETNESS_SOAK_TIME;
			if (s_fWetness > 1.0)
				s_fWetness = 1.0;
		}
		else
		{
			s_fWetness = s_fWetness - dt / WETNESS_DRYING_TIME;
			if (s_fWetness < 0)
				s_fWetness = 0;
		}

		return s_fWetness;
	}

	//------------------------------------------------------------------------------------------------
	//! Raw rain intensity 0..1 (0 when unavailable).
	static float GetRainIntensity(BaseWorld world)
	{
		ChimeraWorld chimeraWorld = ChimeraWorld.CastFrom(world);
		if (!chimeraWorld)
			return 0;

		TimeAndWeatherManagerEntity weatherMgr = chimeraWorld.GetTimeAndWeatherManager();
		if (!weatherMgr)
			return 0;

		float rain = weatherMgr.GetRainIntensity();
		if (rain < 0)
			return 0;
		if (rain > 1.0)
			return 1.0;
		return rain;
	}

	//------------------------------------------------------------------------------------------------
	//! Wet surfaces shed NO dust — the factor runs all the way to zero at full wetness
	//! instead of bottoming out at 40%. Driven by the WORSE of falling rain (airborne
	//! rain knocks dust down immediately) and the persistent ground wetness (the ground
	//! stays soaked long after the rain stops).
	static float GetRainFactor(BaseWorld world)
	{
		float wet = GetRainIntensity(world);
		float ground = GetGroundWetness(world);
		if (ground > wet)
			wet = ground;

		if (wet < 0.01)
			return 1.0;
		float factor = 1.0 - wet / 0.25; // same mud threshold as wheel dust
		if (factor < 0)
			factor = 0;
		return factor;
	}

	//------------------------------------------------------------------------------------------------
	//! Loose surface dust, not combustion smoke. Full dustiness is fine dry soil/sand.
	//! A roof shelters the impact from rain; explicitly wet materials remain dustless.
	static float GetDustAvailability(BaseWorld world, vector pos, string material, bool indoor)
	{
		float dust = ClampF(GetDustFactor(material, pos[1]) / 1.6, 0, 1);
		material.ToLower();
		if (material.Contains("metal") || material.Contains("armor") || material.Contains("glass")
			|| material.Contains("plastic") || material.Contains("rubber") || material.Contains("fabric")
			|| material.Contains("flesh") || material.Contains("aramid"))
			return 0;
		if (!indoor)
			dust *= GetRainFactor(world);
		return dust;
	}

	static bool HasDustEmitters(Particles particles)
	{
		array<string> names = {};
		particles.GetEmitterNames(names);
		foreach (string name : names)
		{
			if (name.IndexOf("ber_dust_") == 0)
				return true;
		}
		return false;
	}

	//! Explicit authored names replace lifetime guesses: debris can live longer than dust.
	//! Called while paused whenever we own the spawn, before even the first particle.
	static void TuneDust(Particles particles, float density, bool indoor, float size = 1.0, float lifetime = 1.0)
	{
		array<string> names = {};
		particles.GetEmitterNames(names);
		foreach (int i, string name : names)
		{
			if (name.IndexOf("ber_dust_") != 0)
				continue;
			particles.MultParam(i, EmitterParam.BIRTH_RATE, density);
			particles.MultParam(i, EmitterParam.BIRTH_RATE_RND, density);
			particles.MultParam(i, EmitterParam.SIZE, size);
			particles.MultParam(i, EmitterParam.SIZE_RND, size);
			particles.SetParam(i, EmitterParam.WIND, !indoor);
			float life = lifetime;
			if (indoor)
			{
				life *= 1.5;
				particles.MultParam(i, EmitterParam.VELOCITY, 0.65);
				particles.MultParam(i, EmitterParam.VELOCITY_RND, 0.65);
			}
			float original, random;
			particles.GetParamOrig(i, EmitterParam.LIFETIME, original);
			particles.GetParamOrig(i, EmitterParam.LIFETIME_RND, random);
			if (original + random > 0.01)
				life = ClampF(life, 0, 40.0 / (original + random));
			particles.MultParam(i, EmitterParam.LIFETIME, life);
			particles.MultParam(i, EmitterParam.LIFETIME_RND, life);
		}
	}


	//! Resolve the wall at the impact, rather than assuming an effect transform is a normal.
	static bool TraceImpact(BaseWorld world, vector pos, vector incoming, IEntity exclude, out vector hitPos, out vector normal, out string material, out IEntity hitRoot)
	{
		hitRoot = null;
		normal = vector.Zero;
		material = "";
		if (!world || incoming.LengthSq() < 0.0001)
			return false;
		incoming.Normalize();
		TraceParam trace = new TraceParam();
		trace.Start = pos - incoming * 0.75;
		trace.End = pos + incoming * 0.35;
		trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		trace.Exclude = exclude;
		float fraction = world.TraceMove(trace, null);
		if (fraction >= 1)
			return false;
		hitPos = trace.Start + (trace.End - trace.Start) * fraction;
		if (vector.DistanceSq(hitPos, pos) > 0.2025 || trace.TraceNorm.LengthSq() < 0.0001)
			return false;
		normal = trace.TraceNorm;
		normal.Normalize();
		if (vector.Dot(incoming, normal) > 0)
			normal = -normal;
		material = "";
		if (trace.SurfaceProps)
			material = trace.SurfaceProps.GetName();
		if (trace.TraceEnt)
			hitRoot = trace.TraceEnt.GetRootParent();
		return true;
	}

	//! Direction-only art model: preserve tangential momentum and retain outward lift.
	//! No grazing-angle cutoff, no damage or ricochet prediction.
	static vector GetImpactEjectaDirection(vector incoming, vector normal)
	{
		if (normal.LengthSq() < 0.0001)
			return vector.Up;
		normal.Normalize();
		if (incoming.LengthSq() < 0.0001)
			return normal;
		incoming.Normalize();
		float dot = vector.Dot(incoming, normal);
		if (dot > 0)
		{
			normal = -normal;
			dot = -dot;
		}
		float incidence = ClampF(-dot, 0, 1);
		vector tangent = incoming - normal * dot;
		vector direction = tangent + normal * (0.2 + 0.8 * incidence);
		direction.Normalize();
		return direction;
	}

	//! Bound the directional cone to the outward hemisphere, including shallow shots.
	static void TuneImpactCone(Particles particles, vector direction, vector normal)
	{
		float clearance = ClampF(vector.Dot(direction, normal), 0, 1);
		array<string> names = {};
		particles.GetEmitterNames(names);
		foreach (int i, string name : names)
		{
			if (name == "ber_dust_fines")
				continue;
			if (name.IndexOf("ber_dust_") != 0 && name.IndexOf("sparks_") != 0 && name.IndexOf("debris") != 0)
				continue; // never redirect light, fire, smoke or prefab contact triggers
			particles.SetParam(i, EmitterParam.CONEANGLE, Vector(360, 0, 45 * clearance));
		}
	}

	//! One swept center move. Stop short of a wall instead of placing haze through it.
	static vector ClipCloudPosition(BaseWorld world, vector start, vector desired, IEntity exclude)
	{
		vector delta = desired - start;
		float length = delta.Length();
		if (length < 0.001)
			return start;
		float distance = WallRayDist(world, start, delta, exclude);
		float travel = ClampF(distance - 0.08, 0, length);
		return start + delta * (travel / length);
	}

	//! Conservative local emission box, not a room mesh or a ventilation solution.
	static vector GetCloudExtent(BaseWorld world, vector center, IEntity exclude)
	{
		float x = Math.Min(WallRayDist(world, center, Vector(1.4, 0, 0), exclude), WallRayDist(world, center, Vector(-1.4, 0, 0), exclude));
		float y = Math.Min(WallRayDist(world, center, Vector(0, 0.8, 0), exclude), WallRayDist(world, center, Vector(0, -0.8, 0), exclude));
		float z = Math.Min(WallRayDist(world, center, Vector(0, 0, 1.4), exclude), WallRayDist(world, center, Vector(0, 0, -1.4), exclude));
		return Vector(Math.Max(0, x - 0.12), Math.Max(0, y - 0.12), Math.Max(0, z - 0.12)) * 0.6;
	}

	static bool HasClearPath(BaseWorld world, vector start, vector end, IEntity exclude = null)
	{
		if (vector.DistanceSq(start, end) < 0.01)
			return true;
		TraceParam trace = new TraceParam();
		trace.Start = start;
		trace.End = end;
		trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		trace.Exclude = exclude;
		return world.TraceMove(trace, null) >= 0.99;
	}

	static void ResetWetness()
	{
		s_fWetness = -1;
		s_fWetnessLastTime = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Current wind speed in m/s (0 when unavailable).
	static float GetWindSpeed(BaseWorld world)
	{
		ChimeraWorld chimeraWorld = ChimeraWorld.CastFrom(world);
		if (!chimeraWorld)
			return 0;

		TimeAndWeatherManagerEntity weatherMgr = chimeraWorld.GetTimeAndWeatherManager();
		if (!weatherMgr)
			return 0;

		return weatherMgr.GetWindSpeed();
	}

	//------------------------------------------------------------------------------------------------
	//! Normalized horizontal downwind direction (where the wind blows TO), or zero vector
	//! when unavailable. Uses the weather manager's single global wind azimuth so every
	//! drifting effect moves the same way — the per-position wind SWAY oscillates around
	//! that azimuth and a one-time sample of it can point anywhere, which made different
	//! clouds drift in different (sometimes opposite) directions. The raw angle is the
	//! blow-TO azimuth: the map UI adds 180 to it to display the meteorological FROM value.
	static vector GetWindDirection(BaseWorld world, vector pos)
	{
		ChimeraWorld chimeraWorld = ChimeraWorld.CastFrom(world);
		if (!chimeraWorld)
			return vector.Zero;

		TimeAndWeatherManagerEntity weatherMgr = chimeraWorld.GetTimeAndWeatherManager();
		if (!weatherMgr)
			return vector.Zero;

		return Vector(weatherMgr.GetWindDirection(), 0, 0).AnglesToVector();
	}

	//------------------------------------------------------------------------------------------------
	//! Indoor probe, shared by explosion tuning and muzzle dust so "indoors" means the same
	//! everywhere. Two conditions must BOTH hold:
	//!  - roof: 3+ of 5 upward rays blocked within dist,
	//!  - enclosure: walls on 3+ of 4 cardinal directions within ENCLOSE_DIST.
	//! The enclosure test is what keeps the OUTSIDE of a building outdoors: standing at an
	//! exterior wall, the tilted roof rays clip the facade/eaves above and used to classify
	//! the spot as indoors (wind exempted, room fog spawned) — but there is only a wall on
	//! ONE side there. A courtyard fails the roof test instead.
	static bool IsRoofed(BaseWorld world, vector pos, IEntity exclude, float dist)
	{
		vector start = pos + Vector(0, 0.3, 0);

		int hits = 0;
		hits += TraceRoofRay(world, start, Vector(0, dist, 0), exclude);
		hits += TraceRoofRay(world, start, Vector(0.35 * dist, dist, 0), exclude);
		hits += TraceRoofRay(world, start, Vector(-0.35 * dist, dist, 0), exclude);
		hits += TraceRoofRay(world, start, Vector(0, dist, 0.35 * dist), exclude);
		hits += TraceRoofRay(world, start, Vector(0, dist, -0.35 * dist), exclude);

		if (hits < 3)
			return false;

		vector wallStart = pos + Vector(0, 0.6, 0);
		int walls = 0;
		if (WallRayDist(world, wallStart, Vector(ENCLOSE_DIST, 0, 0), exclude) < ENCLOSE_DIST)
			walls++;
		if (WallRayDist(world, wallStart, Vector(-ENCLOSE_DIST, 0, 0), exclude) < ENCLOSE_DIST)
			walls++;
		if (WallRayDist(world, wallStart, Vector(0, 0, ENCLOSE_DIST), exclude) < ENCLOSE_DIST)
			walls++;
		if (WallRayDist(world, wallStart, Vector(0, 0, -ENCLOSE_DIST), exclude) < ENCLOSE_DIST)
			walls++;

		return walls >= 3;
	}

	protected const float ENCLOSE_DIST = 14.0; // m — max wall distance that still counts as "enclosed"

	//------------------------------------------------------------------------------------------------
	//! Distance to the nearest wall around pos (8 horizontal rays), capped at maxDist.
	//! Used to confine indoor explosion particles to the room they detonated in.
	static float GetMinWallDistance(BaseWorld world, vector pos, IEntity exclude, float maxDist)
	{
		vector start = pos + Vector(0, 0.6, 0);
		float best = maxDist;
		float d = 0.7071 * maxDist;

		best = MinWallRay(world, start, Vector(maxDist, 0, 0), exclude, best);
		best = MinWallRay(world, start, Vector(-maxDist, 0, 0), exclude, best);
		best = MinWallRay(world, start, Vector(0, 0, maxDist), exclude, best);
		best = MinWallRay(world, start, Vector(0, 0, -maxDist), exclude, best);
		best = MinWallRay(world, start, Vector(d, 0, d), exclude, best);
		best = MinWallRay(world, start, Vector(d, 0, -d), exclude, best);
		best = MinWallRay(world, start, Vector(-d, 0, d), exclude, best);
		best = MinWallRay(world, start, Vector(-d, 0, -d), exclude, best);

		return best;
	}

	//------------------------------------------------------------------------------------------------
	//! Estimate the room around an indoor position from 4 cardinal wall rays: center of the
	//! free space (so room-filling effects can sit mid-room instead of hugging the wall the
	//! event happened at) and the half-extent of the smaller axis as a room-size measure.
	static void GetRoomGeometry(BaseWorld world, vector pos, IEntity exclude, float maxDist, out vector roomCenter, out float roomHalfExtent)
	{
		vector start = pos + Vector(0, 0.6, 0);

		float dxp = WallRayDist(world, start, Vector(maxDist, 0, 0), exclude);
		float dxm = WallRayDist(world, start, Vector(-maxDist, 0, 0), exclude);
		float dzp = WallRayDist(world, start, Vector(0, 0, maxDist), exclude);
		float dzm = WallRayDist(world, start, Vector(0, 0, -maxDist), exclude);

		roomCenter = pos + Vector((dxp - dxm) * 0.5, 0, (dzp - dzm) * 0.5);

		float halfX = (dxp + dxm) * 0.5;
		float halfZ = (dzp + dzm) * 0.5;
		roomHalfExtent = halfX;
		if (halfZ < roomHalfExtent)
			roomHalfExtent = halfZ;
	}

	//------------------------------------------------------------------------------------------------
	protected static float WallRayDist(BaseWorld world, vector start, vector offset, IEntity exclude)
	{
		TraceParam tp = new TraceParam();
		tp.Start = start;
		tp.End = start + offset;
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		tp.Exclude = exclude;

		return offset.Length() * world.TraceMove(tp, null);
	}

	//------------------------------------------------------------------------------------------------
	protected static float MinWallRay(BaseWorld world, vector start, vector offset, IEntity exclude, float best)
	{
		TraceParam tp = new TraceParam();
		tp.Start = start;
		tp.End = start + offset;
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		tp.Exclude = exclude;

		float frac = world.TraceMove(tp, null);
		if (frac >= 1.0)
			return best;

		float dist = offset.Length() * frac;
		if (dist < best)
			return dist;
		return best;
	}

	//------------------------------------------------------------------------------------------------
	protected static int TraceRoofRay(BaseWorld world, vector start, vector offset, IEntity exclude)
	{
		TraceParam tp = new TraceParam();
		tp.Start = start;
		tp.End = start + offset;
		tp.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		tp.Exclude = exclude;
		if (world.TraceMove(tp, null) < 1.0)
			return 1;
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	static float ClampF(float value, float lo, float hi)
	{
		if (value < lo)
			return lo;
		if (value > hi)
			return hi;
		return value;
	}
}

//------------------------------------------------------------------------------------------------
// Registry of particle effects BER itself spawned. The tuning component's adoption sphere
// queries pick up EVERY nearby ParticleEffectEntity — without this, one warhead adopts
// another warhead's takeover cloud (double drift registration), its own room fog / ground
// debris (lifetime re-boosted far past authored values) or a muzzle-dust puff (reoriented).
// Everything BER spawns is marked here and skipped by adoption.
class BER_OwnedEffects
{
	protected static ref array<ParticleEffectEntity> s_aOwned;
	protected static ref array<float> s_aTime;

	// longest-lived BER-spawned effect is an indoor smoke plume (up to ~120 s emission
	// plus particle lifetime) — anything older than this can safely fall out
	protected const float KEEP_TIME = 300.0;
	protected static BaseWorld s_World;
	protected static float s_fClock;
	protected static void EnsureState(BaseWorld world)
	{
		float now = world.GetWorldTime();
		if (!s_aOwned || s_World != world || now < s_fClock)
		{
			s_World = world;
			s_aOwned = {};
			s_aTime = {};
		}
		s_fClock = now;
	}

	//------------------------------------------------------------------------------------------------
	static void MarkOwned(ParticleEffectEntity pfx)
	{
		if (!pfx)
			return;

		EnsureState(pfx.GetWorld());
		if (s_aOwned.Find(pfx) != -1)
			return;
		float now = pfx.GetWorld().GetWorldTime() * 0.001;

		for (int i = s_aOwned.Count() - 1; i >= 0; i--)
		{
			if (!s_aOwned[i] || now - s_aTime[i] > KEEP_TIME)
			{
				s_aOwned.Remove(i);
				s_aTime.Remove(i);
			}
		}

		if (s_aOwned.Count() >= 2048)
		{
			s_aOwned.Remove(0);
			s_aTime.Remove(0);
		}
		s_aOwned.Insert(pfx);
		s_aTime.Insert(now);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsOwned(ParticleEffectEntity pfx)
	{
		if (!pfx)
			return false;
		EnsureState(pfx.GetWorld());
		return s_aOwned.Find(pfx) != -1;
	}
}
