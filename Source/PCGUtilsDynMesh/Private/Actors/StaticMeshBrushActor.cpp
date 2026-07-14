// Copyright Max Harris

#include "Actors/StaticMeshBrushActor.h"

#include "Components/StaticMeshBrushComponent.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "UDynamicMesh.h"

AStaticMeshBrushActor::AStaticMeshBrushActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	StaticMeshBrushComponent = CreateDefaultSubobject<UStaticMeshBrushComponent>(TEXT("StaticMeshBrushComponent"));
	RootComponent = StaticMeshBrushComponent;
}

UDynamicMesh* AStaticMeshBrushActor::RetrieveDynamicMesh_Implementation() const
{
	if (!StaticMeshBrushComponent)
	{
		return nullptr;
	}

	UStaticMesh* StaticMesh = StaticMeshBrushComponent->GetStaticMesh();
	if (!StaticMesh)
	{
		return nullptr;
	}

	UDynamicMesh* DynamicMesh = NewObject<UDynamicMesh>(GetTransientPackageAsObject());
	if (!DynamicMesh)
	{
		return nullptr;
	}

	FGeometryScriptCopyMeshFromAssetOptions AssetOptions;
	FGeometryScriptMeshReadLOD RequestedLOD;
	EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;

	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(
		StaticMesh,
		DynamicMesh,
		AssetOptions,
		RequestedLOD,
		Outcome,
		/*bUseSectionMaterials=*/true);
	
	return Outcome == EGeometryScriptOutcomePins::Success ? DynamicMesh : nullptr;
}
