// Copyright Recursoft LLC. All Rights Reserved.

#include "SMVariableCustomization.h"

#include "Blueprints/SMBlueprintEditor.h"
#include "Compilers/SMCompilerDelegates.h"
#include "Properties/SMEditorPropertyUtils.h"
#include "Utilities/SMNodeInstanceUtils.h"

#include "SMNodeInstance.h"
#include "SMTransitionInstance.h"
#include "SMUtils.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"

#define LOCTEXT_NAMESPACE "SMVariableCustomization"

TSharedPtr<IDetailCustomization> FSMVariableCustomization::MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
{
	const TArray<UObject*>* Objects = (InBlueprintEditor.IsValid() ? InBlueprintEditor->GetObjectsCurrentlyBeingEdited() : nullptr);
	if (Objects && Objects->Num() == 1)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>((*Objects)[0]))
		{
			if (Blueprint->ParentClass->IsChildOf(USMNodeInstance::StaticClass()) &&
				!Blueprint->ParentClass->IsChildOf(USMTransitionInstance::StaticClass()))
			{
				return MakeShareable(new FSMVariableCustomization(InBlueprintEditor, Blueprint));
			}
		}
	}

	return nullptr;
}

FSMVariableCustomization::~FSMVariableCustomization()
{
	FSMCompilerDelegates::OnNodePostCompiled.RemoveAll(this);
}

void FSMVariableCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	if (!BlueprintPtr.IsValid() || !BlueprintPtr->GeneratedClass)
	{
		return;
	}
	
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 0)
	{
		UPropertyWrapper* PropertyWrapper = Cast<UPropertyWrapper>(ObjectsBeingCustomized[0].Get());
		const TWeakFieldPtr<FProperty> PropertyBeingCustomized = PropertyWrapper ? PropertyWrapper->GetProperty() : nullptr;
		if (PropertyBeingCustomized.IsValid() && PropertyBeingCustomized->Owner.IsValid())
		{
			UClass* OwnerClass = PropertyBeingCustomized->Owner.Get<UClass>();
			OwnerClassWeakPtr = OwnerClass;
			const bool bIsGraphProperty = LD::Editor::PropertyUtils::GetGraphPropertyFromProperty(PropertyBeingCustomized.Get()) != nullptr;
			
			// Filter to exclude local variables or properties that shouldn't be exposed to the graph.
			if (OwnerClass && OwnerClass->IsChildOf(USMNodeInstance::StaticClass()) &&
				(bIsGraphProperty || LD::Editor::PropertyUtils::IsPropertyExposedToGraphNode(PropertyBeingCustomized.Get())))
			{
				{
					FSMCompilerDelegates::OnNodePostCompiled.RemoveAll(this);

					const TSharedPtr<IPropertyUtilities> Utilities = DetailLayout.GetPropertyUtilities();
					FSMCompilerDelegates::OnNodePostCompiled.AddSP(this, &FSMVariableCustomization::OnNodeCompiled, Utilities);
				}
				
				USMNodeInstance* NodeInstance = CastChecked<USMNodeInstance>(BlueprintPtr->GeneratedClass->GetDefaultObject(false));

				const TSharedPtr<IPropertyHandle> GraphPropertyHandle = FSMNodeInstanceUtils::FindOrAddExposedPropertyOverrideByName(NodeInstance, PropertyBeingCustomized->GetFName(),
					ExposedPropertyOverridePropertyView);
				check(GraphPropertyHandle.IsValid());
			
				IDetailCategoryBuilder& Category = DetailLayout.EditCategory("Variable");

				const TSharedPtr<IPropertyHandle> ReadOnlyHandle = GraphPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSMGraphProperty, bReadOnly));
				const TSharedPtr<IPropertyHandle> HiddenHandle = GraphPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSMGraphProperty, bHidden));
				const TSharedPtr<IPropertyHandle> WidgetHandle = GraphPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FSMGraphProperty, WidgetInfo));
				const TSharedPtr<IPropertyHandle> PropertyFlagHandle = GraphPropertyHandle->GetChildHandle(FSMGraphProperty_Base_Runtime::GetPropertyFlagsPropertyName());

				check(ReadOnlyHandle.IsValid());
				check(HiddenHandle.IsValid());
				check(WidgetHandle.IsValid());
				check(PropertyFlagHandle.IsValid());
				
				IDetailGroup& DetailGroup = Category.AddGroup("StateMachineVariable", LOCTEXT("StateMachineVariableDisplayName", "State Machine Variable"),
					false, true);

				// Add individual properties rather under a group rather than the owning struct.
				// In UE4 the array handle of the struct will show up otherwise.

				// Perform special ResetToDefaults handling only on the class owning the property.
				// It always shows up otherwise, however in the event the user is overriding a parent property
				// ResetToDefaults works and displays correctly.

				UClass* NodeInstanceUpToDateClass = FBlueprintEditorUtils::GetMostUpToDateClass(NodeInstance->GetClass());
				UClass* OwnerClassUpToDateClass = FBlueprintEditorUtils::GetMostUpToDateClass(OwnerClass);

				OwnerClassWeakPtr = OwnerClassUpToDateClass;
				
				const bool bPropertyOwnedByThisBlueprint = NodeInstanceUpToDateClass == OwnerClassUpToDateClass;

				const FResetToDefaultOverride ResetToDefaultOverride = FResetToDefaultOverride::Create(
					FIsResetToDefaultVisible::CreateSP(this, &FSMVariableCustomization::IsResetToDefaultVisible),
					FResetToDefaultHandler::CreateSP(this, &FSMVariableCustomization::OnResetToDefaultClicked));

				FSMGraphProperty_Base* GraphProperty = nullptr;
				
				FSMGraphProperty_Base::FVariableDetailsCustomizationConfiguration Config;
				if (bIsGraphProperty)
				{
					// For actual graph properties use the most up to date class for correct variable details information.
					// TODO: NodeInstanceUpToDateClass needed to avoid an ensure, but arrays don't work right if not using the default GeneratedClass.
					TArray<FSMGraphProperty_Base*> GraphProperties;
					USMUtils::BlueprintPropertyToNativeProperty(PropertyBeingCustomized.Get(), NodeInstanceUpToDateClass->GetDefaultObject(false), GraphProperties);
					if (GraphProperties.Num())
					{
						// Apply specific customization for this type of graph property.
						GraphProperties[0]->GetVariableDetailsCustomization(Config);
						GraphProperty = GraphProperties[0];
					}
				}

				// Get access to the graph property override data directly if this wasn't already a graph property.
				if (!GraphProperty)
				{
					void* Data;
					const FPropertyAccess::Result Result = GraphPropertyHandle->GetValueData(Data);
					check(Result == FPropertyAccess::Success);

					GraphProperty = static_cast<FSMGraphProperty_Base*>(Data);
				}

				const bool bIsNative = IsNativeProperty(GraphProperty);
				
				if (Config.bShowDirectionSelection && !bIsNative)
				{
					auto GetInitialSelectedOption = [this](const FSMGraphProperty_Base* InGraphProperty) -> TSharedPtr<FString>
					{
						if (InGraphProperty->HasAllPropertyFlags(ESMGraphPropertyFlags::Input | ESMGraphPropertyFlags::Output))
						{
							return GraphPropertyDirectionOptions[2];
						}
						if (InGraphProperty->HasAnyPropertyFlags(ESMGraphPropertyFlags::Input))
						{
							return GraphPropertyDirectionOptions[0];
						}
						if (InGraphProperty->HasAnyPropertyFlags(ESMGraphPropertyFlags::Output))
						{
							return GraphPropertyDirectionOptions[1];
						}
						return MakeShared<FString>(TEXT("None"));
					};

					DetailGroup.AddWidgetRow()
					.NameContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("VariableDirection", "Direction"))
						.ToolTipText(LOCTEXT("VariableDirectionTooltip", "Select the direction of the variable pin when instanced in a state machine graph.\
											   \nInput - Variable will have its values set from the state machine graph when starting.\
											   \nOutput - Variable will write its values to the state machine graph when ending.\
											   \nBoth - Variable will read values when starting and write values when ending."))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
					.ValueContent()
					.MinDesiredWidth(166.0f)
					[
						SNew(SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&GraphPropertyDirectionOptions)
						.OnGenerateWidget_Lambda([](TSharedPtr<FString> InOption)
						{
							return SNew(STextBlock)
								.Text(FText::FromString(*InOption));
						})
						.OnSelectionChanged_Lambda([this, GraphProperty, PropertyFlagHandle](TSharedPtr<FString> NewSelection, ESelectInfo::Type)
						{
							if (NewSelection.IsValid())
							{
								if (*NewSelection == TEXT("Input"))
								{
									if (ModifyGraphPropertyFlag(PropertyFlagHandle, GraphProperty, ESMGraphPropertyFlags::Input, true))
									{
										ModifyGraphPropertyFlag(PropertyFlagHandle, GraphProperty, ESMGraphPropertyFlags::Output, false);
									}
								}
								else if (*NewSelection == TEXT("Output"))
								{
									if (ModifyGraphPropertyFlag(PropertyFlagHandle, GraphProperty, ESMGraphPropertyFlags::Output, true))
									{
										ModifyGraphPropertyFlag(PropertyFlagHandle, GraphProperty, ESMGraphPropertyFlags::Input, false);
									}
								}
								else if (*NewSelection == TEXT("Both"))
								{
									ModifyGraphPropertyFlag(PropertyFlagHandle, GraphProperty, ESMGraphPropertyFlags::Input | ESMGraphPropertyFlags::Output, true);
								}
							}
						})
						.InitiallySelectedItem(GetInitialSelectedOption(GraphProperty))
						[
							SNew(STextBlock)
							.Text_Lambda([this, GraphProperty]()
							{
								// Dynamically update the displayed text based on the current flag state.
								if (GraphProperty->HasAllPropertyFlags(ESMGraphPropertyFlags::Input | ESMGraphPropertyFlags::Output))
								{
									return LOCTEXT("DirectionBothOption", "Both");
								}
								if (GraphProperty->HasAnyPropertyFlags(ESMGraphPropertyFlags::Input))
								{
									return LOCTEXT("DirectionInputOption", "Input");
								}
								if (GraphProperty->HasAnyPropertyFlags(ESMGraphPropertyFlags::Output))
								{
									return LOCTEXT("DirectionOutputOption", "Output");
								}
								ensureMsgf(false, TEXT("No directional flag present."));
								return LOCTEXT("DirectionNoneOption", "None");
							})
						]
					];
				}

				if (Config.bShowReadOnly)
				{
					IDetailPropertyRow& PropertyRow = DetailGroup.AddPropertyRow(ReadOnlyHandle.ToSharedRef());
					if (bPropertyOwnedByThisBlueprint)
					{
						PropertyRow.OverrideResetToDefault(ResetToDefaultOverride);
					}
				}

				if (Config.bShowHidden)
				{
					IDetailPropertyRow& PropertyRow = DetailGroup.AddPropertyRow(HiddenHandle.ToSharedRef());
					if (bPropertyOwnedByThisBlueprint)
					{
						PropertyRow.OverrideResetToDefault(ResetToDefaultOverride);
					}
				}

				if (Config.bShowWidgetInfo)
				{
					IDetailPropertyRow& PropertyRow = DetailGroup.AddPropertyRow(WidgetHandle.ToSharedRef());
					if (bPropertyOwnedByThisBlueprint)
					{
						PropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Hide());
					}
				}

				if (Config.bShowPropertyFlags)
				{
					RefreshPropertyList(GraphProperty);
					
					DetailGroup.AddWidgetRow()
					.WholeRowWidget
					[
						SNew(SVerticalBox)
						+SVerticalBox::Slot()
						.Padding(0.f, 2.f)
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GraphPropertyFlags", "Graph Property Flags"))
							.ToolTipText(LOCTEXT("GraphPropertyFlagsTooltip", "Current runtime property flags for this graph property."))
							.Font(IDetailLayoutBuilder::GetDetailFontBold())
						]
						+SVerticalBox::Slot()
						.Padding(0.f, 2.f)
						[
							SAssignNew(PropertyFlagListView, SListView<TSharedPtr<FString>>)
							.OnGenerateRow(this, &FSMVariableCustomization::OnGenerateWidgetForPropertyList)
							.ListItemsSource(&GraphPropertyFlags)
							.SelectionMode(ESelectionMode::None)
							.ScrollbarVisibility(EVisibility::Collapsed)
						]
					];
				}

				const FSimpleDelegate OnStructContentsPreChangedDelegate = FSimpleDelegate::CreateSP(this,
					&FSMVariableCustomization::OnStructContentsPreChanged, NodeInstance);
			
				GraphPropertyHandle->SetOnChildPropertyValuePreChange(OnStructContentsPreChangedDelegate);
				GraphPropertyHandle->SetOnPropertyValuePreChange(OnStructContentsPreChangedDelegate);
			}
		}
	}
}

const FProperty* FSMVariableCustomization::GetFProperty(const FSMGraphProperty_Base* InGraphProperty) const
{
	check(InGraphProperty);
	if (OwnerClassWeakPtr.IsValid())
	{
		return OwnerClassWeakPtr->FindPropertyByName(InGraphProperty->VariableName);
	}

	return nullptr;
}

bool FSMVariableCustomization::IsNativeProperty(const FSMGraphProperty_Base* InGraphProperty) const
{
	if (const FProperty* Property = GetFProperty(InGraphProperty))
	{
		return Property->IsNative();
	}

	return false;
}

void FSMVariableCustomization::OnStructContentsPreChanged(USMNodeInstance* InNodeInstance)
{
	if (InNodeInstance)
	{
		InNodeInstance->Modify();
	}
}

bool FSMVariableCustomization::IsResetToDefaultVisible(TSharedPtr<IPropertyHandle> InProperty) const
{
	bool bValue = false;
	if (InProperty.IsValid())
	{
		InProperty->GetValue(bValue);
	}

	// False is the default value for the properties we're checking.
	return bValue;
}

void FSMVariableCustomization::OnResetToDefaultClicked(TSharedPtr<IPropertyHandle> InProperty)
{
	if (InProperty.IsValid())
	{
		InProperty->SetValue(false);
	}
}

ECheckBoxState FSMVariableCustomization::GetGraphPropertyFlagCheckBoxState(FSMGraphProperty_Base* InGraphProperty, ESMGraphPropertyFlags InFlag) const
{
	return InGraphProperty && InGraphProperty->HasAnyPropertyFlags(InFlag) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool FSMVariableCustomization::ModifyGraphPropertyFlag(const TSharedPtr<IPropertyHandle>& InFlagHandle, const FSMGraphProperty_Base* InGraphProperty,
	ESMGraphPropertyFlags InFlag, bool bAdd)
{
	if (InGraphProperty && InFlagHandle.IsValid())
	{
		if (bAdd && !InGraphProperty->CanAcceptPropertyFlag(InFlag))
		{
			return false;
		}
		
		uint8 CurrentFlags = 0;
		check(InFlagHandle->GetValue(CurrentFlags) == FPropertyAccess::Success);

		if (bAdd)
		{
			CurrentFlags |= static_cast<uint8>(InFlag); // Add the flag.
		}
		else
		{
			CurrentFlags &= ~static_cast<uint8>(InFlag); // Remove the flag.
		}

		check(InFlagHandle->SetValue(CurrentFlags) == FPropertyAccess::Success);

		RefreshPropertyList(InGraphProperty);
		PropertyFlagListView.Pin()->RebuildList();
		
		return true;
	}

	return false;
}

void FSMVariableCustomization::RefreshPropertyList(const FSMGraphProperty_Base* InGraphProperty)
{
	check(InGraphProperty);
	GraphPropertyFlags.Empty();
	const UEnum* EnumPtr = StaticEnum<ESMGraphPropertyFlags>();
	
	if (const FProperty* Property = GetFProperty(InGraphProperty))
	{
		if (Property->IsNative())
		{
			// Always make sure we're synced with native properties. They shouldn't be changing.
			LD::Editor::PropertyUtils::SetGraphPropertyFlagsFromNativeProperty(*const_cast<FSMGraphProperty_Base*>(InGraphProperty), Property);
		}
	}
	
	// Iterate over all possible values in the enum.
	for (uint8 BitIdx = 0; BitIdx < 8 * sizeof(ESMGraphPropertyFlags); ++BitIdx) // Assuming 8-bit flags
	{
		const ESMGraphPropertyFlags Flag = static_cast<ESMGraphPropertyFlags>(1 << BitIdx);

		// Check if the flag is set in the current property flags.
		if (EnumHasAnyFlags(InGraphProperty->GetPropertyFlags(), Flag))
		{
			// Add the flag name to the array as a string.
			FString FlagName = EnumPtr->GetNameByValue(static_cast<int64>(Flag)).ToString();
			GraphPropertyFlags.Add(MakeShared<FString>(FlagName));
		}
	}
}

TSharedRef<ITableRow> FSMVariableCustomization::OnGenerateWidgetForPropertyList(TSharedPtr<FString> Item,
                                                                                const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
	.Padding(2.f)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		[
			SNew(STextBlock)
				.Text(FText::FromString(*Item.Get()))
				.ToolTipText(FText::FromString(*Item.Get()))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SCheckBox)
				.IsChecked(ECheckBoxState::Checked)
				.IsEnabled(false)
		]
	];
}

void FSMVariableCustomization::OnNodeCompiled(const FKismetCompilerContext& InCompilerContext, TSharedPtr<IPropertyUtilities> InPropertyUtilities)
{
	// Loading children node classes can invalidate the current selection.
	if (InPropertyUtilities.IsValid())
	{
		InPropertyUtilities->EnqueueDeferredAction(FSimpleDelegate::CreateSP(InPropertyUtilities.ToSharedRef(), &IPropertyUtilities::ForceRefresh));
	}
}

#undef LOCTEXT_NAMESPACE
