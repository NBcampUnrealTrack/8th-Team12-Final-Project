// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "PythonAPI/UBlueprintService.h"
#include "PythonAPI/UObjectReflectionService.h"

/**
 * Gates 1-6 of docs/design/unreal-cdo-readback-audit.md section 5.4.
 *
 * These were the audit's baseline tests and asserted the defect. The fix slice
 * inverted them: a class name passed to the object API is now refused, and the
 * class-default capability is reachable only under its own name.
 */
namespace RevvyCdoAuditTests
{
	// AActor::CustomTimeDilation — a plain float UPROPERTY on a class that is
	// always loaded in an editor build, with a non-empty default worth contrasting.
	const TCHAR* ProbeClass = TEXT("Actor");
	const TCHAR* ProbeProperty = TEXT("CustomTimeDilation");
	const TCHAR* AuditFolder = TEXT("/Game/RevvyAutomation");
}

// ============================================================================
// Gates 1 and 3 — a class name is refused, and the explicit CDO API still works
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyCdoAuditImplicitFallbackTest,
	"Revvy.Reflection.CdoAudit.ImplicitClassFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyCdoAuditImplicitFallbackTest::RunTest(const FString& Parameters)
{
	using namespace RevvyCdoAuditTests;

	// Gate 3: the capability survives under the name that describes it.
	const FString ViaClassApi =
		UObjectReflectionService::GetClassDefaultObjectProperty(ProbeClass, ProbeProperty);
	// Gate 1: the object API no longer answers a class question.
	const FString ViaObjectApi =
		UObjectReflectionService::GetProperty(ProbeClass, ProbeProperty);

	AddInfo(FString::Printf(
		TEXT("get_class_default_object_property('%s','%s') = '%s'"),
		ProbeClass, ProbeProperty, *ViaClassApi));
	AddInfo(FString::Printf(
		TEXT("get_property('%s','%s') = '%s'"),
		ProbeClass, ProbeProperty, *ViaObjectApi));

	TestFalse(TEXT("The explicit CDO API still returns the class default"),
		ViaClassApi.IsEmpty());
	TestTrue(TEXT("A class name passed to the object API returns nothing"),
		ViaObjectApi.IsEmpty());
	TestNotEqual(TEXT("The object API no longer mirrors the CDO API"),
		ViaObjectApi, ViaClassApi);

	// Gate 2 (regression): naming the actual object still resolves and reads. The
	// CDO has an object path of its own; asking for it by that path is legitimate,
	// and it is the class NAME that is now refused.
	const FString ViaCdoObjectPath = UObjectReflectionService::GetProperty(
		TEXT("/Script/Engine.Default__Actor"), ProbeProperty);
	AddInfo(FString::Printf(TEXT("object-path read of the CDO -> '%s'"), *ViaCdoObjectPath));
	TestEqual(TEXT("An explicit object path still resolves and reads"),
		ViaCdoObjectPath, ViaClassApi);

	return true;
}

// ============================================================================
// Gate 6 — instance value and class default stay distinguishable
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyCdoAuditInstanceVersusDefaultTest,
	"Revvy.Reflection.CdoAudit.InstanceVersusDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyCdoAuditInstanceVersusDefaultTest::RunTest(const FString& Parameters)
{
	using namespace RevvyCdoAuditTests;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		// Reported, never counted as covered.
		AddInfo(TEXT("Not exercised: no editor world available to spawn an instance into"));
		return true;
	}

	AActor* Instance = World->SpawnActor<AActor>();
	if (!Instance)
	{
		AddInfo(TEXT("Not exercised: the editor world refused to spawn a probe actor"));
		return true;
	}

	ON_SCOPE_EXIT { Instance->Destroy(); };

	const FString ClassDefault =
		UObjectReflectionService::GetClassDefaultObjectProperty(ProbeClass, ProbeProperty);

	// Give the instance a value its class default does not have.
	Instance->CustomTimeDilation = 3.5f;

	const FString InstancePath = Instance->GetPathName();
	const FString ViaInstancePath =
		UObjectReflectionService::GetProperty(InstancePath, ProbeProperty);
	const FString ViaClassName =
		UObjectReflectionService::GetProperty(ProbeClass, ProbeProperty);

	AddInfo(FString::Printf(TEXT("instance path '%s' -> '%s'"), *InstancePath, *ViaInstancePath));
	AddInfo(FString::Printf(TEXT("class name '%s' -> '%s' (default '%s')"),
		ProbeClass, *ViaClassName, *ClassDefault));

	// The instance API reads the instance — unchanged by the fix.
	TestTrue(TEXT("An object path reads the instance's own value"),
		ViaInstancePath.StartsWith(TEXT("3.5")));
	TestNotEqual(TEXT("The instance value differs from the class default"),
		ViaInstancePath, ClassDefault);

	// And the class name no longer produces a value at all, so the two can never be
	// confused by a caller holding only the returned string.
	TestTrue(TEXT("The class name yields nothing from the object API"), ViaClassName.IsEmpty());

	return true;
}

// ============================================================================
// Gate 4 — set_property with a class name writes nothing
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyCdoAuditNoImplicitCdoWriteTest,
	"Revvy.Reflection.CdoAudit.NoImplicitCdoWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyCdoAuditNoImplicitCdoWriteTest::RunTest(const FString& Parameters)
{
	using namespace RevvyCdoAuditTests;

	const FString Before =
		UObjectReflectionService::GetClassDefaultObjectProperty(ProbeClass, ProbeProperty);
	TestFalse(TEXT("The probe class default is readable before the write attempt"),
		Before.IsEmpty());

	const bool bWrote =
		UObjectReflectionService::SetProperty(ProbeClass, ProbeProperty, TEXT("9.75"));

	const FString After =
		UObjectReflectionService::GetClassDefaultObjectProperty(ProbeClass, ProbeProperty);

	AddInfo(FString::Printf(TEXT("set_property('%s') returned %s; CDO '%s' -> '%s'"),
		ProbeClass, bWrote ? TEXT("true") : TEXT("false"), *Before, *After));

	TestFalse(TEXT("set_property refuses a class name"), bWrote);
	TestEqual(TEXT("The class default object is unchanged by the refused write"), After, Before);
	TestNotEqual(TEXT("The attempted value did not land"), After, FString(TEXT("9.750000")));

	return true;
}

// ============================================================================
// Gate 5 — an inherited component is refused distinctly from an unknown one
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRevvyCdoAuditInheritedComponentTest,
	"Revvy.Blueprints.CdoAudit.InheritedComponentReadback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRevvyCdoAuditInheritedComponentTest::RunTest(const FString& Parameters)
{
	using namespace RevvyCdoAuditTests;

	// A C++ parent that owns a native component: AStaticMeshActor's StaticMeshComponent.
	const FString BlueprintPath = UBlueprintService::CreateBlueprint(
		TEXT("BP_RevvyCdoAuditInherited"), TEXT("StaticMeshActor"), AuditFolder);
	if (BlueprintPath.IsEmpty())
	{
		AddError(TEXT("Could not create the inherited-component fixture Blueprint"));
		return false;
	}

	ON_SCOPE_EXIT
	{
		if (UEditorAssetLibrary::DoesAssetExist(BlueprintPath))
		{
			UEditorAssetLibrary::DeleteAsset(BlueprintPath);
		}
	};

	const TArray<FBlueprintComponentInfo> Components =
		UBlueprintService::ListComponents(BlueprintPath);

	const FBlueprintComponentInfo* Inherited = Components.FindByPredicate(
		[](const FBlueprintComponentInfo& Info) { return Info.bIsInherited; });

	for (const FBlueprintComponentInfo& Info : Components)
	{
		AddInfo(FString::Printf(TEXT("component '%s' (%s) inherited=%d"),
			*Info.ComponentName, *Info.ComponentClass, Info.bIsInherited ? 1 : 0));
	}

	if (!Inherited)
	{
		AddInfo(TEXT("Not exercised: the parent class contributed no inherited component"));
		return true;
	}

	TestTrue(TEXT("Inherited components are labelled, not passed off as the Blueprint's own"),
		Inherited->bIsInherited);

	// Gate 5 proper: the two refusals must not look alike. Expectations are declared
	// BEFORE the calls that emit them — the framework matches forward, not back.
	AddExpectedMessage(
		TEXT("is inherited from 'StaticMeshActor' and has no Blueprint-owned template"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		1);
	AddExpectedMessage(
		TEXT("Component 'NoSuchComponentAnywhere' not found"),
		ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains,
		1);

	// Still refused — that is the safe side of the CDO-readback bug — and still
	// leaks no parent value.
	FString Value = TEXT("<untouched>");
	const bool bRead = UBlueprintService::GetComponentProperty(
		BlueprintPath, Inherited->ComponentName, TEXT("RelativeLocation"), Value);

	AddInfo(FString::Printf(TEXT("get_component_property('%s') -> %s, value '%s'"),
		*Inherited->ComponentName, bRead ? TEXT("true") : TEXT("false"), *Value));

	TestFalse(TEXT("An inherited component is refused rather than read from the parent CDO"),
		bRead);
	TestEqual(TEXT("A refused read leaks no parent-CDO value"), Value, FString(TEXT("<untouched>")));

	FString UnknownValue;
	const bool bUnknownRead = UBlueprintService::GetComponentProperty(
		BlueprintPath, TEXT("NoSuchComponentAnywhere"), TEXT("RelativeLocation"), UnknownValue);
	TestFalse(TEXT("An unknown component is also refused"), bUnknownRead);

	// An SCS-owned component reads normally — the refusal is about ownership, not
	// about the property or the call being broken.
	if (UBlueprintService::AddComponent(
		BlueprintPath, TEXT("SceneComponent"), TEXT("RevvyAuditOwned")))
	{
		FString OwnedValue;
		const bool bOwnedRead = UBlueprintService::GetComponentProperty(
			BlueprintPath, TEXT("RevvyAuditOwned"), TEXT("RelativeLocation"), OwnedValue);
		TestTrue(TEXT("A Blueprint-owned component reads normally"), bOwnedRead);
		TestFalse(TEXT("A Blueprint-owned component returns a value"), OwnedValue.IsEmpty());
	}
	else
	{
		AddInfo(TEXT("Not exercised: could not add an SCS-owned control component"));
	}

	return true;
}

#endif
