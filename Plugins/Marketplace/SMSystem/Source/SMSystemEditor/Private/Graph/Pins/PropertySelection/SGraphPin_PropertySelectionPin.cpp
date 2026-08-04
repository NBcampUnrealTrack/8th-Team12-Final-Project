// Copyright Recursoft LLC. All Rights Reserved.

#include "SGraphPin_PropertySelectionPin.h"

#include "Blueprints/SMBlueprintEditor.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOBase.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOReaderNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyIOWriterNode.h"
#include "Graph/Nodes/PropertyNodes/SMGraphK2Node_PropertyNode_Base.h"
#include "Graph/Nodes/SMGraphNode_Base.h"
#include "Graph/Nodes/SMGraphNode_StateNodeBase.h"
#include "Graph/Pins/SGraphPin_SMDefaults.h"
#include "Properties/PropertyInteraction/SMPropertyInteractionManager.h"
#include "SMUnrealTypeDefs.h"
#include "Utilities/SMBlueprintEditorUtils.h"
#include "Widgets/Properties/SSMPropertyWidget.h"
#include "Widgets/StateSelection/SSMStatePropertySelectionTreeView.h"

#include "Blueprints/SMBlueprint.h"
#include "SMInstance.h"
#include "SMNodeInstance.h"
#include "SMStateInstance.h"

#include "Framework/Application/SlateApplication.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "SPinTypeSelector.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SToolTip.h"

#define LOCTEXT_NAMESPACE "SMPropertySelectionPin"

TSharedPtr<SGraphPin> FSMPropertySelectionPinFactory::CreatePin(UEdGraphPin* InPin) const
{
	if (InPin)
	{
		if (const USMGraphK2Node_PropertyIOBase* PropertyNode = Cast<USMGraphK2Node_PropertyIOBase>(InPin->GetOwningNode()))
		{
			const bool bIsPropertySelection = PropertyNode->IsPropertySelectionPin(InPin);
			if (bIsPropertySelection
				|| PropertyNode->IsPropertyConnectorPin(InPin)
				|| USMGraphK2Node_PropertyIOWriterNode::IsPrimaryInputPin(InPin))
			{
				// Determine the direction of the property selection. It will be the opposite of the connector pin.
				// This pin widget handles displaying both property selection and the pin connector.
				const EEdGraphPinDirection PropertySelectionDirection = bIsPropertySelection ? InPin->Direction.GetValue() :
					InPin->Direction == EGPD_Input ? EGPD_Output : EGPD_Input;
				
				return SNew(SGraphPin_PropertySelectionPin, InPin)
					.PropertySelectionDirection(PropertySelectionDirection)
					.DisplayPinTooltips(true)
					.MinPropertySelectionWidth(165.f)
					.MaxPropertySelectionWidth(165.f);
			}
		}
	}

	return nullptr;
}

void FSMPropertySelectionPinFactory::RegisterFactory()
{
	FEdGraphUtilities::RegisterVisualPinFactory(MakeShared<FSMPropertySelectionPinFactory>());
}

void SGraphPin_PropertySelectionPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	GraphPinObj = InGraphPinObj;
	check(GraphPinObj);

	PropertySelectionDirection = InArgs._PropertySelectionDirection;
	OnPropertySelectionOpenChanged = InArgs._OnPropertySelectionOpenChanged;
	OnContextMenuOpening = InArgs._OnContextMenuOpening;
	OnMouseButtonDownEvent = InArgs._OnMouseButtonDown;
	MinPropertySelectionWidth = InArgs._MinPropertySelectionWidth;
	MaxPropertySelectionWidth = InArgs._MaxPropertySelectionWidth;
	
	const bool bIsPropertySelection = (GetDirection() == PropertySelectionDirection);
	const bool bIsPrimaryInput = USMGraphK2Node_PropertyIOWriterNode::IsPrimaryInputPin(InGraphPinObj);
	
	bOnlyShowDefaultValue = bIsPropertySelection;
	bUsePinColorForText = false;
	bShowLabel = bIsPrimaryInput;

	const bool bCanDrawArrows = InArgs._CanDrawArrows
	&& (GetDirection() == EGPD_Input
		&& !bIsPrimaryInput
		&& (GetPinConnectorVisibility() == EVisibility::Visible || IsPropertySelected()));
	
	this->SetCursor(EMouseCursor::Default);
	SetVisibility(MakeAttributeSP(this, &SGraphPin_PropertySelectionPin::GetPinVisiblity));
	SetIsEditable(MakeAttributeSP(this, &SGraphPin_PropertySelectionPin::CanPinBeEdited));
	
	// Default args of SGraphPin
	constexpr float SideToSideMargin = 5.0f;
	const FName PinLabelStyle = NAME_DefaultPinLabelStyle;
	// ~Default args of SGraphPin

	// Height needs to be consistent between connector pins and selectors. However, there are vertical alignment
	// issues with the default value widget that has a property selected. Locking the property selection height and
	// adding a small amount helps. We could also do a negative margin but this way is probably more consistent.
	const float ConsistentHeight = MaxPropertySelectionHeight + 2.f;
	
	const UEdGraphSchema* Schema = GraphPinObj->GetSchema();
	checkf(
		Schema, 
		TEXT("Missing schema for pin: %s with outer: %s of type %s"), 
		*(GraphPinObj->GetName()),
		GraphPinObj->GetOuter() ? *(GraphPinObj->GetOuter()->GetName()) : TEXT("NULL OUTER"), 
		GraphPinObj->GetOuter() ? *(GraphPinObj->GetOuter()->GetClass()->GetName()) : TEXT("NULL OUTER")
	);
	
	// Create the pin indicator widget (used for watched values)
	static const FName NAME_NoBorder("NoBorder");
	const TSharedRef<SWidget> PinStatusIndicator =
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), NAME_NoBorder)
		.Visibility(this, &SGraphPin_PropertySelectionPin::GetPinStatusIconVisibility)
		.ContentPadding(0)
		.OnClicked(this, &SGraphPin_PropertySelectionPin::ClickedOnPinStatusIcon)
		[
			SNew(SImage)
			.Image(this, &SGraphPin_PropertySelectionPin::GetPinStatusIcon)
		];

	const TSharedRef<SWidget> LabelWidget = GetLabelWidget(PinLabelStyle);

	// Create the widget used for the pin body (status indicator, label, and value)
	LabelAndValue =
		SNew(SWrapBox)
		.PreferredSize(ConsistentHeight);

		LabelAndValue->AddSlot()
			.VAlign(VAlign_Center)
			[
				LabelWidget
			];

		ValueWidget = GetDefaultValueWidget();

		if (ValueWidget != SNullWidget::NullWidget)
		{
			TSharedPtr<SBox> ValueBox;
			LabelAndValue->AddSlot()
				.Padding(bIsPropertySelection ? FMargin(SideToSideMargin, 0, bCanDrawArrows ? 0 : SideToSideMargin, 0) : FMargin(bCanDrawArrows ? 0 : SideToSideMargin, 0, SideToSideMargin, 0))
				.VAlign(VAlign_Center)
				[
					SAssignNew(ValueBox, SBox)
					.Padding(0.0f)
					.VAlign(VAlign_Center)
					[
						ValueWidget.ToSharedRef()
					]
				];

			if (!DoesWidgetHandleSettingEditingEnabled())
			{
				ValueBox->SetEnabled(TAttribute<bool>(this, &SGraphPin_PropertySelectionPin::IsEditingEnabled));
			}
		}

		LabelAndValue->AddSlot()
			.VAlign(VAlign_Center)
			[
				PinStatusIndicator
			];

	// Create the pin icon widget
	const TSharedRef<SWidget> PinWidgetRef = SPinTypeSelector::ConstructPinTypeImage(
		MakeAttributeSP(this, &SGraphPin_PropertySelectionPin::GetPinIcon),
		MakeAttributeSP(this, &SGraphPin_PropertySelectionPin::GetPinColor),
		MakeAttributeSP(this, &SGraphPin_PropertySelectionPin::GetSecondaryPinIcon),
		MakeAttributeSP(this, &SGraphPin_PropertySelectionPin::GetSecondaryPinColor));
	PinImage = PinWidgetRef;

	PinWidgetRef->SetCursor( 
		TAttribute<TOptional<EMouseCursor::Type>>::Create(
			TAttribute<TOptional<EMouseCursor::Type>>::FGetter::CreateRaw(this,
				&SGraphPin_PropertySelectionPin::GetPinCursor)
		)
	);
	
	TSharedPtr<SHorizontalBox> PinContent;

	if (bIsPropertySelection)
	{
		// Property selection dropdown.
		FullPinHorizontalRowWidget = PinContent = 
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f)
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.HeightOverride(ConsistentHeight)
				.Padding(0.f)
				[
					LabelAndValue.ToSharedRef()
				]
			];
	}
	else if (bIsPrimaryInput)
	{
		// The primary input pin for writing proxy values.
		check(GetDirection() == EGPD_Input);
		
		FullPinHorizontalRowWidget = PinContent = 
			SNew(SHorizontalBox)
			.Visibility(this, &SGraphPin_PropertySelectionPin::GetPinConnectorVisibility)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.Padding(0.0f)
					[
						PinWidgetRef
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(SideToSideMargin, 0, bCanDrawArrows ? 0 : SideToSideMargin, 0))
				[
					LabelWidget
				]
			];
	}
	else
	{
		// Individual connector pins.
		FullPinHorizontalRowWidget = PinContent = 
			SNew(SHorizontalBox)
			.Visibility(this, &SGraphPin_PropertySelectionPin::GetPinConnectorVisibility)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f)
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.HeightOverride(ConsistentHeight)
				[
					PinWidgetRef
				]
			];
	}

	// Draw arrows linking pins (Input -> Output)
	
	if (bCanDrawArrows
		&& (GetDirection() == EGPD_Input
			&& !bIsPrimaryInput
			&& (GetPinConnectorVisibility() == EVisibility::Visible || IsPropertySelected())))
	{
		PinContent->AddSlot()
		.AutoWidth()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(FMargin(8.0f, 0.f, 2.f, 0.0f)) 
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Icons.ArrowRight"))
			.ColorAndOpacity(FSlateColor(FLinearColor::White))
		];
	}

	// Set up a hover for pins that is tinted the color of the pin.
	
	SBorder::Construct(SBorder::FArguments()
		.BorderImage(this, &SGraphPin_PropertySelectionPin::GetPinBorder)
		.BorderBackgroundColor(this, &SGraphPin_PropertySelectionPin::GetHighlightColor)
		.OnMouseButtonDown(this, &SGraphPin_PropertySelectionPin::OnPinNameMouseDown)
		[
			SNew(SBorder)
			.BorderImage(CachedImg_Pin_DiffOutline)
			.BorderBackgroundColor(this, &SGraphPin_PropertySelectionPin::GetPinDiffColor)
			[
				PinContent.ToSharedRef()
			]
		]
	);

	if (ShouldDisplayPin() && InArgs._DisplayPinTooltips)
	{
		const TSharedPtr<IToolTip> TooltipWidget = SNew(SToolTip)
			.Text(this, &SGraphPin_PropertySelectionPin::GetTooltipText);

		SetToolTip(TooltipWidget);
	}
}

FSlateColor SGraphPin_PropertySelectionPin::GetPinColor() const
{
	return PropertySelectionDirection == GetDirection() ? FLinearColor::Transparent
		: SGraphPin::GetPinColor();
}

FSlateColor SGraphPin_PropertySelectionPin::GetHighlightColor() const
{
	if (!ShouldDisplayPin())
	{
		return FLinearColor::Transparent;
	}
	return SGraphPin::GetHighlightColor();
}

TSharedRef<SWidget> SGraphPin_PropertySelectionPin::GetDefaultValueWidget()
{
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.HAlign(HAlign_Fill)
		[
			SAssignNew(PropertyPickerAnchor, SComboButton)
			.ContentPadding(2.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Fill)
			.ButtonStyle(FSMUnrealAppStyle::Get(), "PropertyEditor.AssetComboStyle")
			.ForegroundColor(this, &SGraphPin_PropertySelectionPin::OnGetComboForeground)
			.ButtonColorAndOpacity(this, &SGraphPin_PropertySelectionPin::OnGetWidgetBackground)
			.MenuPlacement(MenuPlacement_BelowAnchor)
			.IsEnabled(this, &SGraphPin_PropertySelectionPin::IsComboEnabled)
			.OnGetMenuContent(this, &SGraphPin_PropertySelectionPin::OnGetMenuContent)
			.OnMenuOpenChanged(OnPropertySelectionOpenChanged)
			.ButtonContent()
			[
				SNew(SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.MinDesiredHeight(25.f)
				.MaxDesiredHeight(MaxPropertySelectionHeight)

				/*
				 * Force set width size to help with alignment and spacing.
				 * 
				 * Ideally, we would have a lower min width (and possibly higher max width) with this element always
				 * taking on the largest neighbor element size. E.g., if this pin is vertical to other pins, and another
				 * pin has a larger size, we should resize to that size as well. Actually achieving that is a task
				 * in itself, and these numbers by and large seem to work well enough for most cases. Removing the min
				 * width smaller entries align to the right, which is okay, but the left space isn't used and looks off.
				 */
				.MinDesiredWidth(MinPropertySelectionWidth)
				.MaxDesiredWidth(MaxPropertySelectionWidth)
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Visibility(this, &SGraphPin_PropertySelectionPin::GetNoPropertySelectedTextVisibility)
						.Text(LOCTEXT("DefaultComboText", "Select Property"))
					]
					+SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.Visibility(this, &SGraphPin_PropertySelectionPin::GetPropertySelectedTextVisibility)
						.ToolTipText_Lambda([this]()
						{
							return FText::Format(LOCTEXT("PropertyTooltipText", "Owner: {0}\nProperty: {1}"), OnGetDefaultComboStateText(), GetPropertyText());
						})
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SScaleBox)
								.Stretch(EStretch::ScaleToFit)
								.StretchDirection(EStretchDirection::DownOnly)
								.HAlign(HAlign_Left)
								[
									SNew(STextBlock)
									.Text(this, &SGraphPin_PropertySelectionPin::OnGetDefaultComboStateText)
									.TextStyle(FSMUnrealAppStyle::Get(), "NormalText.Important")
								]
							]
							+SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.f, 0.f, 0.f, 2.f)
							[
								SNew(SSeparator)
								.Orientation(EOrientation::Orient_Horizontal)
								.Thickness(1.f)
								.ColorAndOpacity(FLinearColor::White)
								.SeparatorImage(FAppStyle::GetBrush("ThinLine.Horizontal"))
							]
							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SScaleBox)
								.Stretch(EStretch::ScaleToFit)
								.StretchDirection(EStretchDirection::DownOnly)
								.HAlign(HAlign_Left)
								[
									SNew(SSMPropertyWidget)
									.Property(this, &SGraphPin_PropertySelectionPin::GetProperty)
									.DisplayText(this, &SGraphPin_PropertySelectionPin::GetPropertyText)
								]
							]
						]
					]
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.Padding(4.f, 0.f, 0.f, 0.f)
		[
			ConstructGoToButton()
		];
}

FReply SGraphPin_PropertySelectionPin::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (OnMouseButtonDownEvent.IsBound())
	{
		const FReply Reply = OnMouseButtonDownEvent.Execute(MyGeometry, MouseEvent);
		if (Reply.IsEventHandled())
		{
			return Reply;
		}
	}
	return SGraphPin::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SGraphPin_PropertySelectionPin::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton
		&& OnContextMenuOpening.IsBound())
	{
		if (const TSharedPtr<SWidget> ContextMenuWidget = OnContextMenuOpening.Execute())
		{
			FSlateApplication::Get().PushMenu(
				SharedThis(this),
				FWidgetPath(),
				ContextMenuWidget.ToSharedRef(),
				MouseEvent.GetScreenSpacePosition(),
				FPopupTransitionEffect::ContextMenu
			);
			return FReply::Handled();
		}
	}
	
	return SGraphPin::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SGraphPin_PropertySelectionPin::OnPropertySelected(TSharedPtr<FSMStateTreeItem> InSelectedItem)
{
	ON_SCOPE_EXIT
	{
		PropertyPickerAnchor->SetIsOpen(false);
	};
	
	if (!GraphPinObj || GraphPinObj->IsPendingKill())
	{
		return;
	}

	if (!InSelectedItem.IsValid())
	{
		const FScopedTransaction Transaction(LOCTEXT("ClearPinValue", "Clear Property"));
		GraphPinObj->Modify();
		GraphPinObj->ResetDefaultValue();
		return;
	}

	using namespace LD::Editor::PropertyUtils::IO;
	
	if (const TSharedPtr<FPropertyData_Base> SelectedPropertyData = ConvertTreeItemToPropertyData(
		InSelectedItem))
	{
		const FString SerializedPropertyName = SerializePropertyDataToString(SelectedPropertyData);
		if (!GraphPinObj->GetDefaultAsString().Equals(SerializedPropertyName))
		{
			const FScopedTransaction Transaction(LOCTEXT("ChangePinValue", "Select Property"));
			GraphPinObj->Modify();
			if (SelectedPropertyData->IsA<FNodePropertyData>())
			{
				// Connection to another state property.
				ensure(TryMakePropertyConnection(GraphPinObj, SelectedPropertyData));
			}
			else if (USMGraphK2Node_PropertyIOBase* PropertyNode = Cast<USMGraphK2Node_PropertyIOBase>(GraphPinObj->GetOwningNode()))
			{
				// Setting BP Owning variable.
				PropertyNode->SetPropertySelectionPinValue(GraphPinObj, SelectedPropertyData);
			}
		}
	}
}

const FProperty* SGraphPin_PropertySelectionPin::GetProperty() const
{
	TryUpdateCache();
	if (CachedPropertyData.IsValid())
	{
		return CachedPropertyData->Property;
	}
	return nullptr;
}

FText SGraphPin_PropertySelectionPin::GetPropertyText() const
{
	TryUpdateCache();
	return GetFormattedPropertyDisplayName(CachedPropertyData);
}

bool SGraphPin_PropertySelectionPin::ShouldDisplayPin() const
{
	return PropertySelectionDirection == GetDirection() && GraphPinObj && !GraphPinObj->bNotConnectable;
}

bool SGraphPin_PropertySelectionPin::CanPinBeEdited() const
{
	return GraphPinObj
		&& (GraphPinObj->GetOwningNodeUnchecked() && GraphPinObj->GetOwningNodeUnchecked()->GetGraph()
			&& !FSMBlueprintEditorUtils::IsGraphReadOnlyForAnyReason(GraphPinObj->GetOwningNodeUnchecked()->GetGraph()));
}

EVisibility SGraphPin_PropertySelectionPin::GetPinConnectorVisibility() const
{
	// We use bNotConnectable rather than bHidden, because bHidden will collapse the pin and we don't want that. The engine
	// does this under IsPinVisibleAsAdvanced which isn't virtual.
	return PropertySelectionDirection != GetDirection() && GraphPinObj && !GraphPinObj->bNotConnectable ? EVisibility::Visible : EVisibility::Hidden;
}

TSharedRef<SWidget> SGraphPin_PropertySelectionPin::ConstructGoToButton()
{
	using namespace LD::Editor::PropertyUtils::IO;
	return SNew(SButton)
	.ButtonStyle(FSMUnrealAppStyle::Get(), "SimpleButton")
	.Visibility_Lambda([this]()
	{
		if (const TSharedPtr<FPropertyData_Base> PropertyData = GetPropertyDataFromPin(GraphPinObj))
		{
			if (PropertyData->PropertyOwnerType == EPropertyOwnerType::NodeInstance)
			{
				return EVisibility::Visible;
			}
		}
		return EVisibility::Hidden;
	})
	.OnClicked(this, &SGraphPin_PropertySelectionPin::OnGoToButtonClicked)
	.ToolTipText(LOCTEXT("GoToProperty", "Go to the referenced property."))
	.ContentPadding(0)
	[
		SNew(SImage)
		.Image(FSMUnrealAppStyle::Get().GetBrush("Icons.BrowseContent"))
		.ColorAndOpacity(FSlateColor::UseForeground())
	];
}

FReply SGraphPin_PropertySelectionPin::OnGoToButtonClicked()
{
	using namespace LD::Editor::PropertyUtils::IO;
	if (const TSharedPtr<FPropertyData_Base> PropertyData = GetPropertyDataFromPin(GraphPinObj))
	{
		if (USMGraphK2Node_PropertyNode_Base* PropertyNode = GetPropertyNode(PropertyData))
		{
			if (const USMGraphNode_StateNodeBase* StateNode = Cast<USMGraphNode_StateNodeBase>(PropertyNode->GetOwningGraphNode()))
			{
				FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(StateNode);

				// Display input if we're output, or display output if we're input.
				const ESMPropertyInteractionType CorrespondingDirection = PropertySelectionDirection == EGPD_Input
				? ESMPropertyInteractionType::SetDisplayOutputConnector
				: ESMPropertyInteractionType::SetDisplayInputConnector;

				// Selecting the node highlights the property, but doesn't guarantee the other connector is displayed, so
				// we send both events.
				const ESMPropertyInteractionType InteractionType = ESMPropertyInteractionType::SetSelect |
					CorrespondingDirection;
				const FSMStateMachineBlueprintEditor* BPEditor = FSMBlueprintEditorUtils::GetStateMachineEditor(StateNode);

				const TSharedPtr<SGraphPanel> GraphPanel = BPEditor ? BPEditor->GetFocusedGraphPanel() : nullptr;
				
				FSMPropertyInteractionManager::FInteractionArgs Args
				= FSMPropertyInteractionManager::BuildInteractionArgsForExternalSelections(InteractionType,
					GraphPanel.IsValid() ? GraphPanel.ToWeakPtr() : TOptional<TWeakPtr<SGraphPanel>>());
				Args.bExpandCategory = true;
				StateNode->GetPropertyInteractionManager().SetPropertyInteraction(PropertyNode, InteractionType, Args);
			}
		}
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SGraphPin_PropertySelectionPin::OnGetMenuContent()
{
	using namespace LD::Editor::PropertyUtils::IO;
	
	UEdGraphNode* OwningNode = GraphPinObj->GetOwningNode();
	if (const USMBlueprint* Blueprint = FSMBlueprintEditorUtils::FindBlueprintFromObject(OwningNode))
	{
		const FPropertyFilter Filter = BuildPropertySelectionFilter();
		const TSharedPtr<SSMStatePropertySelectionTreeView> TreeView = SNew(SSMStatePropertySelectionTreeView, Blueprint->GetGeneratedClass())
			.OnPropertySelected(this, &SGraphPin_PropertySelectionPin::OnPropertySelected)
			.PropertyFilter(Filter);

		if (TreeView->GetFilteredTreeItems().Num() == 0)
		{
			// Display message when no properties available.
			const ESMGraphPropertyFlags Flags = Filter.GraphPropertyFlags.Get(ESMGraphPropertyFlags::None);
			if (!Filter.PropertyOwnerTypes.Contains(EPropertyOwnerType::OwningBlueprint)
				&& EnumHasAnyFlags(Flags, ESMGraphPropertyFlags::Output) && !EnumHasAnyFlags(Flags, ESMGraphPropertyFlags::Input))
			{
				return SNew(STextBlock)
					.Text(LOCTEXT("PropertySelectionNoOutputProperties", "No Available Output Properties"));
			}
			else if (!Filter.PropertyOwnerTypes.Contains(EPropertyOwnerType::OwningBlueprint)
				&& EnumHasAnyFlags(Flags, ESMGraphPropertyFlags::Input) && !EnumHasAnyFlags(Flags, ESMGraphPropertyFlags::Output))
			{
				return SNew(STextBlock)
					.Text(LOCTEXT("PropertySelectionNoInputProperties", "No Available Input Properties"));
			}
			else
			{
				return SNew(STextBlock)
					.Text(LOCTEXT("PropertySelectionNoProperties", "No Available Properties"));
			}
		}
		
		return TreeView.ToSharedRef();
	}

	return SNullWidget::NullWidget;
}

FText SGraphPin_PropertySelectionPin::OnGetDefaultComboStateText() const
{
	TryUpdateCache();
	return GetPropertyOwnerDisplayName(CachedPropertyData);
}

bool SGraphPin_PropertySelectionPin::IsComboEnabled() const
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	return true;
}

EVisibility SGraphPin_PropertySelectionPin::GetNoPropertySelectedTextVisibility() const
{
	return IsPropertySelected() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility SGraphPin_PropertySelectionPin::GetPropertySelectedTextVisibility() const
{
	return IsPropertySelected() ? EVisibility::Visible : EVisibility::Collapsed;
}

FSlateColor SGraphPin_PropertySelectionPin::OnGetComboForeground() const
{
	const float Alpha = (IsHovered() || bOnlyShowDefaultValue)
						? ActiveComboAlpha
						: InActiveComboAlpha;
	return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Alpha));
}

FSlateColor SGraphPin_PropertySelectionPin::OnGetWidgetBackground() const
{
	const float Alpha = (IsHovered() || bOnlyShowDefaultValue)
						? ActivePinBackgroundAlpha
						: InactivePinBackgroundAlpha;
	return FSlateColor(FLinearColor(1.f, 1.f, 1.f, Alpha));
}

TSharedPtr<LD::Editor::PropertyUtils::IO::FPropertyData_Base> SGraphPin_PropertySelectionPin::
ConvertTreeItemToPropertyData(const TSharedPtr<FSMStateTreeItem>& InItem) const
{
	check(InItem.IsValid());

	using namespace LD::Editor::PropertyUtils::IO;

	TSharedPtr<FPropertyData_Base> PropertyData;
	
	if (InItem->GetTypeName() == FSMStateTreeGraphPropertyItem::StaticTypeName())
	{
		check(InItem->StateNode.IsValid());
		
		const FSMStateTreeGraphPropertyItemPtr GraphPropertyItemPtr = StaticCastSharedPtr<FSMStateTreeGraphPropertyItem>(InItem);
		check(GraphPropertyItemPtr->PropertyNode.IsValid());
	
		bool bIsRuntimeGuid = false;
		const FGuid& NodeGuid = InItem->StateNode->GetCorrectNodeGuid(&bIsRuntimeGuid);
		check(bIsRuntimeGuid);

		// Owning template is only available on parent. Not sure if this should be set on the property item, but this
		// behavior hasn't changed in a long time.
		const FGuid TemplateGuid = InItem->Parent.IsValid() && InItem->Parent.Pin()->NodeInstance.IsValid()
		? InItem->Parent.Pin()->NodeInstance->GetTemplateGuid() : FGuid();

		const FProperty* Property = GraphPropertyItemPtr->PropertyNode->GetProperty();
		check(Property);

		const FSMGraphProperty_Base* PropertyNode = GraphPropertyItemPtr->PropertyNode->GetPropertyNodeConstChecked();
		int32 ArrayIndex = PropertyNode->bIsInArray ? PropertyNode->ArrayIndex : INDEX_NONE;

		PropertyData = MakeShared<FNodePropertyData>(NodeGuid, TemplateGuid, Property, ArrayIndex);
	}
	else if (InItem->GetTypeName() == FSMStateTreeStandardPropertyItem::StaticTypeName())
	{
		const FSMStateTreeStandardPropertyItemPtr GraphPropertyItemPtr = StaticCastSharedPtr<FSMStateTreeStandardPropertyItem>(InItem);
		check(GraphPropertyItemPtr->Property);

		PropertyData = MakeShared<FOwningBlueprintPropertyData>(GraphPropertyItemPtr->Property);
	}

	check(PropertyData.IsValid());
	PropertyData->SourceObject = GraphPinObj->GetOwningNode();
	
	return PropertyData;
}

bool SGraphPin_PropertySelectionPin::TryUpdateCache() const
{
	if (GraphPinObj && !GraphPinObj->bWasTrashed)
	{
		const FString NewValue = GraphPinObj->GetDefaultAsString();
		if (CachedPropertyString != NewValue)
		{
			CachedPropertyString = NewValue;
			CachedPropertyData = LD::Editor::PropertyUtils::IO::DeserializeStringToPropertyData(NewValue);
			if (CachedPropertyData.IsValid())
			{
				CachedPropertyData->SourceObject = GraphPinObj->GetOwningNode();
			}
			return true;
		}
	}

	return false;
}

LD::Editor::PropertyUtils::IO::FPropertyFilter SGraphPin_PropertySelectionPin::BuildPropertySelectionFilter() const
{
	using namespace LD::Editor::PropertyUtils::IO;
	FPropertyFilter Filter;

	if (GraphPinObj)
	{
		if (USMGraphK2Node_PropertyIOWriterNode* WriterNode = Cast<USMGraphK2Node_PropertyIOWriterNode>(GraphPinObj->GetOwningNodeUnchecked()))
		{
			// Input, accept all properties.

			Filter.GraphPropertyFlags = ESMGraphPropertyFlags::Input;
			Filter.OnFilterGraphProperty.BindLambda([](const FSMGraphProperty_Base& GraphProperty)
			{
				// Make sure it's possible the graph property could be configured for output. Filters out text graph properties.
				return !GraphProperty.CanAcceptPropertyFlag(ESMGraphPropertyFlags::Output)
					|| GraphProperty.IsVariableReadOnly();
			});
		}
		else if (USMGraphK2Node_PropertyIOReaderNode* ReaderNode = Cast<USMGraphK2Node_PropertyIOReaderNode>(GraphPinObj->GetOwningNodeUnchecked()))
		{
			// Output, node variables only.
			
			Filter.GraphPropertyFlags = ESMGraphPropertyFlags::Output;
			Filter.PropertyOwnerTypes.Add(EPropertyOwnerType::NodeInstance);
			Filter.OnFilterGraphProperty.BindLambda([](const FSMGraphProperty_Base& GraphProperty)
			{
				return GraphProperty.IsVariableReadOnly();
			});
		}
	}

	// No reason to output to redirect map currently. May want to make a standardized location for gathering
	// excluded properties.
	FProperty* GuidRedirectMapProperty = FindFieldChecked<FProperty>(USMInstance::StaticClass(), "PathGuidRedirectMap");
	Filter.ExcludeProperties.Add(GuidRedirectMapProperty);
	
	return Filter;
}

#undef LOCTEXT_NAMESPACE
