// Copyright Max Harris

#include "Elements/Creation/PCGDynMeshFromStaticMeshPoints.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMeshEditor.h"
#include "Elements/PCGUtilsDynMeshSpaceHelpers.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "Materials/MaterialInterface.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshFromStaticMeshPoints"

// Named, not anonymous: unity builds put this file beside others with helpers of the same names.
namespace PCGDynMeshFromStaticMeshPointsPrivate
{
	using UE::Geometry::FDynamicMesh3;

	/** A static mesh read once, however many points place it, with the materials its sections use. */
	struct FSourceMesh
	{
		bool bRead = false;
		FDynamicMesh3 Mesh;
		TArray<UMaterialInterface*> Materials;
	};

	const FSourceMesh& ReadStaticMesh(FPCGContext* Context, UStaticMesh* StaticMesh, TMap<UStaticMesh*, FSourceMesh>& Cache)
	{
		if (const FSourceMesh* Existing = Cache.Find(StaticMesh))
		{
			return *Existing;
		}
		FSourceMesh& Source = Cache.Add(StaticMesh);

		UDynamicMesh* Converted = FPCGContext::NewObject_AnyThread<UDynamicMesh>(Context);
		FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
		// The source mesh first: it is closed where the render mesh is split along hard edges and UV seams. It is
		// not there in a cooked game, so fall back to the render mesh.
		FGeometryScriptMeshReadLOD RequestedLOD;
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(
			StaticMesh, Converted, AssetOptions, RequestedLOD, Outcome, /*bUseSectionMaterials=*/true);
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			RequestedLOD.LODType = EGeometryScriptLODType::RenderData;
			UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(
				StaticMesh, Converted, AssetOptions, RequestedLOD, Outcome, /*bUseSectionMaterials=*/true);
		}
		if (Outcome != EGeometryScriptOutcomePins::Success)
		{
			return Source;
		}

		Source.bRead = true;
		Source.Mesh = Converted->GetMeshRef();
		TArray<int32> MaterialIndices;
		TArray<FName> SlotNames;
		EGeometryScriptOutcomePins MaterialOutcome = EGeometryScriptOutcomePins::Failure;
		UGeometryScriptLibrary_StaticMeshFunctions::GetSectionMaterialListFromStaticMesh(
			StaticMesh, RequestedLOD, Source.Materials, MaterialIndices, SlotNames, MaterialOutcome);
		return Source;
	}

	/** The attribute on the points when they have it, otherwise on the data itself. Null when neither has it. */
	const FPCGMetadataAttributeBase* FindMeshAttribute(const UPCGMetadata* Metadata, FName Name, bool& bOutOnPoints)
	{
		bOutOnPoints = false;
		if (!Metadata || Name.IsNone())
		{
			return nullptr;
		}
		// Asking a metadata for an attribute in a domain it has never created is logged as an error, so ask for
		// the domain first.
		if (Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Elements))
		{
			if (const FPCGMetadataAttributeBase* OnPoints = Metadata->GetConstAttribute(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Elements)))
			{
				bOutOnPoints = true;
				return OnPoints;
			}
		}
		return Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data)
			? Metadata->GetConstAttribute(FPCGAttributeIdentifier(Name, PCGMetadataDomainID::Data)) : nullptr;
	}

	bool ReadPath(const FPCGMetadataAttributeBase* Attribute, PCGMetadataEntryKey Key, FSoftObjectPath& OutPath)
	{
		switch (Attribute->GetTypeId())
		{
		case PCG::Private::MetadataTypes<FSoftObjectPath>::Id:
			OutPath = static_cast<const FPCGMetadataAttribute<FSoftObjectPath>*>(Attribute)->GetValueFromItemKey(Key);
			return true;
		case PCG::Private::MetadataTypes<FString>::Id:
			OutPath = FSoftObjectPath(static_cast<const FPCGMetadataAttribute<FString>*>(Attribute)->GetValueFromItemKey(Key));
			return true;
		default:
			return false;
		}
	}

	/**
	 * Points a placed mesh's triangles at slots of the output's material list, adding a slot for each material the
	 * list does not have yet. A triangle whose material ID has no material gets a slot with none.
	 */
	void RemapMaterials(FDynamicMesh3& Mesh, const TArray<UMaterialInterface*>& OwnMaterials, TArray<UMaterialInterface*>& OutMaterials)
	{
		if (!Mesh.HasAttributes())
		{
			Mesh.EnableAttributes();
		}
		if (!Mesh.Attributes()->HasMaterialID())
		{
			Mesh.Attributes()->EnableMaterialID();
		}
		UE::Geometry::FDynamicMeshMaterialAttribute* MaterialIDs = Mesh.Attributes()->GetMaterialID();
		TMap<int32, int32> SlotOfID;
		for (const int32 TriangleID : Mesh.TriangleIndicesItr())
		{
			const int32 OwnID = MaterialIDs->GetValue(TriangleID);
			int32* Slot = SlotOfID.Find(OwnID);
			if (!Slot)
			{
				UMaterialInterface* Material = OwnMaterials.IsValidIndex(OwnID) ? OwnMaterials[OwnID] : nullptr;
				const int32 Found = OutMaterials.IndexOfByKey(Material);
				Slot = &SlotOfID.Add(OwnID, Found != INDEX_NONE ? Found : OutMaterials.Add(Material));
			}
			MaterialIDs->SetValue(TriangleID, *Slot);
		}
	}

	/** A mesh has no points, so of the point data's metadata only the dataset-wide @Data domain carries over. */
	void InheritDataDomain(UPCGDynamicMeshData* OutputData, const UPCGBasePointData* PointData)
	{
		const FPCGMetadataDomain* SourceDomain = PointData->ConstMetadata()
			? PointData->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		FPCGMetadataDomain* OutputDomain = OutputData->MutableMetadata()
			? OutputData->MutableMetadata()->GetMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		if (SourceDomain && OutputDomain)
		{
			OutputDomain->Initialize(FPCGMetadataDomainInitializeParams(SourceDomain));
		}
	}
}

#if WITH_EDITOR
FText UPCGDynMeshFromStaticMeshPointsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | From Static Mesh Points");
}

FText UPCGDynMeshFromStaticMeshPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Makes one DynMesh per input point data. Each point places the static mesh it names by its own transform, "
		"and the DynMesh keeps the point data's tags and @Data attributes. Connect Get Static Mesh Data to turn "
		"mesh components placed in the level into DynMeshes that still carry what their components were tagged with.");
}
#endif

TArray<FPCGPinProperties> UPCGDynMeshFromStaticMeshPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, true, true,
		LOCTEXT("InTooltip", "Points that each name a static mesh in the attribute given by Mesh Attribute Name, such as the output of Get Static Mesh Data."))
		.SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynMeshFromStaticMeshPointsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, true, true,
		LOCTEXT("OutTooltip", "One DynMesh per input point data that placed at least one mesh, with that data's tags and @Data attributes."));
	return Pins;
}

FPCGElementPtr UPCGDynMeshFromStaticMeshPointsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshFromStaticMeshPointsElement>();
}

bool FPCGDynMeshFromStaticMeshPointsElement::ExecuteInternal(FPCGContext* Context) const
{
	using namespace PCGDynMeshFromStaticMeshPointsPrivate;
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDynMeshFromStaticMeshPointsElement::ExecuteInternal);
	check(Context);

	const UPCGDynMeshFromStaticMeshPointsSettings* Settings = Context->GetInputSettings<UPCGDynMeshFromStaticMeshPointsSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (Inputs.IsEmpty())
	{
		return true;
	}

	const FTransform ActorTransform = PCGUtilsDynMeshSpaceHelpers::ResolveMeshActorTransform(
		Context, nullptr, Settings->bConvertToLocalSpace);
	TMap<UStaticMesh*, FSourceMesh> SourceMeshes;

	for (int32 Index = 0; Index < Inputs.Num(); ++Index)
	{
		const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Inputs[Index].Data);
		if (!PointData)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("NotPoints", "DynMesh | From Static Mesh Points: input {0} is not point data; skipped."), FText::AsNumber(Index)), Context);
			continue;
		}

		bool bOnPoints = false;
		const FPCGMetadataAttributeBase* MeshAttribute = FindMeshAttribute(PointData->ConstMetadata(), Settings->MeshAttributeName, bOnPoints);
		if (!MeshAttribute)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("NoMeshAttribute", "DynMesh | From Static Mesh Points: input {0} has no '{1}' attribute, so its points name no mesh; skipped."),
				FText::AsNumber(Index), FText::FromName(Settings->MeshAttributeName)), Context);
			continue;
		}
		if (MeshAttribute->GetTypeId() != PCG::Private::MetadataTypes<FSoftObjectPath>::Id && MeshAttribute->GetTypeId() != PCG::Private::MetadataTypes<FString>::Id)
		{
			PCGLog::LogErrorOnGraph(FText::Format(
				LOCTEXT("MeshNotPath", "DynMesh | From Static Mesh Points: input {0}: attribute '{1}' is of type {2}; a mesh must be a soft object path or a string; skipped."),
				FText::AsNumber(Index), FText::FromName(Settings->MeshAttributeName),
				FText::FromString(StaticEnum<EPCGMetadataTypes>()->GetNameStringByValue(MeshAttribute->GetTypeId()))), Context);
			continue;
		}

		FDynamicMesh3 Combined;
		Combined.EnableAttributes();
		Combined.Attributes()->EnableMaterialID();
		TArray<UMaterialInterface*> Materials;
		int32 NumPlaced = 0;

		const TConstPCGValueRange<FTransform> Transforms = PointData->GetConstTransformValueRange();
		const TConstPCGValueRange<int64> EntryKeys = PointData->GetConstMetadataEntryValueRange();
		for (int32 PointIndex = 0; PointIndex < Transforms.Num(); ++PointIndex)
		{
			// A value on the data itself is its first entry.
			FSoftObjectPath MeshPath;
			ReadPath(MeshAttribute, bOnPoints ? EntryKeys[PointIndex] : PCGFirstEntryKey, MeshPath);
			if (MeshPath.IsNull())
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MeshPathEmpty", "DynMesh | From Static Mesh Points: input {0}, point {1}: attribute '{2}' is empty; it must name a static mesh; point skipped."),
					FText::AsNumber(Index), FText::AsNumber(PointIndex), FText::FromName(Settings->MeshAttributeName)), Context);
				continue;
			}
			UObject* Loaded = MeshPath.TryLoad();
			if (!Loaded)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MeshNotLoaded", "DynMesh | From Static Mesh Points: input {0}, point {1}: '{2}' could not be loaded; point skipped."),
					FText::AsNumber(Index), FText::AsNumber(PointIndex), FText::FromString(MeshPath.ToString())), Context);
				continue;
			}
			UStaticMesh* StaticMesh = Cast<UStaticMesh>(Loaded);
			if (!StaticMesh)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("NotStaticMesh", "DynMesh | From Static Mesh Points: input {0}, point {1}: '{2}' is a {3}, not a static mesh; point skipped."),
					FText::AsNumber(Index), FText::AsNumber(PointIndex), FText::FromString(MeshPath.ToString()), FText::FromString(Loaded->GetClass()->GetName())), Context);
				continue;
			}
			const FSourceMesh& Source = ReadStaticMesh(Context, StaticMesh, SourceMeshes);
			if (!Source.bRead)
			{
				PCGLog::LogErrorOnGraph(FText::Format(
					LOCTEXT("MeshNotRead", "DynMesh | From Static Mesh Points: input {0}, point {1}: the geometry of '{2}' could not be read; point skipped."),
					FText::AsNumber(Index), FText::AsNumber(PointIndex), FText::FromString(MeshPath.ToString())), Context);
				continue;
			}

			FDynamicMesh3 Placed = Source.Mesh;
			RemapMaterials(Placed, Source.Materials, Materials);
			// A mirrored point turns its mesh inside out; put the faces back the right way round.
			MeshTransforms::ApplyTransform(Placed,
				UE::Geometry::FTransformSRT3d(Transforms[PointIndex].GetRelativeTransform(ActorTransform)), /*bReverseOrientationIfNeeded=*/true);

			UE::Geometry::FMeshIndexMappings Mappings;
			UE::Geometry::FDynamicMeshEditor(&Combined).AppendMesh(&Placed, Mappings);
			++NumPlaced;
		}

		if (NumPlaced == 0)
		{
			continue;
		}

		UPCGDynamicMeshData* OutputData = FPCGContext::NewObject_AnyThread<UPCGDynamicMeshData>(Context);
		OutputData->Initialize(MoveTemp(Combined), Materials);
		InheritDataDomain(OutputData, PointData);
		OutputData->TargetActor = PointData->TargetActor;

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Inputs[Index]);
		Output.Data = OutputData;
		Output.Pin = PCGPinConstants::DefaultOutputLabel;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
