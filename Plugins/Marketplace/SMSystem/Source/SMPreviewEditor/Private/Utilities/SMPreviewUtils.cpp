// Copyright Recursoft LLC. All Rights Reserved.

#include "SMPreviewUtils.h"

#include "SMPreviewObject.h"
#include "Views/Viewport/SMPreviewModeViewportClient.h"

#include "Blueprints/SMBlueprint.h"
#include "SMInstance.h"

#include "Blueprints/SMBlueprintEditor.h"
#include "Utilities/SMBlueprintEditorUtils.h"

#include "ActorMode.h"
#include "AssetCompilingManager.h"
#include "Components/ModelComponent.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ISceneOutliner.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SMPreviewUtils"

const FString FSMPreviewUtils::PreviewPackagePrefix = "LogicDriverPreviewPackage_";
const FString FSMPreviewUtils::PreviewPackageSimulationPrefix = "LogicDriverSimulationWorld_";

static FDelegateHandle OnPackagedDirtyFlagChangedHandle;
static FDelegateHandle MapChangedHandle;
static TSet<USMBlueprint*> SimulatingBlueprints;

void FSMPreviewUtils::StartSimulation(USMBlueprint* InBlueprint)
{
	check(InBlueprint);
	
	if (!ensure(!SimulatingBlueprints.Contains(InBlueprint)))
	{
		return;
	}
	FKismetEditorUtilities::CompileBlueprint(InBlueprint);

	SimulatingBlueprints.Add(InBlueprint);
	
	USMInstance* PreviewInstance = nullptr;

	if (USMPreviewObject* PreviewObject = InBlueprint->GetPreviewObject(false))
	{
		if (PreviewObject->GetStateMachineTemplate())
		{
			if (const UObject* Context = GetContextForPreview(InBlueprint))
			{
				const TWeakPtr<FSMPreviewModeViewportClient> PreviewClient = GetViewportClient(InBlueprint);
				if (PreviewClient.IsValid())
				{
					//  Clone the world.
					if (UWorld* SimulatedWorld = PreparePreviewWorld(InBlueprint))
					{
						// Best to clear the selection since the world is changing.
						PreviewClient.Pin()->ResetSelection();

						UObject* ClonedContext = GetClonedContextForPreview(Context, SimulatedWorld);
						check(ClonedContext);
						
						// Enable input for pawns if configured.
						if (PreviewObject->ShouldPossessPawnContext())
						{
							if (APawn* ClonedPawnContext = Cast<APawn>(ClonedContext))
							{
								if (APlayerController* Controller = SimulatedWorld->GetFirstPlayerController())
								{
									Controller->Possess(ClonedPawnContext);
								}
							}
						}

						PreviewInstance = PreviewObject->InitializeStateMachine(ClonedContext);
						PreviewObject->SetCurrentWorld(SimulatedWorld);
					}
				}
			}
		}

		PreviewObject->NotifySimulationStarted();
	}

	InBlueprint->SetObjectBeingDebugged(PreviewInstance);
	
	if (PreviewInstance && GEditor && GEditor->IsTimerManagerValid())
	{
		TWeakObjectPtr<USMInstance> PreviewInstanceWeakPtr(PreviewInstance);
		// It's better to wait one tick, so the viewport can update appropriately in the event a breakpoint
		// stops execution right away.
		GEditor->GetTimerManager()->SetTimerForNextTick([PreviewInstanceWeakPtr]()
		{
			if (PreviewInstanceWeakPtr.IsValid())
			{
				PreviewInstanceWeakPtr->Start();
			}
		});
	}
}

void FSMPreviewUtils::StopSimulation(USMBlueprint* InBlueprint)
{
	check(InBlueprint);
	InBlueprint->SetObjectBeingDebugged(nullptr);
	
	TWeakObjectPtr<USMBlueprint> WeakBlueprint(InBlueprint);
	auto Cleanup = [WeakBlueprint]()
	{
		if (!WeakBlueprint.IsValid())
		{
			return;
		}
		USMBlueprint* Blueprint = WeakBlueprint.Get();
		SimulatingBlueprints.Remove(Blueprint);
	
		if (USMPreviewObject* PreviewObject = Blueprint->GetPreviewObject(false))
		{
			PreviewObject->ShutdownStateMachine();
			PreviewObject->NotifySimulationEnded();
		}

		const TWeakPtr<FSMPreviewModeViewportClient> PreviewClientWeakPtr = GetViewportClient(Blueprint);
		if (const TSharedPtr<FSMPreviewModeViewportClient> PreviewClient = PreviewClientWeakPtr.Pin())
		{
			// Best to clear the selection since the world is changing.
			PreviewClient->ResetSelection();
			
			if (FSMAdvancedPreviewScene* PreviewScene = PreviewClient->GetOurPreviewScene())
			{
				check(!PreviewScene->IsTicking());
				PreviewScene->OnPostTick().Unbind();
				PreviewScene->RestoreOriginalWorld();
			}
		}

		if (USMPreviewObject* PreviewObject = Blueprint->GetPreviewObject(false))
		{
			PreviewObject->SetCurrentWorld(PreviewObject->GetPreviewWorld());
		}

		if (FSMStateMachineBlueprintEditor* BlueprintEditor = FSMBlueprintEditorUtils::GetStateMachineEditor(Blueprint))
		{
			if (!BlueprintEditor->IsShuttingDown())
			{
				// Private member standalone host will be null and crash if shutting down,
				// otherwise we want to regenerate toolbars to update the Simulate/Stop button.
				BlueprintEditor->RegenerateMenusAndToolbars();
			}
		}
	};

	bool bRunningNextTick = false;
	if (FSlateApplication::Get().InKismetDebuggingMode())
	{
		FSlateApplication::Get().LeaveDebuggingMode();
		FKismetDebugUtilities::RequestAbortingExecution();
	}
	
	const TWeakPtr<FSMPreviewModeViewportClient> PreviewClient = GetViewportClient(InBlueprint);
	if (PreviewClient.IsValid())
	{
		if (FSMAdvancedPreviewScene* OurPreviewScene = PreviewClient.Pin()->GetOurPreviewScene())
		{
			if (OurPreviewScene->IsTicking())
			{
				// If we clean up during a tick, we risk throwing exceptions during blueprint execution. This most likely
				// only occurs while debugging (in a BP break point), but as a precaution, we always check if we're ticking.
				// The simplest solution is to simply wait for the tick to complete and then cleanup.
				bRunningNextTick = true;
				OurPreviewScene->OnPostTick().BindLambda(Cleanup);
			}
		}
	}
	
	if (!bRunningNextTick)
	{
		Cleanup();
	}
}

void FSMPreviewUtils::StopAllSimulations()
{
	TArray<USMBlueprint*> BlueprintsSimulatingArr(SimulatingBlueprints.Array());
	for (USMBlueprint* Blueprint : BlueprintsSimulatingArr)
	{
		StopSimulation(Blueprint);
	}
}

UObject* FSMPreviewUtils::GetContextForPreview(USMBlueprint* Blueprint)
{
	UObject* Context = nullptr;
	
	TWeakPtr<FSMPreviewModeViewportClient> PreviewClient = GetViewportClient(Blueprint);
	if (PreviewClient.IsValid())
	{
		// Fist check if there is an actor assigned.
		Context = PreviewClient.Pin()->GetOurPreviewScene()->GetContextObjectForCurrentWorld();
	}

	if (!Context)
	{
		// Use the object model instead.
		Context = Blueprint->GetPreviewObject()->GetContextObject();
	}

	return Context;
}

UObject* FSMPreviewUtils::GetClonedContextForPreview(const UObject* Context, const UWorld* ClonedWorld)
{
	check(Context);
	
	UObject* ClonedContext = FindObjectFast<UObject>(ClonedWorld->PersistentLevel, *Context->GetName());
	if (!ClonedContext)
	{
		if (Context->GetClass()->IsChildOf<AGameModeBase>())
		{
			ClonedContext = UGameplayStatics::GetGameMode(ClonedWorld);
		}
		else if (Context->GetClass()->IsChildOf<UGameInstance>())
		{
			ClonedContext = UGameplayStatics::GetGameInstance(ClonedWorld);
		}
	}

	return ClonedContext;
}

USMPreviewObject* FSMPreviewUtils::GetPreviewObject(TWeakPtr<FSMStateMachineBlueprintEditor> BlueprintEditor)
{
	if (BlueprintEditor.IsValid())
	{
		if (USMBlueprint* Blueprint = BlueprintEditor.Pin()->GetStateMachineBlueprint())
		{
			return Blueprint->GetPreviewObject();
		}
	}

	return nullptr;
}

UWorld* FSMPreviewUtils::DuplicateWorldForSimulation(const FString& PackageName, UWorld* OwningWorld, USMBlueprint* Blueprint, FWorldContext& InWorldContext)
{
	// See DuplicateWorldForPIE
	
	// Find the original (non-PIE) level package
	UPackage* EditorLevelPackage = FindObjectFast<UPackage>(nullptr, FName(*PackageName));
	if (!EditorLevelPackage)
	{
		return nullptr;
	}
	
	// Find world object and use its PersistentLevel pointer.
	UWorld* EditorLevelWorld = OwningWorld;// UWorld::FindWorldInPackage(EditorLevelPackage);

	// If the world was not found, try to follow a redirector, if there is one
	if (!EditorLevelWorld)
	{
		EditorLevelWorld = UWorld::FollowWorldRedirectorInPackage(EditorLevelPackage);
		if (EditorLevelWorld)
		{
			EditorLevelPackage = EditorLevelWorld->GetPackage();
		}
	}

	if (!EditorLevelWorld)
	{
		return nullptr;
	}

	const FString PackageAssetPath = FPackageName::GetLongPackagePath(PackageName);
	const FString SimulationName = PreviewPackageSimulationPrefix + FGuid::NewGuid().ToString();
	const FString PrefixedLevelName = FString::Printf(TEXT("%s/%s%s"), *PackageAssetPath, *SimulationName, *EditorLevelWorld->GetName());

	const FName PrefixedLevelFName = FName(*PrefixedLevelName);
	//FSoftObjectPath::AddPIEPackageName(PrefixedLevelFName);

	UWorld::WorldTypePreLoadMap.FindOrAdd(PrefixedLevelFName) = EWorldType::PIE;
	UPackage* SimulationLevelPackage = CreatePackage(*PrefixedLevelName);
	SimulationLevelPackage->SetFlags(RF_Transient);
	SimulationLevelPackage->SetLoadedPath(EditorLevelPackage->GetLoadedPath());
	SimulationLevelPackage->SetSavedHash(EditorLevelPackage->GetSavedHash());
	SimulationLevelPackage->MarkAsFullyLoaded();
	
	FObjectDuplicationParameters Parameters(EditorLevelWorld, SimulationLevelPackage);
	Parameters.DestName = EditorLevelWorld->GetFName();
	Parameters.DestClass = EditorLevelWorld->GetClass();
	Parameters.DuplicateMode = EDuplicateMode::PIE;
	Parameters.PortFlags = PPF_DuplicateForPIE;

	// Gives a chance to any assets being used for PIE/game to complete
	FAssetCompilingManager::Get().ProcessAsyncTasks();
	
	UWorld* SimulationLevelWorld = CastChecked<UWorld>(StaticDuplicateObjectEx(Parameters));
	
	// Ensure the feature level matches the editor's, this is required as FeatureLevel is not a UPROPERTY and is not duplicated from EditorLevelWorld.
	SimulationLevelWorld->SetFeatureLevel(EditorLevelWorld->GetFeatureLevel());

	// Clean up the world type list and owning world list now that PostLoad has occurred
	UWorld::WorldTypePreLoadMap.Remove(PrefixedLevelFName);
	ULevel::StreamedLevelsOwningWorld.Remove(SimulationLevelPackage->GetFName());

	{
		ULevel* EditorLevel = EditorLevelWorld->PersistentLevel;
		ULevel* SimulationLevel = SimulationLevelWorld->PersistentLevel;

		// If editor has run construction scripts or applied level offset, we dont do it again
		SimulationLevel->bAlreadyMovedActors = EditorLevel->bAlreadyMovedActors;
		SimulationLevel->bHasRerunConstructionScripts = EditorLevel->bHasRerunConstructionScripts;

		// Fixup model components. The index buffers have been created for the components in the EditorWorld and the order
		// in which components were post-loaded matters. So don't try to guarantee a particular order here, just copy the
		// elements over.
		if (SimulationLevel->Model != nullptr
			&& SimulationLevel->Model == EditorLevel->Model
			&& SimulationLevel->ModelComponents.Num() == EditorLevel->ModelComponents.Num())
		{
			SimulationLevel->Model->ClearLocalMaterialIndexBuffersData();
			for (int32 ComponentIndex = 0; ComponentIndex < SimulationLevel->ModelComponents.Num(); ++ComponentIndex)
			{
				UModelComponent* SrcComponent = EditorLevel->ModelComponents[ComponentIndex];
				UModelComponent* DestComponent = SimulationLevel->ModelComponents[ComponentIndex];
				DestComponent->CopyElementsFrom(SrcComponent);
			}
		}
	}

	SimulationLevelWorld->ClearFlags(RF_Standalone | RF_Public | RF_Transactional); /* Transactions can result in a crash after an undo/compile. Not needed anyway. */
	SimulationLevelWorld->SetFlags(RF_Transient);
	SimulationLevelWorld->PersistentLevel->ClearFlags(RF_Transactional);
	SimulationLevelWorld->PersistentLevel->SetFlags(RF_Transient);
	//EditorLevelWorld->TransferBlueprintDebugReferences(SimulationLevelWorld);
	
	SimulationLevelWorld->AddToRoot();

	const USMPreviewObject* PreviewObject = Blueprint->GetPreviewObject();
	check(PreviewObject);

	// We need a valid world context before initializing.
	InWorldContext.SetCurrentWorld(SimulationLevelWorld);

	SimulationLevelWorld->InitWorld();

	const TSubclassOf<UGameInstance> GameInstanceClass = PreviewObject->GetGameInstance();
	
	UGameInstance* ClonedGameInstance = NewObject<UGameInstance>(GEngine /* Should be GEngine */,
		GameInstanceClass.Get() ? GameInstanceClass.Get() : UGameInstance::StaticClass());

	ClonedGameInstance->OnWorldChanged(nullptr, SimulationLevelWorld);

	SimulationLevelWorld->SetGameInstance(ClonedGameInstance);
	if (ensureAlways(SimulationLevelWorld->GetGameInstance()))
	{
		// Game mode requires instance.
		SimulationLevelWorld->SetGameMode(FURL());
	}
	
	return SimulationLevelWorld;
}

TWeakPtr<FSMPreviewModeViewportClient> FSMPreviewUtils::GetViewportClient(USMBlueprint* Blueprint)
{
	// TODO: It would be nice if the preview client wasn't stored directly on the editor, but
	// could be retrieved from the preview mode widget.
	if (FSMStateMachineBlueprintEditor* Editor = FSMBlueprintEditorUtils::GetStateMachineEditor(Blueprint))
	{
		return StaticCastSharedPtr<FSMPreviewModeViewportClient>(Editor->GetPreviewClient().Pin());
	}
	return nullptr;
}

bool FSMPreviewUtils::DoesWorldContainActor(UWorld* WorldToCheck, const AActor* CompareActor, bool bCheckName)
{
	if (ULevel* CurrentLevel = WorldToCheck->GetCurrentLevel())
	{
		if (CurrentLevel->Actors.Contains(CompareActor))
		{
			return true;
		}
		
		if (bCheckName)
		{
			return CurrentLevel->Actors.ContainsByPredicate([CompareActor](AActor* Actor)
			{
				return Actor && CompareActor && Actor->GetFName() == CompareActor->GetFName();
			});
		}
	}

	return false;
}

FString FSMPreviewUtils::MakeFullObjectPropertyName(UObject* InObject, FProperty* InProperty)
{
	check(InObject);
	check(InProperty);

	return InObject->GetName() + "_" + InProperty->GetFullName();
}

void FSMPreviewUtils::BindDelegates()
{
	UnbindDelegates();
	MapChangedHandle = FEditorDelegates::OnMapOpened.AddStatic(&FSMPreviewUtils::OnMapChanged);
	OnPackagedDirtyFlagChangedHandle = UPackage::PackageMarkedDirtyEvent.AddStatic(&FSMPreviewUtils::OnPackageDirtyFlagChanged);
}

void FSMPreviewUtils::UnbindDelegates()
{
	if (OnPackagedDirtyFlagChangedHandle.IsValid())
	{
		UPackage::PackageMarkedDirtyEvent.Remove(OnPackagedDirtyFlagChangedHandle);
	}
	if (MapChangedHandle.IsValid())
	{
		FEditorDelegates::OnMapOpened.Remove(MapChangedHandle);
	}
}

UWorld* FSMPreviewUtils::PreparePreviewWorld(USMBlueprint* Blueprint)
{
	const TWeakPtr<FSMPreviewModeViewportClient> PreviewClient = GetViewportClient(Blueprint);
	if (PreviewClient.IsValid())
	{
		PreviewClient.Pin()->GetOurPreviewScene()->CloneOriginalWorldToPreviewWorld(Blueprint);
		return PreviewClient.Pin()->GetOurPreviewScene()->GetWorld();
	}

	return nullptr;
}

void FSMPreviewUtils::OnPackageDirtyFlagChanged(UPackage* Package, bool bWasDirty)
{
	if (Package && Package->IsDirty())
	{
		const FString PackageName = Package->GetName();
		if (PackageName.Contains(FSMPreviewUtils::PreviewPackagePrefix) || PackageName.Contains(FSMPreviewUtils::PreviewPackageSimulationPrefix))
		{
			// Hack: Our packages should never be considered dirty as they do not save and can cause warnings to popup when trying to change levels if they are dirty.
			Package->ClearDirtyFlag();
		}
	}
}

void FSMPreviewUtils::OnMapChanged(const FString& MapName, bool bAsTemplate)
{
	StopAllSimulations();
}

/*
 * Preview Outliner Utils
 */

bool FSMPreviewOutlinerUtils::RefreshLevelEditorOutliner(FSMAdvancedPreviewScene* PreviewOwner)
{
	class FSMActorMode : public FActorMode
	{
	public:
		explicit FSMActorMode(const FActorModeParams& Params)
			: FActorMode(Params)
		{
		}

		// HACK: Retrieve the protected RepresentingWorld property.
		static TWeakObjectPtr<UWorld> GetWorld(FActorMode* InActorMode)
		{
			if (InActorMode == nullptr)
			{
				return nullptr;
			}
			return ((FSMActorMode*)InActorMode)->RepresentingWorld;
		}
	};
	
	const FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

	const TWeakPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetLevelEditorInstance();
	if (LevelEditor.IsValid())
	{
		TArray<TWeakPtr<ISceneOutliner>> SceneOutliners = LevelEditor.Pin()->GetAllSceneOutliners();
		for (const TWeakPtr<ISceneOutliner>& SceneOutlinerPtr : SceneOutliners)
		{
			if (SceneOutlinerPtr.IsValid())
			{
				// Find the world the main level scene outliner is running for.
				// If it is ours we need to refresh the outliner.
			
				FActorMode* ActorMode = (FActorMode*)SceneOutlinerPtr.Pin()->GetMode();
				const TWeakObjectPtr<UWorld> World = FSMActorMode::GetWorld(ActorMode);

				UWorld* RepresentingWorld = World.Get();
				if (IsValid(RepresentingWorld))
				{
					if (UPackage* Package = RepresentingWorld->GetPackage())
					{
						const FString PackageName = Package->GetName();
						if (PackageName.Contains(FSMPreviewUtils::GetPreviewPackagePrefix()) || PackageName.Contains(FSMPreviewUtils::GetPreviewSimulationPrefix()))
						{
							SceneOutlinerPtr.Pin()->FullRefresh();
							return true;
						}
					}
				}
			}
		}
	}
	
	return false;
}

bool FSMPreviewOutlinerUtils::DoesTabBelongToPreview(TSharedPtr<FTabManager> InTabManager, USMBlueprint* SMBlueprint)
{
	check(InTabManager.IsValid());
	check(SMBlueprint);
	return InTabManager->HasTabSpawner(TEXT("SMBlueprintEditorPreviewTab_DetailsView")) && SMBlueprint->GetName() == InTabManager->GetOwnerTab()->GetTabLabel().ToString();
}

#undef LOCTEXT_NAMESPACE
