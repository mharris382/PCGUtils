#include "Elements/Conversion/PCGDynamicMeshSelectionToPoints.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Factories/PCGUtilsDynMeshDomainSelectionFactory.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GameFramework/Actor.h"
#include "Metadata/PCGMetadata.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshSelectionToPoints"

namespace
{
	const FName SelectionToPointsInputPin = TEXT("Selection");
	const FName SelectionToPointsOutputPin = TEXT("Points");
	const FName SelectionDomainAttributeName = TEXT("SelectionDomain");

	const TCHAR* GetSelectionDomainDisplayName(UE::Geometry::EGeometryElementType ElementType)
	{
		switch (ElementType)
		{
		case UE::Geometry::EGeometryElementType::Vertex:
			return TEXT("Vertex");
		case UE::Geometry::EGeometryElementType::Edge:
			return TEXT("Edge");
		case UE::Geometry::EGeometryElementType::Face:
			return TEXT("Triangle");
		default:
			return TEXT("Unknown");
		}
	}

	template<typename OverlayType, typename ValueType>
	ValueType SelectionToPoints_GetFirstVertexOverlayElement(const UE::Geometry::FDynamicMesh3& Mesh,
		const OverlayType* Overlay, int32 VertexID, const ValueType& DefaultValue)
	{
		if (!Overlay) return DefaultValue;
		ValueType Result = DefaultValue;
		bool bFound = false;
		Mesh.EnumerateVertexTriangles(VertexID, [&](int32 TriangleID)
		{
			if (bFound || !Overlay->IsSetTriangle(TriangleID)) return;
			const UE::Geometry::FIndex3i TriangleVertices = Mesh.GetTriangle(TriangleID);
			const UE::Geometry::FIndex3i TriangleElements = Overlay->GetTriangle(TriangleID);
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				if (TriangleVertices[Corner] == VertexID && Overlay->IsElement(TriangleElements[Corner]))
				{
					Result = Overlay->GetElement(TriangleElements[Corner]);
					bFound = true;
					break;
				}
			}
		});
		return Result;
	}
}

#if WITH_EDITOR
FText UPCGDynamicMeshSelectionToPointsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh Selection To Points");
}

FText UPCGDynamicMeshSelectionToPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Converts the incoming vertex, edge, or triangle selection to vertices and outputs one PCG point for each unique selected source-mesh vertex. The source selection domain is written to the SelectionDomain @Data attribute and a readable data tag.");
}
#endif

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionToPointsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins[0] = FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(SelectionToPointsInputPin);
	Pins[0].SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionToPointsSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(SelectionToPointsOutputPin, EPCGDataType::Point, true, true)};
}

FPCGElementPtr UPCGDynamicMeshSelectionToPointsSettings::CreateElement() const
{
	return MakeShared<FPCGDynamicMeshSelectionToPointsElement>();
}

bool FPCGDynamicMeshSelectionToPointsElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGDynamicMeshSelectionToPointsSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshSelectionToPointsSettings>();
	check(Settings);

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(SelectionToPointsInputPin))
	{
		const FPCGUtilsDynMeshResolvedInput ResolvedInput =
			FPCGUtilsDynMeshProcessFunctions::ResolveInput(Input.Data, Settings, Context);
		if (!ResolvedInput.IsValid())
		{
			continue;
		}
		const UPCGDynamicMeshSelectionData* SelectionData = ResolvedInput.SelectionData;
		const UPCGDynamicMeshData* MeshData = ResolvedInput.MeshData;
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!Mesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSource", "Dynamic Mesh Selection To Points skipped a selection with no valid source mesh."), Context);
			continue;
		}

		UE::Geometry::FGeometrySelection VertexSelection;
		if (!PCGUtilsDynMeshSelectionDomains::ConvertSelection(
			MeshData, *Mesh, SelectionData->GetSelection(),
			UE::Geometry::EGeometryElementType::Vertex, Settings->bAllowPartialInclusion,
			VertexSelection))
		{
			PCGLog::LogErrorOnGraph(
				LOCTEXT("SelectionConversionFailed", "Dynamic Mesh Selection To Points could not convert the incoming selection to vertices."),
				Context);
			continue;
		}

		TSet<int32> SelectedVertexSet;
		for (const uint64 EncodedID : VertexSelection.Selection)
		{
			const int32 VertexID = static_cast<int32>(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID);
			if (Mesh->IsVertex(VertexID))
			{
				SelectedVertexSet.Add(VertexID);
			}
		}

		TArray<int32> SelectedVertices = SelectedVertexSet.Array();
		SelectedVertices.Sort();
		FTransform MeshToOutput = FTransform::Identity;
		if (Settings->bOutputToWorldSpace)
		{
			if (const AActor* TargetActor = Context->GetTargetActor(MeshData))
			{
				MeshToOutput = TargetActor->GetActorTransform();
			}
			else
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("MissingTargetActor", "Dynamic Mesh Selection To Points could not resolve a target actor; points remain mesh-local."), Context);
			}
		}

		const UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh->HasAttributes()
			? Mesh->Attributes()->PrimaryNormals() : nullptr;
		const UE::Geometry::FDynamicMeshColorOverlay* Colors = Mesh->HasAttributes()
			? Mesh->Attributes()->PrimaryColors() : nullptr;
		UPCGPointArrayData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
		OutputData->SetNumPoints(SelectedVertices.Num(), false);
		auto Transforms = OutputData->GetTransformValueRange();
		auto PointColors = OutputData->GetColorValueRange();
		auto Densities = OutputData->GetDensityValueRange();
		auto BoundsMin = OutputData->GetBoundsMinValueRange();
		auto BoundsMax = OutputData->GetBoundsMaxValueRange();

		for (int32 Index = 0; Index < SelectedVertices.Num(); ++Index)
		{
			const int32 VertexID = SelectedVertices[Index];
			const FVector LocalPosition(Mesh->GetVertex(VertexID));
			FVector3f Normal = Normals
				? SelectionToPoints_GetFirstVertexOverlayElement(*Mesh, Normals, VertexID, FVector3f::UnitZ())
				: (Mesh->HasVertexNormals() ? Mesh->GetVertexNormal(VertexID) : FVector3f::UnitZ());
			if (!Normal.Normalize())
			{
				Normal = FVector3f::UnitZ();
			}
			const FVector LocalNormal(Normal);
			const FVector Position = Settings->bOutputToWorldSpace
				? MeshToOutput.TransformPosition(LocalPosition) : LocalPosition;
			const FVector OutputNormal = Settings->bOutputToWorldSpace
				? MeshToOutput.TransformVectorNoScale(LocalNormal).GetSafeNormal() : LocalNormal;
			Transforms[Index] = FTransform(FRotationMatrix::MakeFromZ(OutputNormal).ToQuat(), Position);
			const FVector3f NativeColor = Mesh->HasVertexColors() ? Mesh->GetVertexColor(VertexID) : FVector3f::One();
			const FVector4f Color = Colors
				? SelectionToPoints_GetFirstVertexOverlayElement(*Mesh, Colors, VertexID, FVector4f(1, 1, 1, 1))
				: FVector4f(NativeColor.X, NativeColor.Y, NativeColor.Z, 1.0f);
			PointColors[Index] = FVector4(Color);
			Densities[Index] = 1.0f;
			BoundsMin[Index] = FVector::ZeroVector;
			BoundsMax[Index] = FVector::ZeroVector;
		}

		const UE::Geometry::EGeometryElementType SelectionElementType =
			SelectionData->GetSelection().ElementType;
		const int32 SelectionDomainValue = static_cast<int32>(SelectionElementType);
		if (UPCGMetadata* Metadata = OutputData->MutableMetadata())
		{
			if (FPCGMetadataAttribute<int32>* SelectionDomainAttribute =
				Metadata->FindOrCreateAttribute<int32>(
					FPCGAttributeIdentifier(SelectionDomainAttributeName, PCGMetadataDomainID::Data),
					SelectionDomainValue,
					/*bAllowsInterpolation=*/false,
					/*bOverrideParent=*/false,
					/*bOverwriteIfTypeMismatch=*/true))
			{
				SelectionDomainAttribute->SetValue(PCGInvalidEntryKey, SelectionDomainValue);
			}
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = SelectionToPointsOutputPin;
		Output.Tags.Add(FString::Printf(
			TEXT("Selection Domain: %s"), GetSelectionDomainDisplayName(SelectionElementType)));
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
