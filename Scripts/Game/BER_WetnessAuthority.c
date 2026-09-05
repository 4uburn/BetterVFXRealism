//------------------------------------------------------------------------------------------------
// Better Effects Realism — server-authoritative ground wetness
//
// The ground-wetness accumulator (BER_SurfaceUtil) depends on weather HISTORY, and the
// weather API exposes no past states — a client that joins in progress cannot know it
// rained before they connected, so their roads would be dusty while everyone else's are
// muddy, for up to the whole drying window. JIP rule: logical behaviors must survive
// join-in-progress.
//
// Fix: the SERVER runs the integrator (it has the full history by construction) and
// mirrors the value into an [RplProp] on the game mode — RplProps are part of the
// entity's stream-in payload, so a JIP client receives the current wetness the moment
// the game mode streams in. All clients therefore read the same number; the local
// integrator remains only as a fallback where no game mode is reachable.
// ([RplProp] members on modded classes are a proven shipping-mod pattern.)
//------------------------------------------------------------------------------------------------

modded class SCR_BaseGameMode
{
	[RplProp()]
	protected float m_fBerGroundWetness = -1; // -1 = authority not publishing (clients fall back)

	protected const int BER_WETNESS_TICK_MS = 5000;   // integration/publish cadence
	protected const float BER_WETNESS_EPSILON = 0.01; // publish only on meaningful change

	//------------------------------------------------------------------------------------------------
	protected override void OnGameStart()
	{
		super.OnGameStart();

		// ticks everywhere; the authority gate sits inside the tick — only the master
		// ever publishes, clients receive the value through replication
		GetGame().GetCallqueue().CallLater(BerWetnessTick, BER_WETNESS_TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void BerWetnessTick()
	{
		if (!IsMaster())
			return; // clients receive the value through replication instead

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		float wet = BER_SurfaceUtil.AdvanceLocalWetness(world);
		if (Math.AbsFloat(wet - m_fBerGroundWetness) > BER_WETNESS_EPSILON)
		{
			m_fBerGroundWetness = wet;
			Replication.BumpMe();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The replicated server-side wetness, or -1 when no authority value is available
	//! (no game mode up yet, or the authority has not published).
	static float BER_GetAuthoritativeWetness()
	{
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (!gameMode)
			return -1;
		return gameMode.m_fBerGroundWetness;
	}
}
