// Better VFX Realism: opt-in review fixture. Place in a test world and enable m_bEnabled.
// One delayed detonation per instance; deleting it cancels the pending callback.

[EntityEditorProps(category: "GameScripted/BetterVFXRealism", description: "Review: opt-in fragment visual test spawner")]
class BER_FragTestSpawnerClass : ScriptComponentClass
{
}

class BER_FragTestSpawner : ScriptComponent
{
	[Attribute("0", desc: "Enable one test detonation after entering play mode")]
	protected bool m_bEnabled;
	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
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
			GetGame().GetCallqueue().Remove(SpawnTestWarhead);
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
		IEntity warhead = GetGame().SpawnEntityPrefab(res, owner.GetWorld(), params);
		PrintFormat("BER DIAG frag: TEST spawned warhead=%1 at %2", warhead != null, pos);
	}
}
