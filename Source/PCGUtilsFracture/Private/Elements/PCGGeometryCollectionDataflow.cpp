// Copyright Max Harris
#include "Elements/PCGGeometryCollectionDataflow.h"
#include "Data/PCGGeometryCollectionData.h"
#include "PCGContext.h"
#include "PCGNode.h"
#include "PCGModule.h"

#if WITH_EDITOR
#include "Dataflow/PCGUtilsDataflowContext.h"
#include "Dataflow/PCGUtilsDataflowNodes.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowGraph.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGUtilsGeometryCollectionRevisionPublisher.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "PCGParamData.h"
#include "UObject/StrongObjectPtr.h"
#endif

#define LOCTEXT_NAMESPACE "PCGGeometryCollectionDataflow"

int32 PCGUtilsDataflow::GetBatchCount(TConstArrayView<int32> Counts)
{
	int32 Count = 1;
	for (int32 Value : Counts) Count = FMath::Max(Count, Value);
	for (int32 Value : Counts) if (Value != 1 && Value != Count) return 0;
	return Count;
}

TArray<FPCGPinProperties> UPCGGeometryCollectionDataflowSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	for (const auto& Input : Inputs)
	{
		if (Input.Name.IsNone() || Input.Name == TEXT("Parameters") || Pins.ContainsByPredicate([&](const auto& P) { return P.Label == Input.Name; })) continue;
		if (Input.Type == EPCGUtilsDataflowInputType::GeometryCollection)
			Pins.Emplace_GetRef(Input.Name, FPCGGeometryCollectionDataTypeInfo::AsId(), true, true).SetRequiredPin();
		else Pins.Emplace_GetRef(Input.Name, EPCGDataType::Point, true, true).SetRequiredPin();
	}
	Pins.Emplace(TEXT("Parameters"), EPCGDataType::Param, true, true);
	return Pins;
}

TArray<FPCGPinProperties> UPCGGeometryCollectionDataflowSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	for (FName Name : Outputs)
		if (!Name.IsNone() && !Pins.ContainsByPredicate([&](const auto& P) { return P.Label == Name; }))
			Pins.Emplace(Name, FPCGGeometryCollectionDataTypeInfo::AsId(), true, true);
	return Pins;
}

FPCGElementPtr UPCGGeometryCollectionDataflowSettings::CreateElement() const
{
	return MakeShared<FPCGGeometryCollectionDataflowElement>();
}

void UPCGGeometryCollectionDataflowSettings::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FDataflowAssetDelegates::OnVariablesChanged.AddUObject(this, &UPCGGeometryCollectionDataflowSettings::OnDataflowVariablesChanged);
		FDataflowNodeDelegates::OnNodeInvalidated.AddUObject(this, &UPCGGeometryCollectionDataflowSettings::OnDataflowNodeInvalidated);
	}
#endif
}

void UPCGGeometryCollectionDataflowSettings::BeginDestroy()
{
#if WITH_EDITOR
	FDataflowAssetDelegates::OnVariablesChanged.RemoveAll(this);
	FDataflowNodeDelegates::OnNodeInvalidated.RemoveAll(this);
#endif
	Super::BeginDestroy();
}

#if WITH_EDITOR
namespace
{
	using EParameter = EPCGUtilsDataflowParameterType;

	// Validate the untrusted graph boundary before CopyTo or publisher routines index collection arrays.
	bool CopyCollectionResult(const FManagedArrayCollection& Source, FGeometryCollection& Dest, FString& Error)
	{
		for (FName Group : Dest.GroupNames())
		{
			for (FName Attribute : Dest.AttributeNames(Group))
			{
				if ((Source.NumElements(Group) > 0 && !Source.HasAttribute(Attribute, Group)) ||
					(Source.HasAttribute(Attribute, Group) && Source.GetAttributeType(Attribute, Group) != Dest.GetAttributeType(Attribute, Group)))
				{
					Error = FString::Printf(TEXT("Dataflow returned an incompatible GC attribute: %s.%s."), *Group.ToString(), *Attribute.ToString());
					return false;
				}
			}
		}
		Source.CopyTo(&Dest);
		const int32 Bones = Dest.Transform.Num(), Geometries = Dest.TransformIndex.Num();
		const int32 Vertices = Dest.Vertex.Num(), Faces = Dest.Indices.Num();
		auto Invalid = [&]() { Error = TEXT("Dataflow returned invalid GC hierarchy, geometry ranges, or indices."); return false; };
		TArray<uint8> State;
		State.Init(0, Bones);
		for (int32 Bone = 0; Bone < Bones; ++Bone)
		{
			if (Dest.Transform[Bone].ContainsNaN()) return Invalid();
			const int32 Parent = Dest.Parent[Bone], Geometry = Dest.TransformToGeometryIndex[Bone];
			if (Parent < INDEX_NONE || Parent >= Bones || Parent == Bone || Geometry < INDEX_NONE || Geometry >= Geometries) return Invalid();
			if (Parent != INDEX_NONE && !Dest.Children[Parent].Contains(Bone)) return Invalid();
			for (int32 Child : Dest.Children[Bone])
				if (Child < 0 || Child >= Bones || Dest.Parent[Child] != Bone) return Invalid();
			// Three-colour parent walk detects cycles in linear time, without recursive stack growth.
			int32 Cursor = Bone;
			while (Cursor != INDEX_NONE && State[Cursor] == 0)
			{
				State[Cursor] = 1;
				Cursor = Dest.Parent[Cursor];
				if (Cursor < INDEX_NONE || Cursor >= Bones) return Invalid();
			}
			if (Cursor != INDEX_NONE && State[Cursor] == 1) return Invalid();
			Cursor = Bone;
			while (Cursor != INDEX_NONE && State[Cursor] == 1) { State[Cursor] = 2; Cursor = Dest.Parent[Cursor]; }
		}
		for (int32 G = 0; G < Geometries; ++G)
		{
			const int32 Bone = Dest.TransformIndex[G];
			if (Bone < 0 || Bone >= Bones || Dest.TransformToGeometryIndex[Bone] != G ||
				Dest.VertexStart[G] < 0 || Dest.VertexCount[G] < 0 || int64(Dest.VertexStart[G]) + Dest.VertexCount[G] > Vertices ||
				Dest.FaceStart[G] < 0 || Dest.FaceCount[G] < 0 || int64(Dest.FaceStart[G]) + Dest.FaceCount[G] > Faces) return Invalid();
		}
		for (int32 V = 0; V < Vertices; ++V)
			if (Dest.BoneMap[V] < 0 || Dest.BoneMap[V] >= Bones || Dest.Vertex[V].ContainsNaN()) return Invalid();
		for (int32 Face = 0; Face < Faces; ++Face)
		{
			for (int32 Corner = 0; Corner < 3; ++Corner)
				if (Dest.Indices[Face][Corner] < 0 || Dest.Indices[Face][Corner] >= Vertices) return Invalid();
			if (Dest.MaterialID[Face] < 0) return Invalid();
		}
		return true;
	}
	EParameter ParameterType(const FPropertyBagPropertyDesc& Desc)
	{
		if (!Desc.ContainerTypes.IsEmpty()) return EParameter::Unsupported;
		switch (Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool: return EParameter::Bool;
		case EPropertyBagPropertyType::Int32: return EParameter::Int32;
		case EPropertyBagPropertyType::Int64: return EParameter::Int64;
		case EPropertyBagPropertyType::Float: return EParameter::Float;
		case EPropertyBagPropertyType::Double: return EParameter::Double;
		case EPropertyBagPropertyType::Name: return EParameter::Name;
		case EPropertyBagPropertyType::String: return EParameter::String;
		default: return EParameter::Unsupported;
		}
	}

	void ReadDefault(FPCGUtilsDataflowParameter& P, const FInstancedPropertyBag& Bag)
	{
		switch (P.Type)
		{
		case EParameter::Bool: P.BoolValue = Bag.GetValueBool(P.Name).GetValue(); break;
		case EParameter::Int32: case EParameter::Int64: P.IntValue = Bag.GetValueInt64(P.Name).GetValue(); break;
		case EParameter::Float: case EParameter::Double: P.RealValue = Bag.GetValueDouble(P.Name).GetValue(); break;
		case EParameter::Name: P.StringValue = Bag.GetValueName(P.Name).GetValue().ToString(); break;
		case EParameter::String: P.StringValue = Bag.GetValueString(P.Name).GetValue(); break;
		default: break;
		}
	}

	template<typename T>
	bool ReadAttribute(const FPCGMetadataAttributeBase* Attribute, T& Value)
	{
		if (Attribute->GetTypeId() != PCG::Private::MetadataTypes<T>::Id) return false;
		Value = static_cast<const FPCGMetadataAttribute<T>*>(Attribute)->GetValueFromItemKey(0);
		return true;
	}

	bool ApplyParameters(const UPCGGeometryCollectionDataflowSettings& Settings, UDataflow& Asset,
		const UPCGParamData* Data, FDataflowVariableOverrides& Overrides, FString& Error)
	{
		TSet<FName> Names;
		for (auto P : Settings.Parameters)
		{
			const auto* Desc = Asset.Variables.FindPropertyDescByName(P.Name);
			if (!Desc || Names.Contains(P.Name) || ParameterType(*Desc) != P.Type)
			{
				Error = FString::Printf(TEXT("Parameter '%s' is duplicated or its asset definition changed. Refresh Interface."), *P.Name.ToString());
				return false;
			}
			Names.Add(P.Name);
			const auto* Attribute = Data && !P.AttributeName.IsNone() ? Data->Metadata->GetConstAttribute(P.AttributeName) : nullptr;
			if (!Attribute && !P.bOverride) continue;
			bool bValid = true;
			if (Attribute)
			{
				switch (P.Type)
				{
				case EParameter::Bool: bValid = ReadAttribute(Attribute, P.BoolValue); break;
				case EParameter::Int32: case EParameter::Int64:
				{
					int32 Small = 0;
					bValid = ReadAttribute(Attribute, P.IntValue);
					if (!bValid && ReadAttribute(Attribute, Small)) { P.IntValue = Small; bValid = true; }
					break;
				}
				case EParameter::Float: case EParameter::Double:
				{
					float Small = 0;
					bValid = ReadAttribute(Attribute, P.RealValue);
					if (!bValid && ReadAttribute(Attribute, Small)) { P.RealValue = Small; bValid = true; }
					break;
				}
				case EParameter::Name:
				{
					FName Name;
					bValid = ReadAttribute(Attribute, Name);
					P.StringValue = Name.ToString();
					break;
				}
				case EParameter::String: bValid = ReadAttribute(Attribute, P.StringValue); break;
				default: bValid = false; break;
				}
			}
			if (P.Type == EParameter::Int32 && (P.IntValue < MIN_int32 || P.IntValue > MAX_int32)) bValid = false;
			if ((P.Type == EParameter::Float || P.Type == EParameter::Double) &&
				(!FMath::IsFinite(P.RealValue) || FMath::Abs(P.RealValue) > MAX_flt)) bValid = false;
			if (bValid)
			{
				switch (P.Type)
				{
				case EParameter::Bool: bValid = Overrides.OverrideVariableBool(P.Name, P.BoolValue); break;
				case EParameter::Int32: case EParameter::Int64: bValid = Overrides.OverrideVariableInt(P.Name, P.IntValue); break;
				// Dataflow's Double variable getter publishes float in UE 5.8, matching its numeric pins.
				case EParameter::Float: case EParameter::Double: bValid = Overrides.OverrideVariableFloat(P.Name, static_cast<float>(P.RealValue)); break;
				case EParameter::Name: bValid = Overrides.OverrideVariableName(P.Name, FName(P.StringValue)); break;
				case EParameter::String: bValid = Overrides.OverrideVariableString(P.Name, P.StringValue); break;
				default: bValid = false; break;
				}
			}
			if (!bValid)
			{
				Error = FString::Printf(TEXT("Parameter '%s' (attribute '%s') expects %s; the supplied type or value is incompatible."),
					*P.Name.ToString(), *P.AttributeName.ToString(), *StaticEnum<EParameter>()->GetNameStringByValue(static_cast<int64>(P.Type)));
				return false;
			}
		}
		return true;
	}
}

FText UPCGGeometryCollectionDataflowSettings::GetDefaultNodeTitle() const { return LOCTEXT("Title", "GC | Dataflow Processor"); }
FText UPCGGeometryCollectionDataflowSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Experimental editor-only Dataflow asset evaluation. Named inputs pair N:N or broadcast N:1. Every GC output starts a new lineage. No selections or cached evaluations.");
}
EPCGChangeType UPCGGeometryCollectionDataflowSettings::GetChangeTypeForProperty(const FName& PropertyName) const
{
	return Super::GetChangeTypeForProperty(PropertyName) | EPCGChangeType::Structural;
}
void UPCGGeometryCollectionDataflowSettings::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UPCGGeometryCollectionDataflowSettings, DataflowAsset)) RefreshInterface();
	Super::PostEditChangeProperty(Event);
}

void UPCGGeometryCollectionDataflowSettings::GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	if (!DataflowAsset.IsNull()) OutKeysToSettings.FindOrAdd(FPCGSelectionKey::CreateFromPath(DataflowAsset.ToSoftObjectPath())).Emplace(this, false);
}

void UPCGGeometryCollectionDataflowSettings::OnDataflowVariablesChanged(const UDataflow* Asset, FName Variable)
{
	if (Asset != DataflowAsset.Get()) return;
	// Keep names and inline values current, without changing the explicitly authored PCG pin interface.
	TArray<FPCGUtilsDataflowParameter> Updated;
	if (const auto* Bag = Asset->Variables.GetPropertyBagStruct())
	{
		for (const auto& Desc : Bag->GetPropertyDescs())
		{
			auto& P = Updated.Emplace_GetRef();
			P.Name = Desc.Name; P.Type = ParameterType(Desc); P.AttributeName = P.Name;
			if (const auto* Previous = Parameters.FindByPredicate([&](const auto& Old) { return Old.Name == P.Name && Old.Type == P.Type; })) P = *Previous;
			if (!P.bOverride) ReadDefault(P, Asset->Variables);
			P.Status = P.Type == EParameter::Unsupported ? TEXT("Unsupported inline/metadata type; uses asset default.") : TEXT("");
		}
	}
	Parameters = MoveTemp(Updated);
	OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Settings);
}

void UPCGGeometryCollectionDataflowSettings::OnDataflowNodeInvalidated(FDataflowNode& Node)
{
	const auto* Asset = Cast<UDataflow>(DataflowAsset.Get());
	if (Asset && Asset->GetDataflow() && Asset->GetDataflow()->GetNodes().ContainsByPredicate([&](const auto& Entry) { return Entry.Get() == &Node; }))
		OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Settings);
}
#endif

void UPCGGeometryCollectionDataflowSettings::RefreshInterface()
{
#if WITH_EDITOR
	UDataflow* Asset = Cast<UDataflow>(DataflowAsset.LoadSynchronous());
	if (!Asset || !Asset->GetDataflow()) return;
	Modify();
	Inputs.Reset(); Outputs.Reset();
	for (const auto& Node : Asset->GetDataflow()->GetNodes())
	{
		if (Node->GetType() == FPCGUtilsDataflowCollectionInputNode::StaticType())
		{
			auto& Input = Inputs.Emplace_GetRef();
			Input.Name = static_cast<const FPCGUtilsDataflowCollectionInputNode&>(*Node).PinName;
		}
		else if (Node->GetType() == FPCGUtilsDataflowPointsInputNode::StaticType())
		{
			auto& Input = Inputs.Emplace_GetRef();
			Input.Name = static_cast<const FPCGUtilsDataflowPointsInputNode&>(*Node).PinName;
			Input.Type = EPCGUtilsDataflowInputType::Points;
		}
		else if (Node->GetType() == FPCGUtilsDataflowCollectionOutputNode::StaticType())
			Outputs.Add(static_cast<const FPCGUtilsDataflowCollectionOutputNode&>(*Node).PinName);
	}
	OnDataflowVariablesChanged(Asset, NAME_None);
	OnSettingsChangedDelegate.Broadcast(this, EPCGChangeType::Structural | EPCGChangeType::Settings);
#endif
}

bool FPCGGeometryCollectionDataflowElement::ExecuteInternal(FPCGContext* Context) const
{
#if !WITH_EDITOR
	PCGE_LOG_C(Error, GraphAndLog, Context, LOCTEXT("EditorOnly", "Dataflow Processor is editor-only and produces no output in packaged builds."));
	return true;
#else
	const auto* Settings = Context->GetInputSettings<UPCGGeometryCollectionDataflowSettings>();
	auto Fail = [&](const FString& Message)
	{
		PCGE_LOG_C(Error, GraphAndLog, Context, FText::FromString(Message));
		return true;
	};
	TStrongObjectPtr<UDataflow> Asset(Cast<UDataflow>(Settings->DataflowAsset.LoadSynchronous()));
	if (!Asset || !Asset->GetDataflow()) return Fail(TEXT("Assign a Dataflow asset and Refresh Interface."));
	if (Settings->Outputs.IsEmpty()) return Fail(TEXT("At least one named GC output is required."));

	TMap<FName, EPCGUtilsDataflowInputType> BridgeInputs;
	TMap<FName, const FPCGUtilsDataflowCollectionOutputNode*> BridgeOutputs;
	for (const auto& Node : Asset->GetDataflow()->GetNodes())
	{
		for (const auto* Output : Node->GetOutputs())
			if (Output->HasFrozenValue()) return Fail(TEXT("Unfreeze Dataflow outputs before processing: frozen values bypass fresh execution contexts."));
		FName Name;
		EPCGUtilsDataflowInputType Type = EPCGUtilsDataflowInputType::GeometryCollection;
		if (Node->GetType() == FPCGUtilsDataflowCollectionInputNode::StaticType())
			Name = static_cast<const FPCGUtilsDataflowCollectionInputNode&>(*Node).PinName;
		else if (Node->GetType() == FPCGUtilsDataflowPointsInputNode::StaticType())
		{
			Name = static_cast<const FPCGUtilsDataflowPointsInputNode&>(*Node).PinName;
			Type = EPCGUtilsDataflowInputType::Points;
		}
		else if (Node->GetType() == FPCGUtilsDataflowCollectionOutputNode::StaticType())
		{
			const auto* Output = static_cast<const FPCGUtilsDataflowCollectionOutputNode*>(Node.Get());
			if (Output->PinName.IsNone() || BridgeOutputs.Contains(Output->PinName)) return Fail(TEXT("Dataflow GC output names must be nonempty and unique."));
			if (!Output->FindInput(FName(TEXT("Collection")))->IsConnected()) return Fail(TEXT("Every PCG Output GC bridge must have Collection connected."));
			BridgeOutputs.Add(Output->PinName, Output);
			continue;
		}
		else continue;
		if (Name.IsNone() || Name == TEXT("Parameters") || BridgeInputs.Contains(Name)) return Fail(TEXT("Dataflow input names must be unique, nonempty, and cannot be Parameters."));
		BridgeInputs.Add(Name, Type);
	}
	if (BridgeInputs.Num() != Settings->Inputs.Num() || BridgeOutputs.Num() != Settings->Outputs.Num())
		return Fail(TEXT("Dataflow interface differs from the PCG pins. Refresh Interface and reconnect renamed pins."));

	TSet<FName> Seen;
	TArray<TArray<FPCGTaggedData>> InputData;
	TArray<int32> Counts;
	for (const auto& Input : Settings->Inputs)
	{
		const auto* Type = BridgeInputs.Find(Input.Name);
		if (!Type || *Type != Input.Type || Seen.Contains(Input.Name)) return Fail(TEXT("PCG input names/types do not match the Dataflow bridge. Refresh Interface."));
		Seen.Add(Input.Name);
		auto& Data = InputData.Add_GetRef(Context->InputData.GetInputsByPin(Input.Name));
		Counts.Add(Data.Num());
		for (const auto& Item : Data)
		{
			if (Input.Type == EPCGUtilsDataflowInputType::GeometryCollection)
			{
				const auto* Collection = Cast<UPCGGeometryCollectionData>(Item.Data);
				if (!Collection || !Collection->HasCollection()) return Fail(FString::Printf(TEXT("Input '%s' requires GC data."), *Input.Name.ToString()));
			}
			else if (!Cast<UPCGBasePointData>(Item.Data)) return Fail(FString::Printf(TEXT("Input '%s' requires point data."), *Input.Name.ToString()));
		}
	}
	Seen.Reset();
	for (FName Name : Settings->Outputs)
	{
		if (!BridgeOutputs.Contains(Name) || Seen.Contains(Name)) return Fail(TEXT("PCG output names do not match the Dataflow bridge. Refresh Interface."));
		Seen.Add(Name);
	}
	const auto ParamData = Context->InputData.GetInputsByPin(TEXT("Parameters"));
	const bool bParametersConnected = !ParamData.IsEmpty() || (Context->Node && Context->Node->IsInputPinConnected(TEXT("Parameters")));
	if (bParametersConnected) Counts.Add(ParamData.Num());
	for (const auto& Item : ParamData)
	{
		const auto* Param = Cast<UPCGParamData>(Item.Data);
		if (!Param || !Param->Metadata || Param->Metadata->GetItemCountForChild() != 1)
			return Fail(TEXT("Each Parameters dataset must contain exactly one metadata row."));
	}
	const int32 BatchCount = PCGUtilsDataflow::GetBatchCount(Counts);
	if (!BatchCount) return Fail(TEXT("Input dataset counts must be N:N or N:1. Required/connected inputs cannot be empty."));

	// Preflight every parameter row before evaluating any graph. This owner is transient and never saved.
	TArray<TStrongObjectPtr<UGeometryCollection>> Owners;
	for (int32 Batch = 0; Batch < BatchCount; ++Batch)
	{
		TStrongObjectPtr<UGeometryCollection> Owner(NewObject<UGeometryCollection>(GetTransientPackage(), NAME_None, RF_Transient));
		Owner->GetDataflowInstance().SetDataflowAsset(Asset.Get());
		const auto* Params = ParamData.IsEmpty() ? nullptr : Cast<UPCGParamData>(ParamData[ParamData.Num() == 1 ? 0 : Batch].Data);
		FString Error;
		if (!ApplyParameters(*Settings, *Asset, Params, Owner->GetDataflowInstance().GetVariableOverrides(), Error)) return Fail(Error);
		Owners.Add(MoveTemp(Owner));
	}
	const bool bHasPoints = Settings->Inputs.ContainsByPredicate([](const auto& Input) { return Input.Type == EPCGUtilsDataflowInputType::Points; });
	const FTransform ActorTransform = bHasPoints ? PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(Context, nullptr, Settings->bPointsInCollectionSpace) : FTransform::Identity;
	TArray<FPCGTaggedData> Results;
	for (int32 Batch = 0; Batch < BatchCount; ++Batch)
	{
		FPCGUtilsDataflowContext Bridge(Owners[Batch].Get());
		bool bError = false;
		Bridge.GetOnContextLogMulticast().AddLambda([&](const UE::Dataflow::FContext::FLogMessage& Message)
		{
			if (Message.Severity == EMessageSeverity::Error)
			{
				bError = true;
				PCGE_LOG_C(Error, GraphAndLog, Context, Message.Text);
			}
			else if (Message.Severity == EMessageSeverity::Warning) { PCGE_LOG_C(Warning, GraphAndLog, Context, Message.Text); }
		});
		TSet<FString> Tags;
		for (int32 Index = 0; Index < Settings->Inputs.Num(); ++Index)
		{
			const auto& Input = Settings->Inputs[Index];
			const auto& Item = InputData[Index][InputData[Index].Num() == 1 ? 0 : Batch];
			Tags.Append(Item.Tags);
			if (Input.Type == EPCGUtilsDataflowInputType::GeometryCollection)
				Bridge.Collections.Add(Input.Name, CastChecked<UPCGGeometryCollectionData>(Item.Data));
			else
			{
				auto Transforms = CastChecked<UPCGBasePointData>(Item.Data)->GetTransformsCopy();
				for (auto& Transform : Transforms) Transform = Transform.GetRelativeTransform(ActorTransform);
				Bridge.Points.Add(Input.Name, MoveTemp(Transforms));
			}
		}
		for (FName Name : Settings->Outputs)
		{
			const auto* Node = BridgeOutputs[Name];
			const FManagedArrayCollection Empty;
			const auto& Collection = Node->FindOutput(FName(TEXT("Collection")))->GetValue<FManagedArrayCollection>(Bridge, Empty);
			const TArray<TObjectPtr<UMaterialInterface>> EmptyMaterials;
			auto Materials = Node->FindOutput(FName(TEXT("Materials")))->GetValue<TArray<TObjectPtr<UMaterialInterface>>>(Bridge, EmptyMaterials);
			if (bError) return true; // Atomic output: discard earlier batches if any evaluation failed.
			if (!Collection.HasAttribute(TEXT("Transform"), FGeometryCollection::TransformGroup) || !Collection.HasAttribute(TEXT("Vertex"), FGeometryCollection::VerticesGroup))
				return Fail(FString::Printf(TEXT("Dataflow output '%s' is not a geometry collection."), *Name.ToString()));
			TSharedRef<FGeometryCollection> Copy = MakeShared<FGeometryCollection>();
			FString ValidationError;
			if (!CopyCollectionResult(Collection, Copy.Get(), ValidationError)) return Fail(ValidationError);
			// Arbitrary graph processing may duplicate ids; all outputs have independent identity.
			Copy->RemoveAttribute(PCGUtilsGeometryCollectionIdentity::BoneIdAttribute, FGeometryCollection::TransformGroup);
			auto* Output = PCGUtilsGeometryCollectionRevisionPublisher::PublishNewLineage(Context, Copy, MoveTemp(Materials));
			if (!Output) return Fail(TEXT("Unable to publish Dataflow output."));
			auto& Result = Results.Emplace_GetRef();
			Result.Pin = Name; Result.Data = Output; Result.Tags = Tags;
		}
	}
	Context->OutputData.TaggedData.Append(MoveTemp(Results));
	return true;
#endif
}

#undef LOCTEXT_NAMESPACE
