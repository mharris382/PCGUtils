#include "Elements/Conversion/PCGDynamicMeshSelectionToPaths.h"

#include "Data/PCGBasePointData.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGDynamicMeshSelectionData.h"
#include "Data/PCGPointArrayData.h"
#include "GameFramework/Actor.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshSelectionQueryFunctions.h"
#include "Metadata/PCGMetadata.h"
#include "MeshTarget/PCGUtilsMeshTargetFunctions.h"
#include "PCGContext.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshSelectionToPaths"

namespace
{
	const FName SelectionToPathsInputPin = TEXT("Selection");
	const FName SelectionToPathsOutputPin = TEXT("Paths");
	const FName IsClosedAttributeName = TEXT("IsClosed");

	bool IsNegativeProjectedWinding(const TArray<FVector>& Positions, const FVector& ProjectionAxis)
	{
		if (Positions.Num() < 3)
		{
			return false;
		}

		const FVector3d Normal(FVector(ProjectionAxis).GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::UpVector));
		const FVector3d Origin(Positions[0]);
		double SignedTwiceArea = 0.0;
		for (int32 Index = 1; Index + 1 < Positions.Num(); ++Index)
		{
			const FVector3d A = FVector3d(Positions[Index]) - Origin;
			const FVector3d B = FVector3d(Positions[Index + 1]) - Origin;
			SignedTwiceArea += FVector3d::DotProduct(FVector3d::CrossProduct(A, B), Normal);
		}

		// PCGEx's Clipper2 paths use !IsPositive(path) for holes. IsPositive includes zero,
		// so a degenerate projected loop is deliberately not classified as a hole here.
		return SignedTwiceArea < 0.0;
	}

	void SetClosedLoopDataAttribute(UPCGPointArrayData* PointData)
	{
		if (!PointData)
		{
			return;
		}

		if (UPCGMetadata* Metadata = PointData->MutableMetadata())
		{
			if (FPCGMetadataAttribute<bool>* IsClosedAttribute = Metadata->FindOrCreateAttribute<bool>(
				FPCGAttributeIdentifier(IsClosedAttributeName, PCGMetadataDomainID::Data),
				true,
				/*bAllowsInterpolation=*/false,
				/*bOverrideParent=*/false,
				/*bOverwriteIfTypeMismatch=*/true))
			{
				IsClosedAttribute->SetValue(PCGInvalidEntryKey, true);
			}
		}
	}
}

#if WITH_EDITOR
FText UPCGDynamicMeshSelectionToPathsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh Selection To Paths");
}

FText UPCGDynamicMeshSelectionToPathsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Outputs one closed, ordered point-data path for each boundary loop of the selection's implied triangle region. "
		"Vertex and edge selections expand to incident triangles. Optional hole tagging uses projected winding and the PCGEx convention.");
}
#endif

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionToPathsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins = Super::InputPinProperties();
	Pins[0] = FPCGUtilsMeshTargetFunctions::MakeMeshInputPinProperties(SelectionToPathsInputPin);
	Pins[0].SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGDynamicMeshSelectionToPathsSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(SelectionToPathsOutputPin, EPCGDataType::Point, true, true)};
}

FPCGElementPtr UPCGDynamicMeshSelectionToPathsSettings::CreateElement() const
{
	return MakeShared<FPCGDynamicMeshSelectionToPathsElement>();
}

bool FPCGDynamicMeshSelectionToPathsElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);
	const UPCGDynamicMeshSelectionToPathsSettings* Settings =
		Context->GetInputSettings<UPCGDynamicMeshSelectionToPathsSettings>();
	check(Settings);

	const FVector ProjectionAxis = Settings->ProjectionAxis.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::UpVector);
	if (Settings->bTagHoles && Settings->ProjectionAxis.IsNearlyZero())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("InvalidProjectionAxis", "Dynamic Mesh Selection To Paths received a zero projection axis; using +Z."),
			Context);
	}

	for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(SelectionToPathsInputPin))
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
		if (!DynamicMesh || !DynamicMesh->GetMeshPtr())
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("InvalidSource", "Dynamic Mesh Selection To Paths skipped a selection with no valid source mesh."),
				Context);
			continue;
		}

		FGeometryScriptMeshSelection ScriptSelection;
		ScriptSelection.SetSelection(SelectionData->GetSelection());

		TArray<FGeometryScriptIndexList> IndexLoops;
		TArray<FGeometryScriptPolyPath> PathLoops;
		int32 NumLoops = 0;
		bool bFoundErrors = false;
		UGeometryScriptLibrary_MeshSelectionQueryFunctions::GetMeshSelectionBoundaryLoops(
			const_cast<UDynamicMesh*>(DynamicMesh), ScriptSelection, IndexLoops, PathLoops,
			NumLoops, bFoundErrors);

		if (bFoundErrors)
		{
			PCGLog::LogWarningOnGraph(
				LOCTEXT("BoundaryErrors", "Dynamic Mesh Selection To Paths found topological errors; its boundary-loop output may be incomplete."),
				Context);
		}

		FTransform MeshToOutput = FTransform::Identity;
		if (Settings->bOutputToWorldSpace)
		{
			if (const AActor* TargetActor = Context->GetTargetActor(MeshData))
			{
				MeshToOutput = TargetActor->GetActorTransform();
			}
			else
			{
				PCGLog::LogWarningOnGraph(
					LOCTEXT("MissingTargetActor", "Dynamic Mesh Selection To Paths could not resolve a target actor; paths remain mesh-local."),
					Context);
			}
		}

		for (const FGeometryScriptPolyPath& PathLoop : PathLoops)
		{
			if (!PathLoop.Path.IsValid() || PathLoop.Path->Num() < 3)
			{
				PCGLog::LogWarningOnGraph(
					LOCTEXT("InvalidLoop", "Dynamic Mesh Selection To Paths skipped a boundary loop with fewer than three vertices."),
					Context);
				continue;
			}

			TArray<FVector> OutputPositions;
			OutputPositions.Reserve(PathLoop.Path->Num());
			for (const FVector& MeshPosition : *PathLoop.Path)
			{
				OutputPositions.Add(MeshToOutput.TransformPosition(MeshPosition));
			}

			UPCGPointArrayData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
			OutputData->SetNumPoints(OutputPositions.Num(), false);
			auto Transforms = OutputData->GetTransformValueRange();
			auto PointColors = OutputData->GetColorValueRange();
			auto Densities = OutputData->GetDensityValueRange();
			auto BoundsMin = OutputData->GetBoundsMinValueRange();
			auto BoundsMax = OutputData->GetBoundsMaxValueRange();
			for (int32 PointIndex = 0; PointIndex < OutputPositions.Num(); ++PointIndex)
			{
				Transforms[PointIndex] = FTransform(OutputPositions[PointIndex]);
				PointColors[PointIndex] = FVector4::One();
				Densities[PointIndex] = 1.0f;
				BoundsMin[PointIndex] = FVector::ZeroVector;
				BoundsMax[PointIndex] = FVector::ZeroVector;
			}
			SetClosedLoopDataAttribute(OutputData);

			FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef(Input);
			Output.Data = OutputData;
			Output.Pin = SelectionToPathsOutputPin;
			if (Settings->bTagHoles && !Settings->HoleTag.IsEmpty()
				&& IsNegativeProjectedWinding(OutputPositions, ProjectionAxis))
			{
				Output.Tags.Add(Settings->HoleTag);
			}
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
