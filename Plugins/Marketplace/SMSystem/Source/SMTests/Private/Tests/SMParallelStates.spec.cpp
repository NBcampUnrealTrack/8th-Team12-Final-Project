// Copyright Recursoft LLC. All Rights Reserved.

#include "Helpers/SMTestBoilerplate.h"
#include "Helpers/SMTestHelpers.h"
#include "SMTestContext.h"

#include "Blueprints/SMBlueprint.h"

#include "Graph/SMGraph.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#if WITH_DEV_AUTOMATION_TESTS

#if PLATFORM_DESKTOP

BEGIN_DEFINE_SPEC(FParallelStatesSpec,
                  "LogicDriver.States.Parallel",
                  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
	SETUP_SPEC_TEST_DEFINES()

	// Build a branching machine on the current graph, run it to completion, and return the live instance.
	// Callers that need the instance alive afterwards must pass bShutdown=false.
	USMInstance* BuildAndRun(int32 Rows, int32 Branches, bool bRunParallel,
		int32& OutEntry, int32& OutUpdate, int32& OutEnd,
		int32 MaxIterations = 1000, bool bShutdown = true,
		bool bLeaveActive = false, bool bReEnterStates = false, bool bEvalIfNextStateActive = true)
	{
		TArray<UEdGraphPin*> LastStatePins;
		FSMBlueprintEditorUtils::RemoveAllNodesFromGraph(StateMachineGraph, NewBP);
		TestHelpers::BuildBranchingStateMachine(this, StateMachineGraph, Rows, Branches, bRunParallel,
			&LastStatePins, bLeaveActive, bReEnterStates, bEvalIfNextStateActive);
		return TestHelpers::RunStateMachineToCompletion(this, NewBP, OutEntry, OutUpdate, OutEnd,
			MaxIterations, bShutdown);
	}
END_DEFINE_SPEC(FParallelStatesSpec)

void FParallelStatesSpec::Define()
{
	BeforeEach([this]()
	{
		SPEC_TEST_BEFORE_EACH()
		SETUP_NEW_STATE_MACHINE_FOR_SPEC_TEST_NO_STATES()
	});

	AfterEach([this]()
	{
		SPEC_TEST_AFTER_EACH()
		NewAsset.DeleteAsset(this);
	});

	// -------------------------------------------------------------------------
	Describe("Single (non-parallel) mode", [this]()
	{
		It("should follow only one branch in a 2x2 machine", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			BuildAndRun(2, 2, /*bRunParallel=*/false, Entry, Update, End);
			TestEqual("Only one branch entered", Entry, 2 /*Branches*/);
		});
	});

	// -------------------------------------------------------------------------
	Describe("All states visited", [this]()
	{
		// Regression coverage for ProcessStates head-index iteration: every state must be
		// entered and exited exactly once across representative branching topologies.

		It("should enter all states in a 2x2 parallel machine", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			const USMInstance* Instance = BuildAndRun(2, 2, true, Entry, Update, End, 1000, false);
			const int32 Expected = Instance->GetStateMap().Num() - 1;
			TestEqual("Entry count", Entry, Expected);
		});

		It("should enter all states in a 2x4 parallel machine", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			const USMInstance* Instance = BuildAndRun(2, 4, true, Entry, Update, End, 1000, false);
			const int32 Expected = Instance->GetStateMap().Num() - 1;
			TestEqual("Entry count", Entry, Expected);
		});

		It("should enter all states in a 4x2 parallel machine", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			const USMInstance* Instance = BuildAndRun(4, 2, true, Entry, Update, End, 1000, false);
			const int32 Expected = Instance->GetStateMap().Num() - 1;
			TestEqual("Entry count", Entry, Expected);
		});

		It("should enter all states in a 3x3 parallel machine", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			const USMInstance* Instance = BuildAndRun(3, 3, true, Entry, Update, End, 1000, false);
			const int32 Expected = Instance->GetStateMap().Num() - 1;
			TestEqual("Entry count", Entry, Expected);
		});

		It("should enter all states in a 1x3 parallel machine via entry-node fan-out", [this]()
		{
			// Rows=1: all states connect directly from the entry node and activate at Start()
			// via bAllowParallelEntryStates — a distinct activation path from the transition fan-out.
			int32 Entry = 0, Update = 0, End = 0;
			const USMInstance* Instance = BuildAndRun(1, 3, true, Entry, Update, End, 1000, false);
			TestEqual("Entry count", Entry, Instance->GetStateMap().Num() - 1);
		});
	});

	// -------------------------------------------------------------------------
	Describe("Destination state activation ordering", [this]()
	{
		// Destination states inserted by ProcessStates must be processed in the same call
		// they were added — not deferred to the next tick. A 2-row fan-out has one entry
		// state that transitions to N parallel leaf states; all leaves must activate within
		// the single Update() that fires the transition, so MaxIterations=2 is enough.

		It("should activate all 2 parallel leaf states within a single update", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			int32 Iterations = 0;
			TArray<UEdGraphPin*> Pins;
			FSMBlueprintEditorUtils::RemoveAllNodesFromGraph(StateMachineGraph, NewBP);
			TestHelpers::BuildBranchingStateMachine(this, StateMachineGraph, 2, 2, true, &Pins);
			TestHelpers::RunStateMachineToCompletion(this, NewBP, Entry, Update, End,
				/*MaxIterations=*/2, false, true, true, &Iterations);
			TestEqual("All leaf states activated within 1 update", Iterations, 1);
		});

		It("should activate all 4 parallel leaf states within a single update", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			int32 Iterations = 0;
			TArray<UEdGraphPin*> Pins;
			FSMBlueprintEditorUtils::RemoveAllNodesFromGraph(StateMachineGraph, NewBP);
			TestHelpers::BuildBranchingStateMachine(this, StateMachineGraph, 2, 4, true, &Pins);
			TestHelpers::RunStateMachineToCompletion(this, NewBP, Entry, Update, End,
				/*MaxIterations=*/2, false, true, true, &Iterations);
			TestEqual("All leaf states activated within 1 update", Iterations, 1);
		});
	});

	// -------------------------------------------------------------------------
	Describe("Update behavior", [this]()
	{
		It("should update each active state exactly once per tick without double-processing", [this]()
		{
			// After a parallel machine completes (non-leave-active), only the leaf end-states
			// remain active with no outgoing transitions. Each Update() must increment the
			// hit counter by exactly the number of active states — no more, no less.
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(2, 2, true, Entry, Update, End, 1000, false);

			TArray<FGuid> ActiveGuids;
			Instance->GetAllActiveStateGuids(ActiveGuids);
			const int32 ActiveCount = ActiveGuids.Num();

			const USMTestContext* Context = CastChecked<USMTestContext>(Instance->GetContext());
			const int32 UpdatesBefore = Context->TimesUpdateHit.Count;

			Instance->Update(0.f);
			TestEqual("Each active state updated exactly once", Context->TimesUpdateHit.Count, UpdatesBefore + ActiveCount);

			Instance->Update(0.f);
			TestEqual("Each active state updated exactly once again", Context->TimesUpdateHit.Count, UpdatesBefore + ActiveCount * 2);
		});
	});

	// -------------------------------------------------------------------------
	Describe("Shutdown behavior", [this]()
	{
		It("should call end on every entered state after shutdown", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			BuildAndRun(3, 3, true, Entry, Update, End); // bShutdown=true by default
			TestEqual("End count matches entry count", End, Entry);
		});
	});

	// -------------------------------------------------------------------------
	Describe("Active state tracking", [this]()
	{
		It("should report only leaf end-states as active after a 3x3 machine completes", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(3, 3, true, Entry, Update, End, 1000, false);

			TArray<FGuid> ActiveGuids;
			Instance->GetAllActiveStateGuids(ActiveGuids);

			const int32 ExpectedLeaves = FMath::RoundToInt(FMath::Pow(3.f, 3.f));
			TestEqual("Only leaf states active", ActiveGuids.Num(), ExpectedLeaves);
		});

		It("should have no active states after stop", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(3, 3, true, Entry, Update, End, 1000, false);

			Instance->Stop();
			TestEqual("No active states after stop", Instance->GetAllActiveStateGuidsCopy().Num(), 0);
		});

		It("should restore all active states correctly after shutdown and reload", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(3, 3, true, Entry, Update, End, 1000, false);

			TArray<FGuid> ActiveGuids;
			Instance->GetAllActiveStateGuids(ActiveGuids);

			Instance->Shutdown();
			Instance = TestHelpers::CreateNewStateMachineInstanceFromBP(this, NewBP, NewObject<USMTestContext>());
			Instance->LoadFromMultipleStates(ActiveGuids);

			TestEqual("Initial states restored", Instance->GetRootStateMachine().GetInitialStates().Num(), ActiveGuids.Num());

			Instance->Start();
			TestEqual("All reloaded states are active",
				TestHelpers::ArrayContentsInArray(Instance->GetAllActiveStateGuidsCopy(), ActiveGuids),
				ActiveGuids.Num());
		});
	});

	// -------------------------------------------------------------------------
	Describe("Leave states active", [this]()
	{
		// Helper that builds a 3x3 bLeaveActive machine, runs it, then calls the provided
		// validator with the live instance and the captured active guids.
		auto RunLeaveActiveAndValidate = [this](TFunction<void(USMInstance*, const TArray<FGuid>&)> Validate)
		{
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(3, 3, true, Entry, Update, End, 1000, false, /*bLeaveActive=*/true);

			TestEqual("All states entered", Entry, Instance->GetStateMap().Num() - 1);

			TArray<FGuid> ActiveGuids;
			Instance->GetAllActiveStateGuids(ActiveGuids);
			TestEqual("All states remain active", ActiveGuids.Num(), Instance->GetStateMap().Num() - 1);

			Validate(Instance, ActiveGuids);
		};

		It("should keep all states active after completion", [this, RunLeaveActiveAndValidate]()
		{
			RunLeaveActiveAndValidate([this](USMInstance* Instance, const TArray<FGuid>& ActiveGuids)
			{
				Instance->Shutdown();
				Instance = TestHelpers::CreateNewStateMachineInstanceFromBP(this, NewBP, NewObject<USMTestContext>());
				Instance->LoadFromMultipleStates(ActiveGuids);

				TestEqual("Initial states restored", Instance->GetRootStateMachine().GetInitialStates().Num(), ActiveGuids.Num());
				Instance->Start();
				TestEqual("All reloaded states active",
					TestHelpers::ArrayContentsInArray(Instance->GetAllActiveStateGuidsCopy(), ActiveGuids),
					ActiveGuids.Num());
			});
		});

		// Parameterized helper used by both SetActive variants.
		auto TestSetActive = [this, RunLeaveActiveAndValidate](bool bSetActiveNow)
		{
			RunLeaveActiveAndValidate([this, bSetActiveNow](USMInstance* Instance, const TArray<FGuid>& ActiveGuids)
			{
				Instance->Shutdown();
				Instance = TestHelpers::CreateNewStateMachineInstanceFromBP(this, NewBP, NewObject<USMTestContext>());
				Instance->LoadFromMultipleStates(ActiveGuids);
				Instance->Start();

				TArray<USMStateInstance_Base*> StateInstances;
				Instance->GetAllStateInstances(StateInstances);

				// Deactivate one state and verify.
				StateInstances[1]->SetActive(false, true, bSetActiveNow);
				TestEqual("One state deactivated",
					TestHelpers::ArrayContentsInArray(Instance->GetAllActiveStateGuidsCopy(), ActiveGuids),
					ActiveGuids.Num() - 1);
				TestFalse("State is inactive", StateInstances[1]->IsActive());

				// Re-activate that state and verify.
				StateInstances[1]->SetActive(true, true, bSetActiveNow);
				TestEqual("State reactivated",
					TestHelpers::ArrayContentsInArray(Instance->GetAllActiveStateGuidsCopy(), ActiveGuids),
					ActiveGuids.Num());
				if (bSetActiveNow)
				{
					TestTrue("State is immediately active", StateInstances[1]->IsActive());
				}
				else
				{
					TestFalse("State activation is deferred", StateInstances[1]->IsActive());
				}

				// Deactivate all non-root states and verify.
				for (USMStateInstance_Base* StateInstance : StateInstances)
				{
					if (StateInstance == Instance->GetRootStateMachineNodeInstance()) { continue; }
					StateInstance->SetActive(false, true, bSetActiveNow);
					TestFalse("State deactivated", StateInstance->IsActive());
				}
				TestEqual("All states deactivated", Instance->GetAllActiveStateGuidsCopy().Num(), 0);

				// Re-activate all non-root states and verify.
				for (USMStateInstance_Base* StateInstance : StateInstances)
				{
					if (StateInstance == Instance->GetRootStateMachineNodeInstance()) { continue; }
					StateInstance->SetActive(true, true, bSetActiveNow);
				}
				TestEqual("All states reactivated",
					TestHelpers::ArrayContentsInArray(Instance->GetAllActiveStateGuidsCopy(), ActiveGuids),
					ActiveGuids.Num());
			});
		};

		It("should support immediate SetActive toggling (bSetActiveNow=true)", [this, TestSetActive]()
		{
			TestSetActive(true);
		});

		It("should support deferred SetActive toggling (bSetActiveNow=false)", [this, TestSetActive]()
		{
			TestSetActive(false);
		});
	});

	// -------------------------------------------------------------------------
	Describe("State re-entry", [this]()
	{
		It("should re-enter a parallel state and run its update logic on the same tick", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(2, 1, true, Entry, Update, End, 1000, false,
				/*bLeaveActive=*/true, /*bReEnterStates=*/true);

			TestEqual("Initial entry count",  Entry,  Instance->GetStateMap().Num() - 1);
			TestEqual("Initial update count", Update, 1);
			TestEqual("Initial end count",    End,    0);

			const USMTestContext* Context = CastChecked<USMTestContext>(Instance->GetContext());
			TestEqual("Transition init count",     Context->TestTransitionInit.Count,     1);
			TestEqual("Transition shutdown count", Context->TestTransitionShutdown.Count, 0);

			Instance->Update(1.f);

			TestEqual("Entry count after re-entry",  Context->GetEntryInt(),          Instance->GetStateMap().Num());
			TestEqual("Update count after re-entry", Context->TimesUpdateHit.Count,   3); // Each state updates again.
			TestEqual("End count after re-entry",    Context->GetEndInt(),             0);
			TestEqual("Transition init unchanged",   Context->TestTransitionInit.Count,     1);
			TestEqual("Transition shutdown unchanged",Context->TestTransitionShutdown.Count, 0);
		});

		It("should not re-enter a parallel state when re-entry is disabled", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(2, 1, true, Entry, Update, End, 1000, false,
				/*bLeaveActive=*/true, /*bReEnterStates=*/false);

			TestEqual("Initial entry count",  Entry,  Instance->GetStateMap().Num() - 1);
			TestEqual("Initial update count", Update, 1);
			TestEqual("Initial end count",    End,    0);

			Instance->Update(1.f);

			const USMTestContext* Context = CastChecked<USMTestContext>(Instance->GetContext());
			TestEqual("Entry count unchanged after update", Context->GetEntryInt(),        Instance->GetStateMap().Num() - 1);
			TestEqual("Update count after update",          Context->TimesUpdateHit.Count, 3); // Each state updates again.
			TestEqual("End count unchanged",                Context->GetEndInt(),           0);
			TestEqual("Transition init count",     Context->TestTransitionInit.Count,     1);
			TestEqual("Transition shutdown count", Context->TestTransitionShutdown.Count, 0);
		});

		It("should re-enter all parallel leaf states when there are multiple branches", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(2, 2, true, Entry, Update, End, 1000, false,
				/*bLeaveActive=*/true, /*bReEnterStates=*/true);

			// Rows=2, Branches=2: 2 row-1 states + 4 row-2 leaf states = 6 total non-root states.
			const int32 NumNonRootStates = Instance->GetStateMap().Num() - 1; // 6
			const int32 LeafCount        = 4; // Branches^Rows = 2^2; these are the transition destinations that re-enter.
			const int32 InitTransitions  = 4; // 2 row-1 states x 2 transitions each.

			TestEqual("Initial entry count",  Entry,  NumNonRootStates);
			TestEqual("Initial update count", Update, 2); // One update per row-1 state before each transitions to its leaves.
			TestEqual("Initial end count",    End,    0);

			const USMTestContext* Context = CastChecked<USMTestContext>(Instance->GetContext());
			TestEqual("Transition init count", Context->TestTransitionInit.Count, InitTransitions);

			Instance->Update(1.f);

			// Only leaf states (transition destinations) re-enter; row-1 states (sources) do not.
			TestEqual("Entry count after re-entry",    Context->GetEntryInt(),               NumNonRootStates + LeafCount);
			TestEqual("Update count after re-entry",   Context->TimesUpdateHit.Count,        Update + NumNonRootStates); // Initial updates + all active states update once more.
			TestEqual("End count after re-entry",      Context->GetEndInt(),                 0);
			TestEqual("Transition init unchanged",     Context->TestTransitionInit.Count,    InitTransitions);
			TestEqual("Transition shutdown unchanged", Context->TestTransitionShutdown.Count, 0);
		});

		It("should not retrigger a transition when bEvalIfNextStateActive is false", [this]()
		{
			int32 Entry = 0, Update = 0, End = 0;
			USMInstance* Instance = BuildAndRun(2, 1, true, Entry, Update, End, 1000, false,
				/*bLeaveActive=*/true, /*bReEnterStates=*/true, /*bEvalIfNextStateActive=*/false);

			TestEqual("Initial entry count",  Entry,  Instance->GetStateMap().Num() - 1);
			TestEqual("Initial update count", Update, 1);
			TestEqual("Initial end count",    End,    0);

			Instance->Update(1.f);

			const USMTestContext* Context = CastChecked<USMTestContext>(Instance->GetContext());
			TestEqual("Entry count unchanged", Context->GetEntryInt(),        Instance->GetStateMap().Num() - 1);
			TestEqual("Update count",          Context->TimesUpdateHit.Count, 3);
			TestEqual("End count unchanged",   Context->GetEndInt(),           0);
			TestEqual("Transition init count",     Context->TestTransitionInit.Count,     1);
			TestEqual("Transition shutdown count", Context->TestTransitionShutdown.Count, 0);
		});
	});
}

#endif

#endif // WITH_DEV_AUTOMATION_TESTS
