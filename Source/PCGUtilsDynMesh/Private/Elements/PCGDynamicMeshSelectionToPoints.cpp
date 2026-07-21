#include "Elements/PCGDynamicMeshSelectionToPoints.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GameFramework/Actor.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshSelectionToPoints"

namespace
{
	const FName SelectionToPointsInputPin = TEXT("Selection");
	const FName SelectionToPointsOutputPin = TEXT("Points");

	template<typename OverlayType, typename ValueType>
	ValueType GetFirstVertexOverlayElement(const UE::Geometry::FDynamicMesh3& Mesh,
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
	return LOCTEXT("Title", "Dynamic Mesh Selection To Points");
}

FText UPCGDynamicMeshSelectionToPointsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Outputs one PCG point for each unique source-mesh vertex belonging to the selected triangle faces.");
}
#endif

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionToPointsSettings::InputPinProperties() const
{
	return {FPCGPinProperties(SelectionToPointsInputPin,
		FPCGDataTypeIdentifier(UPCGDynamicMeshSelectionData::StaticClass()), true, true)};
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
		const UPCGDynamicMeshSelectionData* SelectionData = Cast<const UPCGDynamicMeshSelectionData>(Input.Data);
		const UPCGDynamicMeshData* MeshData = SelectionData ? SelectionData->GetSourceMeshData() : nullptr;
		const UDynamicMesh* DynamicMesh = MeshData ? MeshData->GetDynamicMesh() : nullptr;
		const UE::Geometry::FDynamicMesh3* Mesh = DynamicMesh ? DynamicMesh->GetMeshPtr() : nullptr;
		if (!Mesh)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("InvalidSource", "Dynamic Mesh Selection To Points skipped a selection with no valid source mesh."), Context);
			continue;
		}

		TSet<int32> SelectedVertexSet;
		int32 InvalidTriangleCount = 0;
		for (uint64 EncodedID : SelectionData->GetSelection().Selection)
		{
			const int32 TriangleID = static_cast<int32>(UE::Geometry::FGeoSelectionID(EncodedID).GeometryID);
			if (!Mesh->IsTriangle(TriangleID))
			{
				++InvalidTriangleCount;
				continue;
			}
			const UE::Geometry::FIndex3i Triangle = Mesh->GetTriangle(TriangleID);
			SelectedVertexSet.Add(Triangle.A);
			SelectedVertexSet.Add(Triangle.B);
			SelectedVertexSet.Add(Triangle.C);
		}
		if (InvalidTriangleCount > 0)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("InvalidTriangles", "Dynamic Mesh Selection To Points ignored {0} invalid or stale triangle IDs."),
				FText::AsNumber(InvalidTriangleCount)), Context);
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
				? GetFirstVertexOverlayElement(*Mesh, Normals, VertexID, FVector3f::UnitZ())
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
				? GetFirstVertexOverlayElement(*Mesh, Colors, VertexID, FVector4f(1, 1, 1, 1))
				: FVector4f(NativeColor.X, NativeColor.Y, NativeColor.Z, 1.0f);
			PointColors[Index] = FVector4(Color);
			Densities[Index] = 1.0f;
			BoundsMin[Index] = FVector::ZeroVector;
			BoundsMax[Index] = FVector::ZeroVector;
		}

		FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
		Output.Data = OutputData;
		Output.Pin = SelectionToPointsOutputPin;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
