// Copyright Max Harris

#include "StaticMesh/PCGUtilsPainterStaticMeshBackend.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "RenderResource.h"
#include "Rendering/ColorVertexBuffer.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"
#include "StaticMeshComponentLODInfo.h"
#include "StaticMeshResources.h"

namespace PCGUtilsPainterStaticMeshBackend
{
	namespace
	{
		const FStaticMeshLODResources* GetLODResources(const UStaticMeshComponent* Component, int32 LODIndex)
		{
			const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
			const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
			if (!RenderData || !RenderData->LODResources.IsValidIndex(LODIndex))
			{
				return nullptr;
			}
			return &RenderData->LODResources[LODIndex];
		}
	}

	int32 GetNumLODs(const UStaticMeshComponent* Component)
	{
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		return (Mesh && Mesh->GetRenderData()) ? Mesh->GetNumLODs() : 0;
	}

	int32 GetLODRenderVertexCount(const UStaticMeshComponent* Component, int32 LODIndex)
	{
		const FStaticMeshLODResources* LOD = GetLODResources(Component, LODIndex);
		return LOD ? LOD->GetNumVertices() : 0;
	}

	bool GetLODRenderVertices(
		const UStaticMeshComponent* Component,
		int32 LODIndex,
		TArray<FVector3f>& OutPositions,
		TArray<FVector3f>& OutNormals)
	{
		OutPositions.Reset();
		OutNormals.Reset();

		const FStaticMeshLODResources* LOD = GetLODResources(Component, LODIndex);
		if (!LOD)
		{
			return false;
		}

		const FPositionVertexBuffer& PositionBuffer = LOD->VertexBuffers.PositionVertexBuffer;
		const FStaticMeshVertexBuffer& VertexBuffer = LOD->VertexBuffers.StaticMeshVertexBuffer;

		const int32 NumVertices = static_cast<int32>(PositionBuffer.GetNumVertices());
		if (NumVertices <= 0)
		{
			// No CPU-side position data (cooked mesh without bAllowCPUAccess). Editor meshes always have it.
			return false;
		}

		const bool bHaveNormals = static_cast<int32>(VertexBuffer.GetNumVertices()) == NumVertices;

		OutPositions.SetNumUninitialized(NumVertices);
		OutNormals.SetNumUninitialized(NumVertices);
		for (int32 Index = 0; Index < NumVertices; ++Index)
		{
			OutPositions[Index] = PositionBuffer.VertexPosition(Index);
			OutNormals[Index] = bHaveNormals
				? FVector3f(FVector4f(VertexBuffer.VertexTangentZ(Index)))
				: FVector3f::ZAxisVector;
		}
		return true;
	}

	void GetBaseLODColors(
		const UStaticMeshComponent* Component,
		int32 LODIndex,
		EBaseColorMode Mode,
		TArray<FColor>& OutColors)
	{
		OutColors.Reset();

		const FStaticMeshLODResources* LOD = GetLODResources(Component, LODIndex);
		if (!LOD)
		{
			return;
		}
		const int32 NumVertices = LOD->GetNumVertices();
		if (NumVertices <= 0)
		{
			return;
		}

		const FColor Fallback = (Mode == EBaseColorMode::Black) ? FColor(0, 0, 0, 255) : FColor::White;

		auto TryFillFrom = [&OutColors, NumVertices](const FColorVertexBuffer* Buffer) -> bool
		{
			if (Buffer && static_cast<int32>(Buffer->GetNumVertices()) == NumVertices)
			{
				OutColors.SetNumUninitialized(NumVertices);
				for (int32 Index = 0; Index < NumVertices; ++Index)
				{
					OutColors[Index] = Buffer->VertexColor(Index);
				}
				return true;
			}
			return false;
		};

		if (Mode == EBaseColorMode::White || Mode == EBaseColorMode::Black)
		{
			OutColors.Init(Fallback, NumVertices);
			return;
		}

		if (Mode == EBaseColorMode::Existing
			&& Component->LODData.IsValidIndex(LODIndex)
			&& TryFillFrom(Component->LODData[LODIndex].OverrideVertexColors))
		{
			return;
		}

		// Existing-with-no-override and AssetVertexColors both fall back to the asset's own color buffer.
		if (TryFillFrom(&LOD->VertexBuffers.ColorVertexBuffer))
		{
			return;
		}

		OutColors.Init(Fallback, NumVertices);
	}

	bool SetOverrideVertexColorsForLOD(
		UStaticMeshComponent* Component,
		int32 LODIndex,
		TConstArrayView<FColor> Colors)
	{
		if (!Component)
		{
			return false;
		}
		UStaticMesh* Mesh = Component->GetStaticMesh();
		if (!Mesh || !Mesh->GetRenderData())
		{
			return false;
		}
		const int32 NumLODs = Mesh->GetNumLODs();
		if (LODIndex < 0 || LODIndex >= NumLODs || !Mesh->GetRenderData()->LODResources.IsValidIndex(LODIndex))
		{
			return false;
		}
		if (Colors.Num() != Mesh->GetRenderData()->LODResources[LODIndex].GetNumVertices())
		{
			return false;
		}

		// Ensure LODData has an entry for every mesh LOD, then release any prior override on this LOD.
		Component->SetLODDataCount(NumLODs, NumLODs);
		Component->RemoveInstanceVertexColorsFromLOD(LODIndex);

		FStaticMeshComponentLODInfo& LODInfo = Component->LODData[LODIndex];
		check(LODInfo.OverrideVertexColors == nullptr);

		LODInfo.OverrideVertexColors = new FColorVertexBuffer();
		LODInfo.OverrideVertexColors->InitFromColorArray(Colors.GetData(), Colors.Num());

#if WITH_EDITOR
		if (LODIndex > 0)
		{
			Component->bCustomOverrideVertexColorPerLOD = true;
		}
#endif

		BeginInitResource(LODInfo.OverrideVertexColors);
		return true;
	}

	void FinalizeVertexColorEdit(UStaticMeshComponent* Component)
	{
		if (!Component)
		{
			return;
		}

#if WITH_EDITOR
		// Snapshot painted-vertex data (position/normal/color) so the result persists cleanly and behaves like an
		// interactively mesh-painted component, then pair the Static Mesh DDC key so RequiresOverrideVertexColorsFixup
		// does not spuriously remap our exact procedural colors on the next load.
		Component->CachePaintedDataIfNecessary();
		if (const UStaticMesh* Mesh = Component->GetStaticMesh())
		{
			if (const FStaticMeshRenderData* RenderData = Mesh->GetRenderData())
			{
				Component->StaticMeshDerivedDataKey = RenderData->DerivedDataKey;
			}
		}
#endif

		Component->MarkRenderStateDirty();
	}
}
