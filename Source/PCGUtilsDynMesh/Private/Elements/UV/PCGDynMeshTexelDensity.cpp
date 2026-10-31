// Copyright Max Harris

#include "Elements/UV/PCGDynMeshTexelDensity.h"

#include "Data/PCGDynamicMeshData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/PCGUtilsDynMeshUVRegion.h"
#include "Utils/PCGLogErrors.h"

#define LOCTEXT_NAMESPACE "PCGDynMeshTexelDensity"

UPCGDynMeshTexelDensitySettings::UPCGDynMeshTexelDensitySettings()
	: TexelDensity(512.0, TEXT("TexelDensity"))
	, TextureResolution(1024, TEXT("TextureResolution"))
{
}

#if WITH_EDITOR
FText UPCGDynMeshTexelDensitySettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "DynMesh | Texel Density");
}

FText UPCGDynMeshTexelDensitySettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip",
		"Scales UV islands so a texture of Texture Resolution pixels gets Texel Density pixels per metre: each island on "
		"its own, or the whole region by one factor. A DynMesh Selection or Selector limits it to those triangles.");
}

FString UPCGDynMeshTexelDensitySettings::GetAdditionalTitleInformation() const
{
	const FString Density = TexelDensity.IsConstant() ? FString::Printf(TEXT("%s px/m"), *FString::SanitizeFloat(TexelDensity.Constant, 0)) : TEXT("@Data px/m");
	const FString Resolution = TextureResolution.IsConstant() ? FString::Printf(TEXT("%d px"), TextureResolution.Constant) : TEXT("@Data px");
	return FString::Printf(TEXT("%s at %s"), *Density, *Resolution);
}
#endif

TSharedPtr<const FPCGUtilsDynMeshProcessOperation> UPCGDynMeshTexelDensitySettings::CreateProcessOperation(FPCGContext*) const
{
	TSharedPtr<FPCGUtilsDynMeshTexelDensityOperation> Operation = MakeShared<FPCGUtilsDynMeshTexelDensityOperation>();
	Operation->UVLayer = UVLayer;
	Operation->Mode = Mode;
	Operation->TexelDensity = TexelDensity;
	Operation->TextureResolution = TextureResolution;
	return Operation;
}

FPCGElementPtr UPCGDynMeshTexelDensitySettings::CreateElement() const
{
	return MakeShared<FPCGDynMeshTexelDensityElement>();
}

bool FPCGUtilsDynMeshTexelDensityOperation::ProcessUVs(const FPCGUtilsDynMeshProcessInvocation& Invocation,
	UE::Geometry::FDynamicMesh3& Mesh, UE::Geometry::FDynamicMeshUVOverlay& UVOverlay, TArrayView<const int32> TriangleIDs) const
{
	const UPCGDynamicMeshData* Source = Invocation.SourceMeshData ? Invocation.SourceMeshData : Invocation.MeshData;
	double Density = 0.0;
	int32 Resolution = 0;
	FText Error;
	if (!TexelDensity.TryReadDataValue(Source, Density, Error, LOCTEXT("DensityLabel", "Texel Density"), Invocation.InputIndex)
		|| !TextureResolution.TryReadDataValue(Source, Resolution, Error, LOCTEXT("ResolutionLabel", "Texture Resolution"), Invocation.InputIndex))
	{
		PCGLog::LogErrorOnGraph(Error, Invocation.Context);
		return false;
	}
	if (Density <= 0.0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadDensity",
			"DynMesh | Texel Density (input {0}): Texel Density is {1}; it must be above 0. The mesh's UVs were left unchanged."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Density)), Invocation.Context);
		return true;
	}
	if (Resolution <= 0)
	{
		PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("BadResolution",
			"DynMesh | Texel Density (input {0}): Texture Resolution is {1}; it must be above 0. The mesh's UVs were left unchanged."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Resolution)), Invocation.Context);
		return true;
	}
	// UV length per cm of surface: pixels per cm over pixels per UV unit.
	const double TargetUVPerCm = (Density / 100.0) / Resolution;

	PCGUtilsDynMeshUV::FRegionUVs Region;
	PCGUtilsDynMeshUV::Gather(Mesh, UVOverlay, TriangleIDs, Region);
	const TArray<TArray<int32>> Islands = PCGUtilsDynMeshUV::FindIslands(Region);

	auto ScaleAbout = [&Region](TConstArrayView<int32> Indices, double Factor)
	{
		FBox2D Bounds(ForceInit);
		for (const int32 Index : Indices)
		{
			Bounds += FVector2D(Region.UVs[Index]);
		}
		const FVector2D Center = Bounds.GetCenter();
		for (const int32 Index : Indices)
		{
			Region.UVs[Index] = FVector2f(Center + (FVector2D(Region.UVs[Index]) - Center) * Factor);
		}
	};

	int32 Degenerate = 0;
	if (Mode == EPCGUtilsDynMeshTexelDensityMode::EachIsland)
	{
		for (const TArray<int32>& Island : Islands)
		{
			double Surface = 0.0, UVArea = 0.0;
			PCGUtilsDynMeshUV::IslandAreas(Mesh, Region, Island, Surface, UVArea);
			if (Surface <= UE_DOUBLE_SMALL_NUMBER || UVArea <= UE_DOUBLE_SMALL_NUMBER)
			{
				++Degenerate;
				continue;
			}
			ScaleAbout(PCGUtilsDynMeshUV::IslandUVIndices(Region, Island), TargetUVPerCm / FMath::Sqrt(UVArea / Surface));
		}
	}
	else
	{
		double Surface = 0.0, UVArea = 0.0;
		TArray<int32> AllTriangles;
		AllTriangles.Reserve(Region.Triangles.Num());
		for (int32 Index = 0; Index < Region.Triangles.Num(); ++Index)
		{
			AllTriangles.Add(Index);
		}
		PCGUtilsDynMeshUV::IslandAreas(Mesh, Region, AllTriangles, Surface, UVArea);
		if (Surface <= UE_DOUBLE_SMALL_NUMBER || UVArea <= UE_DOUBLE_SMALL_NUMBER)
		{
			++Degenerate;
		}
		else
		{
			TArray<int32> All;
			All.Reserve(Region.UVs.Num());
			for (int32 Index = 0; Index < Region.UVs.Num(); ++Index)
			{
				All.Add(Index);
			}
			ScaleAbout(All, TargetUVPerCm / FMath::Sqrt(UVArea / Surface));
		}
	}
	if (Degenerate > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("Degenerate",
			"DynMesh | Texel Density (input {0}): {1} island(s) have no surface or no UV area, so were not scaled."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Degenerate)), Invocation.Context);
	}
	if (Region.NumUnset > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(LOCTEXT("Unset",
			"DynMesh | Texel Density (input {0}): {1} triangles have no UVs on layer {2}, so were not scaled."),
			FText::AsNumber(Invocation.InputIndex), FText::AsNumber(Region.NumUnset), FText::AsNumber(UVLayer)), Invocation.Context);
	}
	PCGUtilsDynMeshUV::WriteBack(UVOverlay, Region);
	return true;
}

#undef LOCTEXT_NAMESPACE
