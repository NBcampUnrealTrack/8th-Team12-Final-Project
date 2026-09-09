---
name: ue-automation-functional-tests
description: "Use this skill when authoring Unreal automated tests: AFunctionalTest actors, Automation Test macros and Specs, Session Frontend, command-line test execution, Gauntlet client-server/performance scenarios, test categorization, and smoke versus nightly build gates."
metadata:
  version: 1.0.1
---

# UE Automation Framework + Functional Tests + Gauntlet

You are an expert in UE's automated testing stack: code unit tests (Automation Framework), in-world Functional Tests, BDD-style Automation Specs, and Gauntlet for cooked-build integration testing.

## Context Check

Read `.agents/ue-project-context.md` first. Confirm:

- `FunctionalTesting` plugin enabled (default yes)
- Automation Spec usage (engine has it; no plugin needed)
- Gauntlet plugin enabled for cooked-build tests
- Existing test maps under `Content/Tests/`
- `.uproject` test module declarations
- Whether CI runs tests via `RunUAT` already

## Information to Gather

1. What level of test — unit (no world), in-world (PIE), cooked-build integration?
2. Single test or test suite?
3. Should it block the build (smoke) or run nightly?
4. Are there networking concerns (client-server) — Gauntlet?
5. Performance regression target (FPS, mem) — Gauntlet perf capture?

---

## Test Type Decision Tree

```
Need a real world / actors / PIE?
├─ No → IMPLEMENT_SIMPLE_AUTOMATION_TEST (unit)
│       or Automation Spec (BDD-style)
├─ Yes, single deterministic flow → AFunctionalTest actor in test map
└─ Yes, cooked client+server, perf capture → Gauntlet
```

---

## IMPLEMENT_SIMPLE_AUTOMATION_TEST

Source: `Runtime/Core/Public/Misc/AutomationTest.h`

```cpp
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDuckSoulInventoryAddTest,
    "DuckSoul.Inventory.Add",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::EngineFilter)

bool FDuckSoulInventoryAddTest::RunTest(const FString& Parameters)
{
    FDuckSoulInventory Inv;
    Inv.Add(FName("Potion"), 3);
    TestEqual(TEXT("Stack count"), Inv.GetCount(FName("Potion")), 3);

    Inv.Add(FName("Potion"), 2);
    TestEqual(TEXT("Stack merged"), Inv.GetCount(FName("Potion")), 5);

    return true;
}
```

### Test name hierarchy

`"DuckSoul.Inventory.Add"` → tree path in Session Frontend. Use dots for
namespacing. The Filter UI matches on substring.

### Flags

| Flag | Purpose |
|---|---|
| `EditorContext` | Runs in editor |
| `ClientContext` | Runs in cooked client |
| `ServerContext` | Runs in cooked server |
| `CommandletContext` | Runs in `-run=Automation` |
| `EngineFilter` | Categorize under Engine in UI |
| `SmokeFilter` | Build-gating smoke set |
| `EngineFilter \| StressFilter` | Slow tests |

### Test assertion API

```cpp
TestTrue(TEXT("Description"), bCondition);
TestFalse(TEXT("Description"), bCondition);
TestEqual(TEXT("Description"), Actual, Expected);
TestEqual(TEXT("Description"), ActualString, ExpectedString);
TestNotEqual, TestSame, TestNotNull, TestValid(...);

AddExpectedError(TEXT("ExpectedSubstring"));  // suppress and assert error
AddError(TEXT("Description"));                 // fail without abort
AddWarning(TEXT("Description"));               // log only
```

---

## IMPLEMENT_COMPLEX_AUTOMATION_TEST (Parameterized)

```cpp
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FDuckSoulAllMapsLoadTest,
    "DuckSoul.Maps.Load",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDuckSoulAllMapsLoadTest::GetTests(TArray<FString>& OutBeautifiedNames,
                                        TArray<FString>& OutTestCommands) const
{
    TArray<FAssetData> MapAssets;
    UAssetManager::Get().GetPrimaryAssetDataList(
        FPrimaryAssetType("Map"), MapAssets);
    for (const FAssetData& A : MapAssets)
    {
        OutBeautifiedNames.Add(A.AssetName.ToString());
        OutTestCommands.Add(A.GetSoftObjectPath().ToString());
    }
}

bool FDuckSoulAllMapsLoadTest::RunTest(const FString& Parameters)
{
    // Parameters holds one entry from OutTestCommands per invocation
    return AutomationOpenMap(Parameters);
}
```

One test definition → N test runs, one per map. The Session Frontend shows
each as a row.

---

## Automation Spec (BDD-style)

```cpp
BEGIN_DEFINE_SPEC(FDuckSoulShopSpec,
    "DuckSoul.Shop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

    TUniquePtr<FDuckSoulShop> Shop;

END_DEFINE_SPEC(FDuckSoulShopSpec)

void FDuckSoulShopSpec::Define()
{
    Describe("FDuckSoulShop", [this]
    {
        BeforeEach([this]() { Shop = MakeUnique<FDuckSoulShop>(); });
        AfterEach([this]() { Shop.Reset(); });

        Describe("when a player buys an item", [this]
        {
            It("should deduct gold", [this]()
            {
                Shop->SetGold(100);
                Shop->Buy(FName("Potion"), 50);
                TestEqual("Gold remaining", Shop->GetGold(), 50);
            });

            It("should reject if insufficient gold", [this]()
            {
                Shop->SetGold(10);
                const bool bOk = Shop->Buy(FName("Potion"), 50);
                TestFalse("Purchase blocked", bOk);
                TestEqual("Gold unchanged", Shop->GetGold(), 10);
            });
        });
    });
}
```

Cleaner than simple tests for state-heavy logic. Spec runner handles setup
isolation per `It` block.

---

## AFunctionalTest (In-World Tests)

Source: `FunctionalTesting/Classes/FunctionalTest.h`

### Authoring in editor

1. Open a test map (or create one under `Content/Tests/`).
2. Place an `AFunctionalTest` actor.
3. In Details panel: set Test Name, Time Limit, Test Description.
4. Use Sequence Events: `OnTestStart`, `OnTestFinished`.
5. Drive the test via Blueprint logic OR a custom C++ subclass.

### C++ subclass pattern

```cpp
UCLASS()
class ADuckSoulShopHUDTest : public AFunctionalTest
{
    GENERATED_BODY()
public:
    virtual void PrepareTest() override;
    virtual bool IsReady_Implementation() override;
    virtual void StartTest() override;

private:
    UFUNCTION() void Step_OpenShop();
    UFUNCTION() void Step_CheckWidgetVisible();
    UFUNCTION() void Step_BuyPotion();
    UFUNCTION() void Step_VerifyInventory();

    int32 CurrentStep = 0;
    FTimerHandle StepTimer;
};
```

```cpp
void ADuckSoulShopHUDTest::PrepareTest()
{
    Super::PrepareTest();
    // Possess Duck, hide other actors, set deterministic seed
}

bool ADuckSoulShopHUDTest::IsReady_Implementation()
{
    // Wait until Experience loaded
    AGameStateBase* GS = GetWorld()->GetGameState();
    auto* Mgr = GS ? GS->FindComponentByClass<ULyraExperienceManagerComponent>() : nullptr;
    return Mgr && Mgr->IsExperienceLoaded();
}

void ADuckSoulShopHUDTest::StartTest()
{
    Super::StartTest();
    Step_OpenShop();
}

void ADuckSoulShopHUDTest::Step_OpenShop()
{
    APlayerController* PC = GetWorld()->GetFirstPlayerController();
    PC->ConsoleCommand(TEXT("OpenShop"));
    GetWorldTimerManager().SetTimer(StepTimer, this,
        &ADuckSoulShopHUDTest::Step_CheckWidgetVisible, 0.5f);
}

void ADuckSoulShopHUDTest::Step_CheckWidgetVisible()
{
    bool bFound = /* search for W_ShopHUD on PrimaryGameLayout */;
    AssertTrue(bFound, TEXT("Shop widget visible"));
    Step_BuyPotion();
}

// ... etc ...
// FinishTest(EFunctionalTestResult::Succeeded, TEXT("OK")) when done
```

### AssertTrue from Blueprint

In BP `AFunctionalTest`, use `Assert True` / `Assert Equal` nodes. They
auto-log to the result and fail the test on miss.

---

## Session Frontend UI

In editor: **Window → Developer Tools → Session Frontend → Automation tab**.

- Tree of all tests (by namespace).
- Filter by Smoke / Engine / Editor / Stress.
- Select tests → Start Tests.
- History pane shows pass/fail per run with logs.

For headless runs:

```bash
UnrealEditor-Cmd.exe DuckSoul.uproject -ExecCmds="Automation RunTests DuckSoul.Inventory;Quit" -unattended -nopause -log
```

---

## RunUAT BuildCookRun + Tests

```bash
RunUAT.bat BuildCookRun -project=DuckSoul.uproject -platform=Win64 -clientconfig=Development -cook -stage -pak -archive -archivedirectory=Out -RunAutomationTests="DuckSoul.Smoke+DuckSoul.Functional"
```

Tests run after cook against the cooked content. Results land in
`Saved/Logs/Automation.log` and the standard CI surfaces in
`Saved/Automation/Reports/`.

---

## Gauntlet

Cooked-build integration testing — runs full client + server processes.

```bash
RunUAT.bat RunUnreal -project=DuckSoul.uproject -platform=Win64
    -configuration=Development -test=DuckSoul.GauntletTestApp
    -build=Local -clean
```

`DuckSoul.GauntletTestApp` is a `UnrealAutomationTest` (C# under `Build/`)
that spawns processes, captures logs, asserts conditions.

Use when:
- Test must run against staged build (not editor)
- Multi-process (server + clients)
- Capture FPS/mem traces (`-StatsReport`)
- Long-soak (15 min loop)

For smaller tests, prefer Functional Tests — Gauntlet has high setup cost.

---

## Test Categorization

Add a tag-style namespace:

```
DuckSoul.Smoke.*       — fast, build-gating (< 30s total)
DuckSoul.Functional.*  — in-world, ~30s each
DuckSoul.Nightly.*     — slow / cross-platform / long-soak
DuckSoul.Perf.*        — Gauntlet perf-regression
```

In CI:

- PR build: `-RunAutomationTests="DuckSoul.Smoke"`
- Nightly: `-RunAutomationTests="DuckSoul.Smoke+DuckSoul.Functional+DuckSoul.Nightly"`
- Perf job: separate Gauntlet pipeline

---

## DuckSoul Recipe — Shop Widget Test

Goal: prove that loading `ShopLevel`, possessing Duck, opening shop UI, and
purchasing a Potion all work end-to-end.

1. Create map `Content/Tests/Map_Test_Shop.umap` with a Duck spawn + AFunctionalTest.
2. Subclass `ADuckSoulShopHUDTest` (above) and place in map.
3. Set test name `DuckSoul.Functional.Shop.PurchaseHappyPath`.
4. Time limit: 10 seconds.
5. CI: include in `DuckSoul.Functional` filter.

Run locally:

```
Automation RunTests DuckSoul.Functional.Shop.PurchaseHappyPath
```

---

## Module Setup for Tests

Tests live in a test module (only built when running tests):

```cpp
// DuckSoulTests.Build.cs
public class DuckSoulTests : ModuleRules
{
    public DuckSoulTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine",
            "FunctionalTesting", "AutomationController",
            "AutomationTest",
            "DuckSoul"   // the game module being tested
        });
    }
}
```

In `.uproject`:

```json
{
    "Name": "DuckSoulTests",
    "Type": "Editor",
    "LoadingPhase": "Default",
    "TargetAllowList": ["Editor", "Game"]
}
```

---

## Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| Test passes locally, fails in CI | World reuse — PIE state from previous test leaked | Always reset state in `BeforeEach` / `PrepareTest`; never assume clean world |
| AFunctionalTest hangs at start | `IsReady_Implementation` never returns true | Add timeout fallback; log why not ready |
| Async asset load mid-test never completes | Streaming source moved away | Pin asset via `FStreamableManager::RequestSyncLoad` for test scope |
| Test marked "passed" but assertions never ran | Forgot `FinishTest(...)` call | Always call `FinishTest` in completion handler |
| Save game from PIE bleeds | Default save slot used | Use unique save slot name per test |
| Tests share global timer | `FTimerManager` is world-scoped | Use `GetWorldTimerManager()` not stale UWorld ptr |
| Spec setup runs once per file, not per It | Misused `Describe` vs `It` | Setup needing per-test isolation goes in `BeforeEach` |
| Gauntlet client doesn't connect to server | Listen-server NAT / port conflict in CI | Use loopback + custom port; explicit `-PORT=` flag |
| Smoke test set drifts | No filter enforcement in CI | CI step asserts test count > N for the Smoke filter |

---

## Build.cs

```csharp
PublicDependencyModuleNames.AddRange(new[] {
    "Core", "CoreUObject", "Engine",
    "FunctionalTesting", "AutomationController", "AutomationTest"
});
// Optional Gauntlet:
PrivateDependencyModuleNames.Add("Gauntlet");
```

---

## Related Skills

- `pie-testing` — manual PIE workflow; complements automated tests.
- `ue-testing-debugging` — debug/diagnostic tools; pairs with this skill for
  failed-test investigation.
- `ue-lyra-experience` — many functional tests gate on `OnExperienceLoaded`.
- `ue-common-ui` — UI tests need widget-search helpers on PrimaryGameLayout.
- `ue-build-cook-package` — cooked tests run inside the build/cook pipeline.
