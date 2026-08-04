// Copyright Recursoft LLC. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

enum class ESMGraphPropertyFlags : uint8;
struct FSMGraphProperty_Base;
class USMBlueprint;
class USMNodeInstance;
class USMPropertyGraph;
class USMGraphNode_StateNodeBase;
class USMGraphK2Node_PropertyNode_Base;
class USMGraphK2Node_PropertyIOBase;
class USMGraphK2Node_PropertyIOReaderNode;
class USMGraphK2Node_PropertyIOWriterNode;
class FProperty;
class UEdGraphPin;
class UEdGraph;
class UEdGraphSchema;
class SWidget;

/**
 * Utilities specifically related to data and operation of input/output properties. E.g., IO writer and reader nodes.
 */
namespace LD::Editor::PropertyUtils::IO
{
	enum class EPropertyOwnerType : uint8
	{
		Invalid,
		NodeInstance,
		OwningBlueprint
	};

	/** Filter for locating properties. */
	struct FPropertyFilter
	{
		/** Flags specific to graph properties. */
		TOptional<ESMGraphPropertyFlags> GraphPropertyFlags;
		/** The type(s) of object owning the property. */
		TSet<EPropertyOwnerType> PropertyOwnerTypes;
		/** Specific properties to exclude. */
		TSet<FProperty*> ExcludeProperties;

		DECLARE_DELEGATE_RetVal_OneParam(bool, FOnFilterGraphProperty, const FSMGraphProperty_Base& /* GraphProperty */);
		/** Called on each graph property, return true to filter it out. */
		FOnFilterGraphProperty OnFilterGraphProperty;
	};
	
	struct FNodePropertyData;
	struct FOwningBlueprintPropertyData;
	
	struct FPropertyData_Base : TSharedFromThis<FPropertyData_Base>
	{
		/** Used for casting to children types. */
		EPropertyOwnerType PropertyOwnerType;
		/** The property this is for. */
		const FProperty* Property;
		/**
		 * The source object making the request. This does not have to be the direct owner of the property.
		 * Such as a graph property belonging to a state class would likely have a source of the owning state machine BP.
		 *
		 * This is used to look up the correct blueprint to use.
		 * 
		 * If this is null it is looked up from the Property.
		 */
		UObject* SourceObject;

		virtual ~FPropertyData_Base() = default;
		
		explicit FPropertyData_Base(const EPropertyOwnerType InPropertyType) : PropertyOwnerType(InPropertyType),
		Property(nullptr), SourceObject(nullptr) {}

		FORCEINLINE bool operator==(const FPropertyData_Base& Other) const
		{
			return Equals(Other);
		}

		/** Check equality which can be overloaded. */
		virtual bool Equals(const FPropertyData_Base& Other) const
		{
			return PropertyOwnerType == Other.PropertyOwnerType
				&& Property == Other.Property;
		}
		
		/** Cast to a child type. Returns nullptr if not supported. */
		template<typename T>
		TSharedPtr<T> As()
		{
			static_assert(std::is_base_of_v<FPropertyData_Base, T>, 
			  "T must be derived from FPropertyData_Base");

			switch (PropertyOwnerType)
			{
			case EPropertyOwnerType::NodeInstance:
				if constexpr (std::is_same_v<T, FNodePropertyData>)
				{
					return StaticCastSharedPtr<FNodePropertyData>(SharedThis(this).ToSharedPtr());
				}
				break;

			case EPropertyOwnerType::OwningBlueprint:
				if constexpr (std::is_same_v<T, FOwningBlueprintPropertyData>)
				{
					return StaticCastSharedPtr<FOwningBlueprintPropertyData>(SharedThis(this).ToSharedPtr());
				}
				break;

			default:
				break;
			}

			return nullptr;
		}

		/** Cast to a child type. Cannot fail. */
		template<typename T>
		TSharedPtr<T> AsChecked()
		{
			TSharedPtr<T> Result = As<T>();
			check(Result.IsValid());
			return Result;
		}

		/** Checks if this is of a type T. */
		template<typename T>
		bool IsA()
		{
			return As<T>() != nullptr;
		}
	};
	
	struct FNodePropertyData final : FPropertyData_Base
	{
		FNodePropertyData() : FPropertyData_Base(EPropertyOwnerType::NodeInstance), ArrayIndex(INDEX_NONE) {}
		FNodePropertyData(const FGuid& InNodeGuid,
			const FGuid& InTemplateGuid,
			const FProperty* InProperty,
			const int32 InArrayIndex) : FNodePropertyData()
		{
			NodeGuid = InNodeGuid;
			TemplateGuid = InTemplateGuid;
			Property = InProperty;
			ArrayIndex = InArrayIndex;
		}
		
		FGuid NodeGuid;
		FGuid TemplateGuid;
		int32 ArrayIndex;

		virtual bool Equals(const FPropertyData_Base& Other) const override
		{
			if (Other.PropertyOwnerType != EPropertyOwnerType::NodeInstance)
			{
				return false;
			}
			const FNodePropertyData* OtherNode = static_cast<const FNodePropertyData*>(&Other);
			return FPropertyData_Base::Equals(Other)
				&& NodeGuid == OtherNode->NodeGuid
				&& TemplateGuid == OtherNode->TemplateGuid
				&& ArrayIndex == OtherNode->ArrayIndex;
		}
	};

	struct FOwningBlueprintPropertyData final : FPropertyData_Base
	{
		FOwningBlueprintPropertyData() : FPropertyData_Base(EPropertyOwnerType::OwningBlueprint) {}
		FOwningBlueprintPropertyData(const FProperty* InProperty) : FOwningBlueprintPropertyData()
		{
			Property = InProperty;
		}
	};

	/** Return the current property serializer version. */
	int32 GetPropertySerializerVersion();

	/**
	 * Serializes the path to a property to a string.
	 * @param InPropertyData The property data to serialize.
	 * @param InVarName [Optional] The name of the variable to serialize. Normally looked up from property data, but could be different in case of a rename.
	 */
	FString SerializePropertyDataToString(const TSharedPtr<FPropertyData_Base>& InPropertyData, const TOptional<FName>& InVarName = TOptional<FName>());

	/** Extract property data from a string. */
	TSharedPtr<FPropertyData_Base> DeserializeStringToPropertyData(const FString& InPropertyString);

	/** Extract property data from a pin. */
	SMSYSTEMEDITOR_API TSharedPtr<FPropertyData_Base> GetPropertyDataFromPin(const UEdGraphPin* Pin);

	/**
	 * Retrieve the owning state graph, only valid for node property data.
	 * @param InPropertyData The property data, which should be NodePropertyData.
	 * @param InBlueprint [Optional] If not provided it will be found from the property data.
	 */
	UEdGraph* GetPropertyOwnerStateGraph(const TSharedPtr<FPropertyData_Base>& InPropertyData, USMBlueprint* InBlueprint = nullptr);

	/**
	 * Retrieve the owning property node, only valid for node property data.
	 * @param InPropertyData The property data, which should be NodePropertyData.
	 * @param InBlueprint [Optional] If not provided it will be found from the property data.
	 */
	USMGraphK2Node_PropertyNode_Base* GetPropertyNode(const TSharedPtr<FPropertyData_Base>& InPropertyData, USMBlueprint* InBlueprint = nullptr);
	
	/** Get the display name of the property owner (such as a state or blueprint). */
	FText GetPropertyOwnerDisplayName(const TSharedPtr<FPropertyData_Base>& InPropertyData);

	/**
	 * Get the best display name for a property.
	 * @param InPropertyData The property data.
	 * @param bIncludeOwnerName If the owner of the property should be prefixed, such as the owning state name.
	 * @return The fully formatted display name.
	 */
	FText GetFormattedPropertyDisplayName(const TSharedPtr<FPropertyData_Base>& InPropertyData, bool bIncludeOwnerName = false);
	
	/** Return a connected reader node. */
	SMSYSTEMEDITOR_API USMGraphK2Node_PropertyIOReaderNode* GetConnectedPropertyReaderNode(USMGraphK2Node_PropertyNode_Base* InPropertyNode);
	
	/** Find an existing connected IO reader node or place and connect a new one. */
	USMGraphK2Node_PropertyIOReaderNode* GetOrPlacePropertyReaderNode(USMGraphK2Node_PropertyNode_Base* InPropertyNode, bool* bJustCreated = nullptr);

	/** Return a connected writer node. */
	SMSYSTEMEDITOR_API USMGraphK2Node_PropertyIOWriterNode* GetConnectedPropertyWriterNode(USMGraphK2Node_PropertyNode_Base* InPropertyNode);

	/** Find an existing connected IO writer node or place and connect a new one. This will wire a connection if needed. */
	USMGraphK2Node_PropertyIOWriterNode* GetOrPlacePropertyWriterNode(USMGraphK2Node_PropertyNode_Base* InPropertyNode, bool* bJustCreated = nullptr);
	
	/**
	 * Locate the correct IO property node to connect to another IO property node from a standard property node.
	 * If we pass in a writer node then this function should find the correct reader node. If we pass in a reader node then
	 * we should find the writer node.
	 * 
	 * @param InPropertyNode The standard property node which could point to a reader or writer node.
	 * @param InToOutputProperty The IO node which we want the InPropertyNode to potentially reference. Most likely this
	 * belongs to another property node than InPropertyNode.
	 *
	 * @return The IO node, if it exists, which could be used to reference the InToOutputProperty.
	 */
	USMGraphK2Node_PropertyIOBase* FindPropertyIONodeThatShouldLinkToPropertyOutput(USMGraphK2Node_PropertyNode_Base* InPropertyNode,
		const USMGraphK2Node_PropertyIOBase* InToOutputProperty);

	/** Makes a connection to a property belonging to the owning blueprint. */
	SMSYSTEMEDITOR_API void MakeWriterConnectionToOwningBlueprintProperty(USMGraphK2Node_PropertyIOWriterNode* InWriterNode, const FProperty* InProperty);

	/** Try to establish a connection between two property nodes. Will automatically look for correct pins. */
	SMSYSTEMEDITOR_API bool TryMakePropertyConnection(USMGraphK2Node_PropertyNode_Base* InPropertyWithOutput, USMGraphK2Node_PropertyNode_Base* InPropertyWithInput);
	
	/** Try to establish a connection between an IONode and a property node. This will find or create IO nodes in relation to the property node. */
	SMSYSTEMEDITOR_API bool TryMakePropertyConnection(UEdGraphPin* InFromIONodePropertyPin, USMGraphK2Node_PropertyNode_Base* InToPropertyNode);

	/** Try to establish a connection between an IONode and corresponding property data. This will find or create IO nodes in relation to the property node. */
	SMSYSTEMEDITOR_API bool TryMakePropertyConnection(UEdGraphPin* InFromIONodePropertyPin, const TSharedPtr<FPropertyData_Base>& InToPropertyData, USMBlueprint* InBlueprint = nullptr);
	
	/** Break a connection given an IO node property pin. */
	SMSYSTEMEDITOR_API void BreakPropertyConnection(UEdGraphPin* InFromIONodePropertyPin);

	/** Break all IO connections for a given property output node. */
	SMSYSTEMEDITOR_API void BreakAllPropertyConnections(const USMGraphK2Node_PropertyIOBase* InPropertyNode);
	
	/** Break all IO connections for a given property node. */
	SMSYSTEMEDITOR_API void BreakAllPropertyConnections(USMGraphK2Node_PropertyNode_Base* InPropertyNode);

	/** Find all connected property nodes, both input and output that are connected to the InPropertyNode. */
	TArray<USMGraphK2Node_PropertyNode_Base*> GetAllConnectedPropertyNodes(USMGraphK2Node_PropertyNode_Base* InPropertyNode);
	
	/** Make a node widget for use in a linear expression. */
	TSharedPtr<SWidget> MakeLinearExpressionNodeWidget(const TSharedPtr<FPropertyData_Base>& InPropertyData,
		const UEdGraphSchema* InSchema);
}
