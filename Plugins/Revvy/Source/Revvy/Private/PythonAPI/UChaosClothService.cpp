// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UChaosClothService.h"

DEFINE_LOG_CATEGORY_STATIC(LogRevvyCloth, Log, All);

// Editor / transaction / asset infrastructure
#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"
#include "UObject/Package.h"

// Skeletal mesh + cloth asset core types
#include "Engine/SkeletalMesh.h"
#include "ClothingAsset.h"                       // UClothingAssetCommon
#include "ClothingAssetBase.h"                   // UClothingAssetBase
#include "ChaosCloth/ChaosClothConfig.h"         // UChaosClothConfig, FChaosClothWeightedValue, EClothMassMode

// JSON serialization
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// Helpers
// ============================================================================

namespace
{
	USkeletalMesh* ChaosClothLoadSkelMeshChecked(const FString& Path, const TCHAR* CallSite)
	{
		if (Path.IsEmpty())
		{
			UE_LOG(LogRevvyCloth, Warning, TEXT("%s: SkelMeshAssetPath is empty"), CallSite);
			return nullptr;
		}
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(Path));
		if (!SkelMesh)
		{
			UE_LOG(LogRevvyCloth, Warning,
				TEXT("%s: failed to load USkeletalMesh at '%s'"), CallSite, *Path);
		}
		return SkelMesh;
	}

	UClothingAssetCommon* LoadClothAssetChecked(const FString& Path, const TCHAR* CallSite)
	{
		if (Path.IsEmpty())
		{
			UE_LOG(LogRevvyCloth, Warning, TEXT("%s: ClothAssetPath is empty"), CallSite);
			return nullptr;
		}
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(Path);
		if (!Loaded)
		{
			UE_LOG(LogRevvyCloth, Warning,
				TEXT("%s: LoadAsset returned null for '%s'"), CallSite, *Path);
			return nullptr;
		}
		UClothingAssetCommon* Cloth = Cast<UClothingAssetCommon>(Loaded);
		if (!Cloth)
		{
			UE_LOG(LogRevvyCloth, Warning,
				TEXT("%s: asset '%s' (%s) is not a UClothingAssetCommon"),
				CallSite, *Path, *Loaded->GetClass()->GetName());
		}
		return Cloth;
	}

	bool SaveLoadedAssetSafe(UObject* Asset)
	{
		if (!Asset) return false;
		Asset->MarkPackageDirty();
		return UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty=*/false);
	}

	const TCHAR* MassModeToString(EClothMassMode M)
	{
		switch (M)
		{
			case EClothMassMode::UniformMass: return TEXT("UniformMass");
			case EClothMassMode::TotalMass:   return TEXT("TotalMass");
			case EClothMassMode::Density:     return TEXT("Density");
			default:                          return TEXT("Unknown");
		}
	}

	FString ChaosClothSerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer
			= TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	FString ChaosClothSerializeJsonArray(const TArray<TSharedPtr<FJsonValue>>& Arr)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer
			= TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Arr, Writer);
		return Out;
	}
}

// ============================================================================
// Query
// ============================================================================

FString UChaosClothService::ListSkelMeshClothingAssets(const FString& SkelMeshAssetPath)
{
	TArray<TSharedPtr<FJsonValue>> Items;

	USkeletalMesh* SkelMesh = ChaosClothLoadSkelMeshChecked(SkelMeshAssetPath, TEXT("ListSkelMeshClothingAssets"));
	if (!SkelMesh)
	{
		return ChaosClothSerializeJsonArray(Items);
	}

	const TArray<UClothingAssetBase*>& Assets = SkelMesh->GetMeshClothingAssets();
	for (const UClothingAssetBase* Base : Assets)
	{
		if (!Base) continue;

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"),  Base->GetName());
		Item->SetStringField(TEXT("class"), Base->GetClass()->GetName());
		Item->SetStringField(TEXT("path"),  Base->GetPathName());

		if (const UClothingAssetCommon* Cloth = Cast<UClothingAssetCommon>(Base))
		{
			Item->SetNumberField(TEXT("num_lods"),       Cloth->GetNumLods());
			Item->SetNumberField(TEXT("reference_bone"), Cloth->ReferenceBoneIndex);
			Item->SetNumberField(TEXT("used_bones"),     Cloth->UsedBoneNames.Num());
		}
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}

	UE_LOG(LogRevvyCloth, Log,
		TEXT("ListSkelMeshClothingAssets: '%s' -> %d asset(s)"),
		*SkelMeshAssetPath, Items.Num());

	return ChaosClothSerializeJsonArray(Items);
}

FString UChaosClothService::GetClothConfigInfo(const FString& ClothAssetPath)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	UClothingAssetCommon* Cloth = LoadClothAssetChecked(ClothAssetPath, TEXT("GetClothConfigInfo"));
	if (!Cloth)
	{
		return ChaosClothSerializeJson(Root);
	}

	Root->SetStringField(TEXT("path"),          Cloth->GetPathName());
	Root->SetStringField(TEXT("class"),         Cloth->GetClass()->GetName());
	Root->SetNumberField(TEXT("num_configs"),   Cloth->ClothConfigs.Num());

	TArray<TSharedPtr<FJsonValue>> ConfigNames;
	for (const TPair<FName, TObjectPtr<UClothConfigBase>>& Pair : Cloth->ClothConfigs)
	{
		ConfigNames.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
	}
	Root->SetArrayField(TEXT("config_class_names"), ConfigNames);

	if (const UChaosClothConfig* Chaos = Cloth->GetClothConfig<UChaosClothConfig>())
	{
		TSharedRef<FJsonObject> ChaosBlock = MakeShared<FJsonObject>();

		// Mass
		ChaosBlock->SetStringField(TEXT("mass_mode"),   MassModeToString(Chaos->MassMode));
		ChaosBlock->SetNumberField(TEXT("total_mass"),  Chaos->TotalMass);
		ChaosBlock->SetNumberField(TEXT("uniform_mass"),Chaos->UniformMass);
		ChaosBlock->SetNumberField(TEXT("density"),     Chaos->Density);
		ChaosBlock->SetNumberField(TEXT("mass_value"),  Chaos->GetMassValue());

		// Material stiffness (weighted values collapse Low/High)
		ChaosBlock->SetNumberField(TEXT("edge_stiffness_low"),     Chaos->EdgeStiffnessWeighted.Low);
		ChaosBlock->SetNumberField(TEXT("edge_stiffness_high"),    Chaos->EdgeStiffnessWeighted.High);
		ChaosBlock->SetNumberField(TEXT("bending_stiffness_low"),  Chaos->BendingStiffnessWeighted.Low);
		ChaosBlock->SetNumberField(TEXT("bending_stiffness_high"), Chaos->BendingStiffnessWeighted.High);
		ChaosBlock->SetNumberField(TEXT("area_stiffness_low"),     Chaos->AreaStiffnessWeighted.Low);
		ChaosBlock->SetNumberField(TEXT("area_stiffness_high"),    Chaos->AreaStiffnessWeighted.High);

		// Collision
		ChaosBlock->SetNumberField(TEXT("collision_thickness"),  Chaos->CollisionThickness);
		ChaosBlock->SetNumberField(TEXT("friction_coefficient"), Chaos->FrictionCoefficient);
		ChaosBlock->SetBoolField  (TEXT("use_ccd"),              Chaos->bUseCCD);
		ChaosBlock->SetBoolField  (TEXT("use_self_collisions"),  Chaos->bUseSelfCollisions);
		ChaosBlock->SetNumberField(TEXT("self_collision_thickness"), Chaos->SelfCollisionThickness);

		// Environmental
		ChaosBlock->SetNumberField(TEXT("damping_coefficient"),       Chaos->DampingCoefficient);
		ChaosBlock->SetNumberField(TEXT("local_damping_coefficient"), Chaos->LocalDampingCoefficient);
		ChaosBlock->SetNumberField(TEXT("gravity_scale"),             Chaos->GravityScale);
		ChaosBlock->SetNumberField(TEXT("drag_low"),                  Chaos->Drag.Low);
		ChaosBlock->SetNumberField(TEXT("drag_high"),                 Chaos->Drag.High);
		ChaosBlock->SetNumberField(TEXT("lift_low"),                  Chaos->Lift.Low);
		ChaosBlock->SetNumberField(TEXT("lift_high"),                 Chaos->Lift.High);

		Root->SetObjectField(TEXT("chaos_config"), ChaosBlock);
	}
	else
	{
		UE_LOG(LogRevvyCloth, Log,
			TEXT("GetClothConfigInfo: '%s' has no UChaosClothConfig"), *ClothAssetPath);
		Root->SetObjectField(TEXT("chaos_config"), MakeShared<FJsonObject>());
	}

	return ChaosClothSerializeJson(Root);
}

// ============================================================================
// Assignment / Binding
// ============================================================================

bool UChaosClothService::AddClothingSimulationToSection(
	const FString& SkelMeshAssetPath,
	int32 LODIndex,
	int32 SectionIndex,
	const FString& ClothAssetPath)
{
#if WITH_EDITOR
	USkeletalMesh* SkelMesh  = ChaosClothLoadSkelMeshChecked(SkelMeshAssetPath, TEXT("AddClothingSimulationToSection"));
	UClothingAssetCommon* Cloth = LoadClothAssetChecked(ClothAssetPath, TEXT("AddClothingSimulationToSection"));
	if (!SkelMesh || !Cloth)
	{
		return false;
	}

	if (LODIndex < 0 || SectionIndex < 0)
	{
		UE_LOG(LogRevvyCloth, Warning,
			TEXT("AddClothingSimulationToSection: invalid LOD %d / Section %d"), LODIndex, SectionIndex);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("ChaosClothService", "AddClothingSimToSection", "Add Clothing Simulation To Section"));

	SkelMesh->Modify();
	Cloth->Modify();

	// Register the cloth asset with the skeletal mesh (dedup handled internally).
	const TArray<UClothingAssetBase*>& Existing = SkelMesh->GetMeshClothingAssets();
	bool bAlreadyRegistered = false;
	for (const UClothingAssetBase* Base : Existing)
	{
		if (Base == Cloth) { bAlreadyRegistered = true; break; }
	}
	if (!bAlreadyRegistered)
	{
		SkelMesh->AddClothingAsset(Cloth);
	}

	// Bind the cloth asset to the specified render section.
	// AssetLodIndex defaults to 0 (first cloth LOD); binding hooks up sim data.
	const bool bBindOk = Cloth->BindToSkeletalMesh(SkelMesh, LODIndex, SectionIndex, /*InAssetLodIndex=*/0);
	if (!bBindOk)
	{
		UE_LOG(LogRevvyCloth, Warning,
			TEXT("AddClothingSimulationToSection: BindToSkeletalMesh failed for mesh '%s' LOD %d Section %d cloth '%s'"),
			*SkelMeshAssetPath, LODIndex, SectionIndex, *ClothAssetPath);
		return false;
	}

	SaveLoadedAssetSafe(Cloth);
	SaveLoadedAssetSafe(SkelMesh);

	UE_LOG(LogRevvyCloth, Log,
		TEXT("AddClothingSimulationToSection: bound '%s' to '%s' LOD %d Section %d"),
		*ClothAssetPath, *SkelMeshAssetPath, LODIndex, SectionIndex);
	return true;
#else
	UE_LOG(LogRevvyCloth, Warning, TEXT("AddClothingSimulationToSection: editor-only, unavailable at runtime"));
	return false;
#endif
}

bool UChaosClothService::RemoveClothingFromSection(
	const FString& SkelMeshAssetPath,
	int32 LODIndex,
	int32 SectionIndex)
{
#if WITH_EDITOR
	USkeletalMesh* SkelMesh = ChaosClothLoadSkelMeshChecked(SkelMeshAssetPath, TEXT("RemoveClothingFromSection"));
	if (!SkelMesh)
	{
		return false;
	}

	if (LODIndex < 0 || SectionIndex < 0)
	{
		UE_LOG(LogRevvyCloth, Warning,
			TEXT("RemoveClothingFromSection: invalid LOD %d / Section %d"), LODIndex, SectionIndex);
		return false;
	}

	// Discover which cloth asset(s) are currently bound to this LOD/Section.
	// UE 5.8: FClothingAssetMeshBinding / GetAllLodMeshClothingAssetBindings moved from
	// struct FClothingAssetUtils (ClothingSystemEditorInterface) to namespace ClothingAssetUtils
	// in ClothingSystemRuntimeCommon/Public/ClothingAsset.h (already included above).
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> Bindings;
	ClothingAssetUtils::GetAllLodMeshClothingAssetBindings(SkelMesh, Bindings, LODIndex);

	TArray<UClothingAssetCommon*> Targets;
	for (const ClothingAssetUtils::FClothingAssetMeshBinding& B : Bindings)
	{
		if (B.LODIndex == LODIndex && B.SectionIndex == SectionIndex)
		{
			if (UClothingAssetCommon* Cloth = Cast<UClothingAssetCommon>(B.Asset))
			{
				Targets.AddUnique(Cloth);
			}
		}
	}

	if (Targets.Num() == 0)
	{
		UE_LOG(LogRevvyCloth, Warning,
			TEXT("RemoveClothingFromSection: no cloth binding found on '%s' LOD %d Section %d"),
			*SkelMeshAssetPath, LODIndex, SectionIndex);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("ChaosClothService", "RemoveClothingFromSection", "Remove Clothing From Section"));

	SkelMesh->Modify();

	bool bAnySucceeded = false;
	for (UClothingAssetCommon* Cloth : Targets)
	{
		Cloth->Modify();
		Cloth->UnbindFromSkeletalMesh(SkelMesh, LODIndex, SectionIndex);
		SaveLoadedAssetSafe(Cloth);
		bAnySucceeded = true;
	}

	SaveLoadedAssetSafe(SkelMesh);

	UE_LOG(LogRevvyCloth, Log,
		TEXT("RemoveClothingFromSection: unbound %d cloth asset(s) from '%s' LOD %d Section %d"),
		Targets.Num(), *SkelMeshAssetPath, LODIndex, SectionIndex);
	return bAnySucceeded;
#else
	UE_LOG(LogRevvyCloth, Warning, TEXT("RemoveClothingFromSection: editor-only, unavailable at runtime"));
	return false;
#endif
}

// ============================================================================
// Physics
// ============================================================================

bool UChaosClothService::SetClothPhysicsMass(const FString& ClothAssetPath, float Mass)
{
#if WITH_EDITOR
	UClothingAssetCommon* Cloth = LoadClothAssetChecked(ClothAssetPath, TEXT("SetClothPhysicsMass"));
	if (!Cloth)
	{
		return false;
	}

	UChaosClothConfig* Chaos = Cloth->GetClothConfig<UChaosClothConfig>();
	if (!Chaos)
	{
		UE_LOG(LogRevvyCloth, Warning,
			TEXT("SetClothPhysicsMass: '%s' has no UChaosClothConfig entry in ClothConfigs"),
			*ClothAssetPath);
		return false;
	}

	FScopedTransaction Tx(NSLOCTEXT("ChaosClothService", "SetClothPhysicsMass", "Set Cloth Physics Mass"));

	Cloth->Modify();
	Chaos->Modify();

	// Chaos cloth supports 3 mass modes; the caller supplied a single scalar so we
	// force TotalMass mode to make the value meaningful (Density / UniformMass units
	// differ significantly and would silently ignore Mass).
	Chaos->MassMode  = EClothMassMode::TotalMass;
	Chaos->TotalMass = FMath::Max(0.f, Mass);

	SaveLoadedAssetSafe(Cloth);

	UE_LOG(LogRevvyCloth, Log,
		TEXT("SetClothPhysicsMass: '%s' MassMode=TotalMass TotalMass=%.4f"),
		*ClothAssetPath, Chaos->TotalMass);
	return true;
#else
	UE_LOG(LogRevvyCloth, Warning, TEXT("SetClothPhysicsMass: editor-only, unavailable at runtime"));
	return false;
#endif
}

// ============================================================================
// Config
// ============================================================================

bool UChaosClothService::SetClothBendingStiffness(const FString& ClothAssetPath, float Stiffness)
{
#if WITH_EDITOR
	UClothingAssetCommon* Cloth = LoadClothAssetChecked(ClothAssetPath, TEXT("SetClothBendingStiffness"));
	if (!Cloth)
	{
		return false;
	}

	UChaosClothConfig* Chaos = Cloth->GetClothConfig<UChaosClothConfig>();
	if (!Chaos)
	{
		UE_LOG(LogRevvyCloth, Warning,
			TEXT("SetClothBendingStiffness: '%s' has no UChaosClothConfig entry"), *ClothAssetPath);
		return false;
	}

	const float Clamped = FMath::Clamp(Stiffness, 0.f, 1.f);

	FScopedTransaction Tx(NSLOCTEXT("ChaosClothService", "SetClothBendingStiffness", "Set Cloth Bending Stiffness"));

	Cloth->Modify();
	Chaos->Modify();

	// Collapse Low/High to the same value; callers wanting per-weight-map ramps
	// should edit the weighted struct directly.
	Chaos->BendingStiffnessWeighted.Low  = Clamped;
	Chaos->BendingStiffnessWeighted.High = Clamped;

	SaveLoadedAssetSafe(Cloth);

	UE_LOG(LogRevvyCloth, Log,
		TEXT("SetClothBendingStiffness: '%s' -> %.4f"), *ClothAssetPath, Clamped);
	return true;
#else
	UE_LOG(LogRevvyCloth, Warning, TEXT("SetClothBendingStiffness: editor-only, unavailable at runtime"));
	return false;
#endif
}

bool UChaosClothService::SetClothCollisionThickness(const FString& ClothAssetPath, float Thickness)
{
#if WITH_EDITOR
	UClothingAssetCommon* Cloth = LoadClothAssetChecked(ClothAssetPath, TEXT("SetClothCollisionThickness"));
	if (!Cloth)
	{
		return false;
	}

	UChaosClothConfig* Chaos = Cloth->GetClothConfig<UChaosClothConfig>();
	if (!Chaos)
	{
		UE_LOG(LogRevvyCloth, Warning,
			TEXT("SetClothCollisionThickness: '%s' has no UChaosClothConfig entry"), *ClothAssetPath);
		return false;
	}

	const float Clamped = FMath::Clamp(Thickness, 0.f, 1000.f);

	FScopedTransaction Tx(NSLOCTEXT("ChaosClothService", "SetClothCollisionThickness", "Set Cloth Collision Thickness"));

	Cloth->Modify();
	Chaos->Modify();

	Chaos->CollisionThickness = Clamped;

	SaveLoadedAssetSafe(Cloth);

	UE_LOG(LogRevvyCloth, Log,
		TEXT("SetClothCollisionThickness: '%s' -> %.4f cm"), *ClothAssetPath, Clamped);
	return true;
#else
	UE_LOG(LogRevvyCloth, Warning, TEXT("SetClothCollisionThickness: editor-only, unavailable at runtime"));
	return false;
#endif
}
