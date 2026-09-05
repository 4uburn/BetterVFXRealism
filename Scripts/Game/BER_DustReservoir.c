//------------------------------------------------------------------------------------------------
// Better Effects Realism — dust reservoir (accumulation / rip-off / regeneration)
//
// Thin dust layers are finite: each dust kick-up event drains a local reservoir and the
// layer regenerates slowly. Very dirty ground (dirt roads, forest floor, gravel) is a
// functionally infinite dust source and never depletes.
//  - GROUND: 3 m grid cells, each holding 0..1 dust. Hard surfaces (roads, rock) refill
//    slowly over ~90 s.
//  - VEHICLE HULLS: one reservoir per vehicle. Dust builds back up from driving — faster
//    at speed and on dusty terrain (offroad), very slowly when parked.
//  - Blast shocks (explosions, big muzzle concussion) rip accumulated dust off everything
//    nearby via RipArea.
//------------------------------------------------------------------------------------------------

class BER_DustCell
{
	vector m_vPos;
	float m_fRemaining;
	float m_fLastTime;
}

class BER_VehicleDust
{
	IEntity m_Vehicle;
	float m_fRemaining;
	float m_fLastTime;
}

class BER_DustReservoir
{
	protected static ref map<string, ref BER_DustCell> s_mCells = new map<string, ref BER_DustCell>();
	protected static ref array<ref BER_VehicleDust> s_aVehicles = {};

	protected const float CELL_SIZE = 3.0;
	protected const int MAX_CELLS = 512;
	protected const float GROUND_REGEN_PER_SEC = 0.011;   // hard surfaces: full layer back in ~90 s
	protected const float VEHICLE_REGEN_PER_SEC = 0.016;  // base rate, scaled by movement and terrain

	//------------------------------------------------------------------------------------------------
	protected static float Now(BaseWorld world)
	{
		return world.GetWorldTime() * 0.001;
	}

	//------------------------------------------------------------------------------------------------
	//! Availability (0..1) of the thin dust layer at pos, then drains it by ripAmount.
	static float TakeGround(BaseWorld world, vector pos, float ripAmount)
	{
		float now = Now(world);
		int cx = Math.Floor(pos[0] / CELL_SIZE);
		int cz = Math.Floor(pos[2] / CELL_SIZE);
		string key = cx.ToString() + "_" + cz.ToString();

		BER_DustCell cell = s_mCells.Get(key);
		if (!cell)
		{
			if (s_mCells.Count() >= MAX_CELLS)
				s_mCells.Clear(); // crude but safe pressure valve

			cell = new BER_DustCell();
			cell.m_vPos = pos;
			cell.m_fRemaining = 1.0;
			cell.m_fLastTime = now;
			s_mCells.Set(key, cell);
		}

		// regenerate since last event
		float dt = now - cell.m_fLastTime;
		if (dt > 0)
			cell.m_fRemaining = BER_SurfaceUtil.ClampF(cell.m_fRemaining + dt * GROUND_REGEN_PER_SEC, 0, 1);
		cell.m_fLastTime = now;

		float available = cell.m_fRemaining;
		cell.m_fRemaining = BER_SurfaceUtil.ClampF(cell.m_fRemaining - ripAmount, 0, 1);
		return available;
	}

	//------------------------------------------------------------------------------------------------
	//! Availability (0..1) of dust accumulated on a vehicle hull, then drains it.
	//! Regeneration scales with how much the vehicle moves and how dusty the terrain is.
	static float TakeVehicle(notnull IEntity vehicle, BaseWorld world, float ripAmount)
	{
		float now = Now(world);

		BER_VehicleDust entry = null;
		for (int i = s_aVehicles.Count() - 1; i >= 0; i--)
		{
			if (!s_aVehicles[i].m_Vehicle)
			{
				s_aVehicles.Remove(i); // vehicle deleted
				continue;
			}
			if (s_aVehicles[i].m_Vehicle == vehicle)
				entry = s_aVehicles[i];
		}

		if (!entry)
		{
			entry = new BER_VehicleDust();
			entry.m_Vehicle = vehicle;
			entry.m_fRemaining = 1.0; // starts fully dusty — first burst rips it all off
			entry.m_fLastTime = now;
			s_aVehicles.Insert(entry);
		}

		// regen factor: parked = slow, driving on dusty terrain = fast
		float speed = 0;
		Physics physics = vehicle.GetPhysics();
		if (physics)
			speed = physics.GetVelocity().Length();
		float speedNorm = BER_SurfaceUtil.ClampF(speed / 8.0, 0, 1.5);

		float terrainDust = 1.0;
		vector groundPos;
		string matName;
		vector groundNormal;
		if (BER_SurfaceUtil.TraceGround(world, vehicle.GetOrigin() + Vector(0, 1, 0), 6.0, vehicle, groundPos, matName, groundNormal) && matName != "")
			terrainDust = BER_SurfaceUtil.GetDustFactor(matName, groundPos[1]);

		float regenFactor = 0.25 + speedNorm * terrainDust;

		float dt = now - entry.m_fLastTime;
		if (dt > 0)
			entry.m_fRemaining = BER_SurfaceUtil.ClampF(entry.m_fRemaining + dt * VEHICLE_REGEN_PER_SEC * regenFactor, 0, 1);
		entry.m_fLastTime = now;

		float available = entry.m_fRemaining;
		entry.m_fRemaining = BER_SurfaceUtil.ClampF(entry.m_fRemaining - ripAmount, 0, 1);
		return available;
	}

	//------------------------------------------------------------------------------------------------
	//! A blast shock rips accumulated dust off every thin layer and hull in the area.
	static void RipArea(BaseWorld world, vector pos, float radius, float amount)
	{
		float now = Now(world);
		float radiusSq = radius * radius;

		foreach (string key, BER_DustCell cell : s_mCells)
		{
			if (vector.DistanceSq(cell.m_vPos, pos) <= radiusSq)
			{
				cell.m_fRemaining = BER_SurfaceUtil.ClampF(cell.m_fRemaining - amount, 0, 1);
				cell.m_fLastTime = now;
			}
		}

		for (int i = s_aVehicles.Count() - 1; i >= 0; i--)
		{
			BER_VehicleDust entry = s_aVehicles[i];
			if (!entry.m_Vehicle)
			{
				s_aVehicles.Remove(i);
				continue;
			}
			if (vector.DistanceSq(entry.m_Vehicle.GetOrigin(), pos) <= radiusSq)
			{
				entry.m_fRemaining = BER_SurfaceUtil.ClampF(entry.m_fRemaining - amount, 0, 1);
				entry.m_fLastTime = now;
			}
		}
	}
}
