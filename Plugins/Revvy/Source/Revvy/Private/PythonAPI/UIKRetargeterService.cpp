// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UIKRetargeterService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyIKRetarget, Log, All);

#include "Editor.h"
#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Animation / Skeleton
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"

// IK Rig (runtime)
#include "Rig/IKRigDefinition.h"

// IK Retargeter (runtime)
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetSettings.h"    // ERetargetSourceOrTarget
#include "Retargeter/IKRetargetChainMapping.h" // EAutoMapChainType

#if WITH_EDITOR
#include "RigEditor/IKRigController.h"
#include "RigEditor/IKRigDefinitionFactory.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "AssetRegistry/AssetData.h"
#endif

// =================================================================
// Helpers (file-local)
// =================================================================

namespace RevvyIKRetargetHelpers
{
	static bool SplitAssetPath(const FString& InFullPath, FString& OutFolder, FString& OutAssetName)
	{
		if (InFullPath.IsEmpty())
		{
			return false;
		}

		FString Working = InFullPath;
		int32 DotIdx = INDEX_NONE;
		if (Working.FindChar('.', DotIdx))
		{
			Working = Working.Left(DotIdx);
		}

		int32 SlashIdx = INDEX_NONE;
		if (!Working.FindLastChar('/', SlashIdx))
		{
			return false;
		}

		OutFolder = Working.Left(SlashIdx);
		OutAssetName = Working.Mid(SlashIdx + 1);
		return !OutFolder.IsEmpty() && !OutAssetName.IsEmpty();
	}

	template <typename T>
	static T* LoadAssetTyped(const FString& AssetPath)
	{
		if (AssetPath.IsEmpty())
		{
			return nullptr;
		}
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
		return Cast<T>(Loaded);
	}

	static void SaveIfLoaded(const FString& AssetPath)
	{
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			UEditorAssetLibrary::SaveAsset(AssetPath, /*bOnlyIfIsDirty=*/false);
		}
	}
}

// =================================================================
// IK Rig Definition Authoring
// =================================================================

FString UIKRetargeterService::CreateIKRigDefinition(const FString& SkeletonAssetPath, const FString& OutputAssetPath)
{
#if WITH_EDITOR
	if (SkeletonAssetPath.IsEmpty() || OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRigDefinition: empty input path"));
		return FString();
	}

	USkeleton* Skeleton = RevvyIKRetargetHelpers::LoadAssetTyped<USkeleton>(SkeletonAssetPath);
	if (!Skeleton)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRigDefinition: could not load USkeleton '%s'"), *SkeletonAssetPath);
		return FString();
	}

	if (UEditorAssetLibrary::DoesAssetExist(OutputAssetPath))
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRigDefinition: asset already exists '%s'"), *OutputAssetPath);
		return FString();
	}

	FString Folder, AssetName;
	if (!RevvyIKRetargetHelpers::SplitAssetPath(OutputAssetPath, Folder, AssetName))
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRigDefinition: bad output path '%s'"), *OutputAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("IKRetargeterService", "CreateIKRigDefinition", "Create IK Rig Definition"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UIKRigDefinitionFactory* Factory = NewObject<UIKRigDefinitionFactory>();

	UIKRigDefinition* NewRig = Cast<UIKRigDefinition>(
		AssetTools.CreateAsset(AssetName, Folder, UIKRigDefinition::StaticClass(), Factory));

	if (!NewRig)
	{
		UE_LOG(LogRevvyIKRetarget, Error, TEXT("CreateIKRigDefinition: CreateAsset failed for '%s'"), *OutputAssetPath);
		return FString();
	}

	NewRig->Modify();

	UIKRigController* Ctrl = UIKRigController::GetController(NewRig);
	if (Ctrl)
	{
		// UIKRigController seeds FIKRigSkeleton from the mesh's ref skeleton.
		// In 5.8 the public entry point is SetSkeletalMesh; SetSkeleton is not the primary API.
		USkeletalMesh* PreviewMesh = Skeleton->GetPreviewMesh(true);
		if (PreviewMesh)
		{
			Ctrl->SetSkeletalMesh(PreviewMesh);
		}
		else
		{
			UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRigDefinition: USkeleton '%s' has no preview mesh; IK rig created but skeleton not seeded"), *SkeletonAssetPath);
		}
	}

	NewRig->MarkPackageDirty();
	RevvyIKRetargetHelpers::SaveIfLoaded(OutputAssetPath);

	UE_LOG(LogRevvyIKRetarget, Log, TEXT("CreateIKRigDefinition: created '%s'"), *OutputAssetPath);
	return OutputAssetPath;
#else
	return FString();
#endif
}

bool UIKRetargeterService::AddRetargetChain(const FString& RigAssetPath, const FString& ChainName, const FString& StartBone, const FString& EndBone, const FString& GoalBoneOptional)
{
#if WITH_EDITOR
	if (RigAssetPath.IsEmpty() || ChainName.IsEmpty() || StartBone.IsEmpty() || EndBone.IsEmpty())
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("AddRetargetChain: missing required argument"));
		return false;
	}

	UIKRigDefinition* Rig = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRigDefinition>(RigAssetPath);
	if (!Rig)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("AddRetargetChain: could not load UIKRigDefinition '%s'"), *RigAssetPath);
		return false;
	}

	UIKRigController* Ctrl = UIKRigController::GetController(Rig);
	if (!Ctrl)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("AddRetargetChain: no controller for '%s'"), *RigAssetPath);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("IKRetargeterService", "AddRetargetChain", "Add IK Retarget Chain"));
	Rig->Modify();

	const FName Goal = GoalBoneOptional.IsEmpty() ? NAME_None : FName(*GoalBoneOptional);
	const FName Added = Ctrl->AddRetargetChain(FName(*ChainName), FName(*StartBone), FName(*EndBone), Goal);

	if (Added.IsNone())
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("AddRetargetChain: AddRetargetChain returned None for '%s'"), *ChainName);
		return false;
	}

	Rig->MarkPackageDirty();
	RevvyIKRetargetHelpers::SaveIfLoaded(RigAssetPath);

	UE_LOG(LogRevvyIKRetarget, Log, TEXT("AddRetargetChain: added '%s' on '%s'"), *Added.ToString(), *RigAssetPath);
	return true;
#else
	return false;
#endif
}

// =================================================================
// IK Retargeter Authoring
// =================================================================

FString UIKRetargeterService::CreateIKRetargeter(const FString& SourceRigAssetPath, const FString& TargetRigAssetPath, const FString& OutputAssetPath)
{
#if WITH_EDITOR
	if (SourceRigAssetPath.IsEmpty() || TargetRigAssetPath.IsEmpty() || OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRetargeter: empty input path"));
		return FString();
	}

	UIKRigDefinition* SourceRig = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRigDefinition>(SourceRigAssetPath);
	UIKRigDefinition* TargetRig = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRigDefinition>(TargetRigAssetPath);
	if (!SourceRig || !TargetRig)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRetargeter: could not load one or both IK rigs (src='%s', tgt='%s')"), *SourceRigAssetPath, *TargetRigAssetPath);
		return FString();
	}

	if (UEditorAssetLibrary::DoesAssetExist(OutputAssetPath))
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRetargeter: asset already exists '%s'"), *OutputAssetPath);
		return FString();
	}

	FString Folder, AssetName;
	if (!RevvyIKRetargetHelpers::SplitAssetPath(OutputAssetPath, Folder, AssetName))
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("CreateIKRetargeter: bad output path '%s'"), *OutputAssetPath);
		return FString();
	}

	FScopedTransaction Tx(NSLOCTEXT("IKRetargeterService", "CreateIKRetargeter", "Create IK Retargeter"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UIKRetargetFactory* Factory = NewObject<UIKRetargetFactory>();

	UIKRetargeter* NewRetargeter = Cast<UIKRetargeter>(
		AssetTools.CreateAsset(AssetName, Folder, UIKRetargeter::StaticClass(), Factory));

	if (!NewRetargeter)
	{
		UE_LOG(LogRevvyIKRetarget, Error, TEXT("CreateIKRetargeter: CreateAsset failed for '%s'"), *OutputAssetPath);
		return FString();
	}

	NewRetargeter->Modify();

	UIKRetargeterController* RC = UIKRetargeterController::GetController(NewRetargeter);
	if (!RC)
	{
		UE_LOG(LogRevvyIKRetarget, Error, TEXT("CreateIKRetargeter: no controller for '%s'"), *OutputAssetPath);
		return FString();
	}

	RC->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
	RC->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);

	// 5.8 requires ops in the stack before chain mapping can meaningfully exist.
	RC->AddDefaultOps();

	NewRetargeter->MarkPackageDirty();
	RevvyIKRetargetHelpers::SaveIfLoaded(OutputAssetPath);

	UE_LOG(LogRevvyIKRetarget, Log, TEXT("CreateIKRetargeter: created '%s' (src='%s', tgt='%s')"), *OutputAssetPath, *SourceRigAssetPath, *TargetRigAssetPath);
	return OutputAssetPath;
#else
	return FString();
#endif
}

bool UIKRetargeterService::SetSourceIKRig(const FString& RetargeterAssetPath, const FString& SourceRigAssetPath)
{
#if WITH_EDITOR
	UIKRetargeter* Retargeter = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRetargeter>(RetargeterAssetPath);
	UIKRigDefinition* SourceRig = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRigDefinition>(SourceRigAssetPath);
	if (!Retargeter || !SourceRig)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("SetSourceIKRig: load failure ('%s', '%s')"), *RetargeterAssetPath, *SourceRigAssetPath);
		return false;
	}

	UIKRetargeterController* RC = UIKRetargeterController::GetController(Retargeter);
	if (!RC)
	{
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("IKRetargeterService", "SetSourceIKRig", "Set Source IK Rig"));
	Retargeter->Modify();

	RC->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);

	Retargeter->MarkPackageDirty();
	RevvyIKRetargetHelpers::SaveIfLoaded(RetargeterAssetPath);
	return true;
#else
	return false;
#endif
}

bool UIKRetargeterService::SetTargetIKRig(const FString& RetargeterAssetPath, const FString& TargetRigAssetPath)
{
#if WITH_EDITOR
	UIKRetargeter* Retargeter = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRetargeter>(RetargeterAssetPath);
	UIKRigDefinition* TargetRig = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRigDefinition>(TargetRigAssetPath);
	if (!Retargeter || !TargetRig)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("SetTargetIKRig: load failure ('%s', '%s')"), *RetargeterAssetPath, *TargetRigAssetPath);
		return false;
	}

	UIKRetargeterController* RC = UIKRetargeterController::GetController(Retargeter);
	if (!RC)
	{
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("IKRetargeterService", "SetTargetIKRig", "Set Target IK Rig"));
	Retargeter->Modify();

	RC->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);

	Retargeter->MarkPackageDirty();
	RevvyIKRetargetHelpers::SaveIfLoaded(RetargeterAssetPath);
	return true;
#else
	return false;
#endif
}

// =================================================================
// Chain Mapping
// =================================================================

FString UIKRetargeterService::ListChainMappings(const FString& RetargeterAssetPath)
{
#if WITH_EDITOR
	UIKRetargeter* Retargeter = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRetargeter>(RetargeterAssetPath);
	if (!Retargeter)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("ListChainMappings: could not load '%s'"), *RetargeterAssetPath);
		return TEXT("[]");
	}

	UIKRetargeterController* RC = UIKRetargeterController::GetController(Retargeter);
	if (!RC)
	{
		return TEXT("[]");
	}

	// Enumerate target chain names from the target IK rig and read back the mapped source.
	TArray<TSharedPtr<FJsonValue>> Items;

	const UIKRigDefinition* TargetRig = RC->GetIKRig(ERetargetSourceOrTarget::Target);
	if (TargetRig)
	{
		const UIKRigController* TargetCtrl = UIKRigController::GetController(const_cast<UIKRigDefinition*>(TargetRig));
		if (TargetCtrl)
		{
			// 5.8: GetRetargetChainNames(TArray<FName>&) was removed. The controller
			// now exposes GetRetargetChains() -> const TArray<FBoneChain>&; pull each
			// chain's ChainName (defined in Rig/IKRigDefinition.h, already included).
			TArray<FName> TargetChainNames;
			for (const FBoneChain& TargetChain : TargetCtrl->GetRetargetChains())
			{
				TargetChainNames.Add(TargetChain.ChainName);
			}

			for (const FName& TargetChainName : TargetChainNames)
			{
				const FName SourceChain = RC->GetSourceChain(TargetChainName);

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("source_chain"), SourceChain.IsNone() ? FString() : SourceChain.ToString());
				Entry->SetStringField(TEXT("target_chain"), TargetChainName.ToString());
				Items.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
	}

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Items, Writer);
	return Out;
#else
	return TEXT("[]");
#endif
}

bool UIKRetargeterService::SetChainMapping(const FString& RetargeterAssetPath, const FString& SourceChainName, const FString& TargetChainName)
{
#if WITH_EDITOR
	if (RetargeterAssetPath.IsEmpty() || TargetChainName.IsEmpty())
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("SetChainMapping: missing required argument"));
		return false;
	}

	UIKRetargeter* Retargeter = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRetargeter>(RetargeterAssetPath);
	if (!Retargeter)
	{
		return false;
	}

	UIKRetargeterController* RC = UIKRetargeterController::GetController(Retargeter);
	if (!RC)
	{
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("IKRetargeterService", "SetChainMapping", "Set IK Retarget Chain Mapping"));
	Retargeter->Modify();

	// UIKRetargeterController::SetSourceChain(InSourceChainName, InTargetChainName, InOpName=NAME_None)
	const FName Src = SourceChainName.IsEmpty() ? NAME_None : FName(*SourceChainName);
	const FName Tgt = FName(*TargetChainName);
	RC->SetSourceChain(Src, Tgt);

	Retargeter->MarkPackageDirty();
	RevvyIKRetargetHelpers::SaveIfLoaded(RetargeterAssetPath);
	return true;
#else
	return false;
#endif
}

bool UIKRetargeterService::AutoMapChains(const FString& RetargeterAssetPath, const FString& StrategyStr)
{
#if WITH_EDITOR
	UIKRetargeter* Retargeter = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRetargeter>(RetargeterAssetPath);
	if (!Retargeter)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("AutoMapChains: could not load '%s'"), *RetargeterAssetPath);
		return false;
	}

	UIKRetargeterController* RC = UIKRetargeterController::GetController(Retargeter);
	if (!RC)
	{
		return false;
	}

	EAutoMapChainType Strategy = EAutoMapChainType::Fuzzy;
	{
		const FString S = StrategyStr.ToLower();
		if (S == TEXT("exact"))
		{
			Strategy = EAutoMapChainType::Exact;
		}
		else if (S == TEXT("clear"))
		{
			Strategy = EAutoMapChainType::Clear;
		}
		else
		{
			Strategy = EAutoMapChainType::Fuzzy;
		}
	}

	FScopedTransaction Tx(NSLOCTEXT("IKRetargeterService", "AutoMapChains", "Auto-Map IK Retarget Chains"));
	Retargeter->Modify();

	RC->AutoMapChains(Strategy, /*bForceRemap=*/true);

	Retargeter->MarkPackageDirty();
	RevvyIKRetargetHelpers::SaveIfLoaded(RetargeterAssetPath);
	return true;
#else
	return false;
#endif
}

// =================================================================
// Retarget Execution
// =================================================================

FString UIKRetargeterService::RunRetargetOnSequence(const FString& RetargeterAssetPath, const FString& SourceAnimAssetPath, const FString& OutputAssetPath)
{
#if WITH_EDITOR
	if (RetargeterAssetPath.IsEmpty() || SourceAnimAssetPath.IsEmpty() || OutputAssetPath.IsEmpty())
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("RunRetargetOnSequence: missing argument"));
		return FString();
	}

	UIKRetargeter* Retargeter = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRetargeter>(RetargeterAssetPath);
	UAnimSequence* SourceAnim = RevvyIKRetargetHelpers::LoadAssetTyped<UAnimSequence>(SourceAnimAssetPath);
	if (!Retargeter || !SourceAnim)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("RunRetargetOnSequence: load failure"));
		return FString();
	}

	UIKRetargeterController* RC = UIKRetargeterController::GetController(Retargeter);
	if (!RC)
	{
		return FString();
	}

	const UIKRigDefinition* SrcRig = RC->GetIKRig(ERetargetSourceOrTarget::Source);
	const UIKRigDefinition* TgtRig = RC->GetIKRig(ERetargetSourceOrTarget::Target);
	if (!SrcRig || !TgtRig)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("RunRetargetOnSequence: retargeter '%s' has no source or target IK rig set"), *RetargeterAssetPath);
		return FString();
	}

	USkeletalMesh* SrcMesh = SrcRig->GetPreviewMesh();
	USkeletalMesh* TgtMesh = TgtRig->GetPreviewMesh();
	if (!SrcMesh || !TgtMesh)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("RunRetargetOnSequence: source/target IK rig lacks preview mesh; cannot batch-retarget"));
		return FString();
	}

	FString OutputFolder, OutputName;
	if (!RevvyIKRetargetHelpers::SplitAssetPath(OutputAssetPath, OutputFolder, OutputName))
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("RunRetargetOnSequence: bad output path '%s'"), *OutputAssetPath);
		return FString();
	}

	// Compute a search/replace that renames source asset short name -> OutputName
	const FString SrcShortName = FPaths::GetBaseFilename(SourceAnimAssetPath);

	FIKRetargetBatchOperationInputs Inputs;
	Inputs.SourceMesh              = SrcMesh;
	Inputs.TargetMesh              = TgtMesh;
	Inputs.IKRetargetAsset         = Retargeter;
	Inputs.TargetPath              = OutputFolder;
	Inputs.Prefix                  = TEXT("");
	Inputs.Suffix                  = TEXT("");
	Inputs.Search                  = SrcShortName;
	Inputs.Replace                 = OutputName;
	Inputs.bUseSourcePath          = false;
	Inputs.bIncludeReferencedAssets= false;
	Inputs.bOverwriteExistingFiles = false;
	Inputs.bRetainAdditiveFlags    = true;

	FAssetData SrcData(SourceAnim);
	Inputs.AssetsToRetarget.Add(SrcData);

	TArray<FAssetData> NewAssets = UIKRetargetBatchOperation::RunBatchRetarget(Inputs);
	if (NewAssets.Num() == 0)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("RunRetargetOnSequence: RunBatchRetarget produced no assets"));
		return FString();
	}

	const FString CreatedPath = NewAssets[0].GetObjectPathString();
	UE_LOG(LogRevvyIKRetarget, Log, TEXT("RunRetargetOnSequence: created '%s'"), *CreatedPath);
	return CreatedPath;
#else
	return FString();
#endif
}

TArray<FString> UIKRetargeterService::BatchRetargetSequences(const FString& RetargeterAssetPath, const TArray<FString>& SourceAnimPaths, const FString& OutputFolder, const FString& OutputSuffix)
{
	TArray<FString> Results;

#if WITH_EDITOR
	if (RetargeterAssetPath.IsEmpty() || SourceAnimPaths.Num() == 0 || OutputFolder.IsEmpty())
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("BatchRetargetSequences: missing argument"));
		return Results;
	}

	UIKRetargeter* Retargeter = RevvyIKRetargetHelpers::LoadAssetTyped<UIKRetargeter>(RetargeterAssetPath);
	if (!Retargeter)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("BatchRetargetSequences: could not load '%s'"), *RetargeterAssetPath);
		return Results;
	}

	UIKRetargeterController* RC = UIKRetargeterController::GetController(Retargeter);
	if (!RC)
	{
		return Results;
	}

	const UIKRigDefinition* SrcRig = RC->GetIKRig(ERetargetSourceOrTarget::Source);
	const UIKRigDefinition* TgtRig = RC->GetIKRig(ERetargetSourceOrTarget::Target);
	if (!SrcRig || !TgtRig)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("BatchRetargetSequences: retargeter '%s' has no source or target IK rig set"), *RetargeterAssetPath);
		return Results;
	}

	USkeletalMesh* SrcMesh = SrcRig->GetPreviewMesh();
	USkeletalMesh* TgtMesh = TgtRig->GetPreviewMesh();
	if (!SrcMesh || !TgtMesh)
	{
		UE_LOG(LogRevvyIKRetarget, Warning, TEXT("BatchRetargetSequences: source/target IK rig lacks preview mesh"));
		return Results;
	}

	TArray<FAssetData> SourceAssets;
	SourceAssets.Reserve(SourceAnimPaths.Num());
	for (const FString& SrcPath : SourceAnimPaths)
	{
		UAnimSequence* Anim = RevvyIKRetargetHelpers::LoadAssetTyped<UAnimSequence>(SrcPath);
		if (Anim)
		{
			SourceAssets.Add(FAssetData(Anim));
		}
		else
		{
			UE_LOG(LogRevvyIKRetarget, Warning, TEXT("BatchRetargetSequences: skipping unloadable anim '%s'"), *SrcPath);
		}
	}

	if (SourceAssets.Num() == 0)
	{
		return Results;
	}

	FIKRetargetBatchOperationInputs Inputs;
	Inputs.SourceMesh              = SrcMesh;
	Inputs.TargetMesh              = TgtMesh;
	Inputs.IKRetargetAsset         = Retargeter;
	Inputs.AssetsToRetarget        = SourceAssets;
	Inputs.TargetPath              = OutputFolder;
	Inputs.Prefix                  = TEXT("");
	Inputs.Suffix                  = OutputSuffix;
	Inputs.Search                  = TEXT("");
	Inputs.Replace                 = TEXT("");
	Inputs.bUseSourcePath          = false;
	Inputs.bIncludeReferencedAssets= true;
	Inputs.bOverwriteExistingFiles = false;
	Inputs.bRetainAdditiveFlags    = true;

	TArray<FAssetData> NewAssets = UIKRetargetBatchOperation::RunBatchRetarget(Inputs);

	Results.Reserve(NewAssets.Num());
	for (const FAssetData& AD : NewAssets)
	{
		Results.Add(AD.GetObjectPathString());
	}

	UE_LOG(LogRevvyIKRetarget, Log, TEXT("BatchRetargetSequences: retargeted %d source anims into '%s' (produced %d assets)"), SourceAssets.Num(), *OutputFolder, Results.Num());
#endif

	return Results;
}
