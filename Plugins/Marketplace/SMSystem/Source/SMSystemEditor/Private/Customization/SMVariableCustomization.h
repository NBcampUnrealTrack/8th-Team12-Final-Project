// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/Views/SListView.h"

enum class ESMGraphPropertyFlags : uint8;
struct FSMGraphProperty_Base;
class FKismetCompilerContext;
class IPropertyHandle;
class IPropertyUtilities;
class ISinglePropertyView;
class IDetailLayoutBuilder;
class IBlueprintEditor;
class UBlueprint;
class USMNodeInstance;

class FSMVariableCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedPtr<IDetailCustomization> MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor);

	FSMVariableCustomization(const TSharedPtr<IBlueprintEditor>& InBlueprintEditor, UBlueprint* Blueprint)
		: BlueprintEditorPtr(InBlueprintEditor)
		, BlueprintPtr(Blueprint)
	{}

	virtual ~FSMVariableCustomization() override;
	
	// IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// ~IDetailCustomization

private:
	/** Retrieve the FProperty representing this graph property. */
	const FProperty* GetFProperty(const FSMGraphProperty_Base* InGraphProperty) const;

	/** If we represent a native property. */
	bool IsNativeProperty(const FSMGraphProperty_Base* InGraphProperty) const;
	
	void OnStructContentsPreChanged(USMNodeInstance* InNodeInstance);

	bool IsResetToDefaultVisible(TSharedPtr<IPropertyHandle> InProperty) const;
	void OnResetToDefaultClicked(TSharedPtr<IPropertyHandle> InProperty);

	ECheckBoxState GetGraphPropertyFlagCheckBoxState(FSMGraphProperty_Base* InGraphProperty, ESMGraphPropertyFlags InFlag) const;
	bool ModifyGraphPropertyFlag(const TSharedPtr<IPropertyHandle>& InFlagHandle, const FSMGraphProperty_Base* InGraphProperty, ESMGraphPropertyFlags InFlag, bool bAdd);
	void RefreshPropertyList(const FSMGraphProperty_Base* InGraphProperty);
	TSharedRef<ITableRow> OnGenerateWidgetForPropertyList(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable);
	
	void OnNodeCompiled(const FKismetCompilerContext& InCompilerContext, TSharedPtr<IPropertyUtilities> InPropertyUtilities);
	
private:
	/** The Blueprint editor we are embedded in. */
	TWeakPtr<IBlueprintEditor> BlueprintEditorPtr;

	/** The blueprint we are editing. */
	TWeakObjectPtr<UBlueprint> BlueprintPtr;

	/** The node class we are editing. */
	TWeakObjectPtr<UClass> OwnerClassWeakPtr;
	
	/** Stores a handle to exposed property overrides. */
	TSharedPtr<ISinglePropertyView> ExposedPropertyOverridePropertyView;

	/** The list view of property flags. */
	TWeakPtr<SListView<TSharedPtr<FString>>> PropertyFlagListView;
	
	/** Graph property flags as a string. */
	TArray<TSharedPtr<FString>> GraphPropertyFlags;

	/** Direction options. */
	TArray<TSharedPtr<FString>> GraphPropertyDirectionOptions = {
		MakeShared<FString>(TEXT("Input")),
		MakeShared<FString>(TEXT("Output")),
		MakeShared<FString>(TEXT("Both"))
	};
};
