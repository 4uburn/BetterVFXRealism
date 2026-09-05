//------------------------------------------------------------------------------------------------
// Better Effects Realism — TESTING ONLY, delete this file before publish
//
// Placeable spawner that detonates a frag-grenade warhead a few seconds into play mode so
// the visual-shrapnel diagnostics (DIAG_FRAG) can run without a player throwing anything.
// Works in worlds without a game mode (no deploy screen needed).
//------------------------------------------------------------------------------------------------

[EntityEditorProps(category: "GameScripted/BetterEffectsRealism", description: "TEMP: frag visual test spawner")]
class BER_FragTestSpawnerClass : ScriptComponentClass
{
}

class BER_FragTestSpawner : ScriptComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		// CallLater never ticks in edit mode, so this only ever fires in play
		GetGame().GetCallqueue().CallLater(SpawnTestWarhead, 8000, false);
		Print("BER DIAG frag: TEST spawner armed (detonation in 8 s)");
	}

	//------------------------------------------------------------------------------------------------
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
		IEntity warhead = GetGame().SpawnEntityPrefab(res, owner.GetWorld(), params);
		PrintFormat("BER DIAG frag: TEST spawned warhead=%1 at %2", warhead != null, pos);
	}
}
