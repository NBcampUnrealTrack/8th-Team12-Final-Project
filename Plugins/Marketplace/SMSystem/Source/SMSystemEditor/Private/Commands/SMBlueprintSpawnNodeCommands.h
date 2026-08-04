// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "SMUnrealTypeDefs.h"

class UEdGraph;
class UEdGraphNode;
struct FInputChord;

/**
 * Recreation of FBlueprintSpawnNodeCommands with dedicated support for spawning Logic Driver states in
 * state machine graphs.
 */
class FSMBlueprintSpawnNodeCommands : public TCommands<FSMBlueprintSpawnNodeCommands>
{
public:

	FSMBlueprintSpawnNodeCommands()
		: TCommands<FSMBlueprintSpawnNodeCommands>(TEXT("LogicDriverStateMachineEditorSpawnNodes"), NSLOCTEXT("Contexts", "StateMachineBlueprintEditor_SpawnNodes", "Blueprint Editor - Spawn Nodes by chord"), NAME_None, FSMUnrealAppStyle::GetAppStyleSetName())
	{
	}	

	virtual void RegisterCommands() override;

	/**
	 * Returns a graph action assigned to the passed in chord
	 *
	 * @param InChord					The chord to use for lookup
	 * @param InDestGraph				The graph to create the graph action for, used for validation purposes and to link any important node data to the graph
	 * @param InOutDestPosition			Position to start placing nodes, will be updated to be at the next safe position for node placement
	 * @param OutNodes					All nodes spawned by this operation
	 */
	void GetGraphActionByChord(const FInputChord& InChord, UEdGraph* InDestGraph, FVector2f& InOutDestPosition, TArray<UEdGraphNode*>& OutNodes) const;

private:
	/** An array of all the possible commands for spawning nodes */
	TArray<TSharedPtr<class FSMStateNodeSpawnInfo>> NodeCommands;
};
