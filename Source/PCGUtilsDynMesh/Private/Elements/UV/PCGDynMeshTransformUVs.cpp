// Copyright Max Harris

#include "Elements/UV/PCGDynMeshTransformUVs.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/PCGUtilsDynMeshUVRegion.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshTransformUVs"

UPCGDynMeshTransformUVsSettings::UPCGDynMeshTransformUVsSettings()
	: Translation(FVector2D::ZeroVector, TEXT("UVTranslation"))
	, Rotation(0.0, TEXT("UVRotation"))
	, Scale(FVector2D::UnitVector, TEXT("UVScale"))
{
}

#if WITH_EDITOR
FText UPCGDynMeshTransformUVsSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Transform UVs");
}

FText UPCGDynMeshTransformUVsSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Scales, rotates and moves a UV layer about a pivot (UV origin, region centre, each island's centre, or a custom "
		"point). Each setting is a constant or read from @Data. A DynMesh Selection or Selector limits it to those triangles.");
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshTransformUVsSettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshTransformUVsOperation> Operation = MakeShared<FPCGUtilsDynMeshTransformUVsOperation>();
	Operation->UVLayer = UVLayer;
	Operation->Translation = Translation;
	Operation->Rotation = Rotation;
	Operation->Scale = Scale;
	Operation->Pivot = Pivot;
	Operation->CustomPivot = CustomPivot;
	return Operation;
}

FPCGElementPtr UPCGDynMeshTransformUVsSettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshTransformUVsElement>();
}

bool FPCGUtilsDynMeshTransformUVsOperation::ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	UE::Geometry::FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const
{
	const UPCGDynamicMeshData* Source = Invocation.SourceMeshData ? Invocation.SourceMeshData : Invocation.MeshData;
	auto Read = [&](const auto& Shorthand, auto& OutValue, const FText& Label)
	{
		FText Error;
		if (!Shorthand.TryReadDataValue(Source, OutValue, Error, Label, Invocation.InputIndex))
		{
			PCGLog::LogErrorOnGraph(Error, Invocation.Context);
			return false;
		}
		return true;
	};
	FVector2D Offset = FVector2D::ZeroVector;
	FVector2D Factor = FVector2D::UnitVector;
	double Degrees = 0.0;
	if (!Read(Translation, Offset, LOCTEXT("TranslationLabel", "Translation"))
		|| !Read(Rotation, Degrees, LOCTEXT("RotationLabel", "Rotation"))
		|| !Read(Scale, Factor, LOCTEXT("ScaleLabel", "Scale")))
	{
		return false;
	}

	PCGUtilsDynMeshUV::FRegionUVs Region;
	PCGUtilsDynMeshUV::Gather(Mesh, UVOverlay, TriangleIDs, Region);
	if (Region.UVs.IsEmpty())
	{
		return true;
	}

	double Sin = 0.0, Cos = 1.0;
	FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Degrees));
	auto Apply = [&](int32 Index, const FVector2D& PivotPoint)
	{
		const FVector2D P = (FVector2D(Region.UVs[Index]) - PivotPoint) * Factor;
		const FVector2D Turned(P.X * Cos - P.Y * Sin, P.X * Sin + P.Y * Cos);
		Region.UVs[Index] = FVector2f(Turned + PivotPoint + Offset);
	};
	auto BoundsCenter = [&Region](TConstArrayView<int32> Indices)
	{
		FBox2D Bounds(ForceInit);
		for (const int32 Index : Indices)
		{
			Bounds += FVector2D(Region.UVs[Index]);
		}
		return Bounds.GetCenter();
	};

	if (Pivot == EPCGUtilsDynMeshUVPivot::IslandCenter)
	{
		for (const TArray<int32>& Island : PCGUtilsDynMeshUV::FindIslands(Region))
		{
			const TArray<int32> Indices = PCGUtilsDynMeshUV::IslandUVIndices(Region, Island);
			const FVector2D Center = BoundsCenter(Indices);
			for (const int32 Index : Indices)
			{
				Apply(Index, Center);
			}
		}
	}
	else
	{
		TArray<int32> All;
		All.Reserve(Region.UVs.Num());
		for (int32 Index = 0; Index < Region.UVs.Num(); ++Index)
		{
			All.Add(Index);
		}
		const FVector2D PivotPoint = Pivot == EPCGUtilsDynMeshUVPivot::Custom ? CustomPivot
			: Pivot == EPCGUtilsDynMeshUVPivot::RegionCenter ? BoundsCenter(All) : FVector2D::ZeroVector;
		for (const int32 Index : All)
		{
			Apply(Index, PivotPoint);
		}
	}
	PCGUtilsDynMeshUV::WriteBack(UVOverlay, Region);
	return true;
}

#undef LOCTEXT_NAMESPACE
