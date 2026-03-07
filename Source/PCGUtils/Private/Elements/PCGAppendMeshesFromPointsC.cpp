// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGAppendMeshesFromPointsC.h"

#include "PCGContext.h"
#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGBasePointData.h"
#include "Elements/Metadata/PCGMetadataElementCommon.h"
#include "Helpers/PCGGeometryHelpers.h"
#include "GeometryTypes.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#if WITH_EDITOR
#include "Helpers/PCGDynamicTrackingHelpers.h"
#endif // WITH_EDITOR

#include "DynamicMeshEditor.h"
#include "UDynamicMesh.h"
#include "ConversionUtils/SceneComponentToDynamicMesh.h"
#include "DynamicMesh/MeshIndexMappings.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGAppendMeshesFromPointsC)

#define LOCTEXT_NAMESPACE "PCGAppendMeshesFromPointsCElement"

namespace PCGAppendMeshesFromPointsC
{
	static const FName InDynMeshPinLabel = TEXT("InDynMesh");
	static const FName InPointsPinLabel = TEXT("InPointsC");
	static const FName InAppendMeshPinLabel = TEXT("AppendDynMesh");
}

#if WITH_EDITOR
FName UPCGAppendMeshesFromPointsCSettings::GetDefaultNodeName() const
{
	return FName(TEXT("AppendMeshesFromPointsC"));
}

FText UPCGAppendMeshesFromPointsCSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Append Meshes From PointsC");
}

FText UPCGAppendMeshesFromPointsCSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Append meshes at the PointsC points transforms. Mesh can be a single static mesh, multiple meshes coming from the points or another dynamic mesh.");
}

void UPCGAppendMeshesFromPointsCSettings::GetStaticTrackedKeys(FPCGSelectionKeyToSettingsMap& OutKeysToSettings, TArray<TObjectPtr<const UPCGGraph>>& OutVisitedGraphs) const
{
	if (Mode != EPCGAppendMeshesFromPointsCMode::SingleStaticMesh || StaticMesh.IsNull() || IsPropertyOverriddenByPin(GET_MEMBER_NAME_CHECKED(UPCGAppendMeshesFromPointsCSettings, StaticMesh)))
	{
		return;
	}

	FPCGSelectionKey Key = FPCGSelectionKey::CreateFromPath(StaticMesh.ToSoftObjectPath());

	OutKeysToSettings.FindOrAdd(MoveTemp(Key)).Emplace(this, /*bCulling=*/false);
}

EPCGChangeType UPCGAppendMeshesFromPointsCSettings::GetChangeTypeForProperty(const FName& InPropertyName) const
{
	EPCGChangeType ChangeType = Super::GetChangeTypeForProperty(InPropertyName) | EPCGChangeType::Cosmetic;

	if (InPropertyName == GET_MEMBER_NAME_CHECKED(UPCGAppendMeshesFromPointsCSettings, Mode))
	{
		// If we change from/to DynamicMesh, this needs to trigger a graph recompilation.
		ChangeType |= EPCGChangeType::Structural;
	}

	return ChangeType;
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGAppendMeshesFromPointsCSettings::CreateElement() const
{
	return MakeShared<FPCGAppendMeshesFromPointsCElement>();
}

TArray<FPCGPinProperties> UPCGAppendMeshesFromPointsCSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace_GetRef(PCGAppendMeshesFromPointsC::InDynMeshPinLabel, EPCGDataType::DynamicMesh, false, false).SetRequiredPin();
	Properties.Emplace_GetRef(PCGAppendMeshesFromPointsC::InPointsPinLabel, EPCGDataType::Point, false, false).SetRequiredPin();

	if (Mode == EPCGAppendMeshesFromPointsCMode::DynamicMesh)
	{
		Properties.Emplace_GetRef(PCGAppendMeshesFromPointsC::InAppendMeshPinLabel, EPCGDataType::DynamicMesh, false, false).SetRequiredPin();
	}
	
	return Properties;
}

TArray<FPCGPinProperties> UPCGAppendMeshesFromPointsCSettings::OutputPinProperties() const
{ 
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh, false, false);
	return Properties;
}

bool FPCGAppendMeshesFromPointsCElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	// Without context, we can't know, so force it in the main thread to be safe.
	return !Context || Context->CurrentPhase == EPCGExecutionPhase::PrepareData;
}

FPCGContext* FPCGAppendMeshesFromPointsCElement::CreateContext()
{
	return new FPCGAppendMeshesFromPointsCContext();
}

bool FPCGAppendMeshesFromPointsCElement::PrepareDataInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAppendMeshesFromPointsCElement::Execute);

	FPCGAppendMeshesFromPointsCContext* Context = static_cast<FPCGAppendMeshesFromPointsCContext*>(InContext);
	check(Context);
	
	const UPCGAppendMeshesFromPointsCSettings* Settings = InContext->GetInputSettings<UPCGAppendMeshesFromPointsCSettings>();
	check(Settings);
	
	if (Context->WasLoadRequested())
	{
		return true;
	}

	if (Settings->Mode == EPCGAppendMeshesFromPointsCMode::SingleStaticMesh)
	{
		if (Settings->StaticMesh.IsNull())
		{
			return true;
		}
		
		Context->bPrepareDataSucceeded = true;
		return Context->RequestResourceLoad(Context, {Settings->StaticMesh.ToSoftObjectPath()}, !Settings->bSynchronousLoad);
	}

	if (Settings->Mode == EPCGAppendMeshesFromPointsCMode::StaticMeshFromAttribute)
	{
		const TArray<FPCGTaggedData> InputPoints = InContext->InputData.GetInputsByPin(PCGAppendMeshesFromPointsC::InPointsPinLabel);
		const UPCGBasePointData* InPointData = !InputPoints.IsEmpty() ? Cast<const UPCGBasePointData>(InputPoints[0].Data) : nullptr;

		if (!InPointData)
		{
			return true;
		}
		
		const FPCGAttributePropertyInputSelector Selector = Settings->MeshAttribute.CopyAndFixLast(InPointData);
		const TUniquePtr<const IPCGAttributeAccessor> Accessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, Selector);
		const TUniquePtr<const IPCGAttributeAccessorKeys> Keys = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, Selector);

		if (!Accessor || !Keys)
		{
			PCGLog::Metadata::LogFailToCreateAccessorError(Selector, Context);
			return true;
		}

		if (!PCG::Private::IsBroadcastableOrConstructible(Accessor->GetUnderlyingType(), PCG::Private::MetadataTypes<FSoftObjectPath>::Id))
		{
			PCGLog::Metadata::LogFailToGetAttributeError<FSoftObjectPath>(Selector, Accessor.Get(), Context);
			return true;
		}

		TArray<FSoftObjectPath> StaticMeshesToLoad;
		PCGMetadataElementCommon::ApplyOnAccessor<FSoftObjectPath>(*Keys, *Accessor, [Context, &StaticMeshesToLoad](const FSoftObjectPath& Path, int32 Index)
		{
			if (!Path.IsNull())
			{
				StaticMeshesToLoad.AddUnique(Path);
				Context->MeshToPointIndicesMapping.FindOrAdd(Path).Add(Index);
			}
		}, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);

		if (StaticMeshesToLoad.IsEmpty())
		{
			return true;
		}

		Context->bPrepareDataSucceeded = true;
		return Context->RequestResourceLoad(Context, std::move(StaticMeshesToLoad), !Settings->bSynchronousLoad);
	}
	
	Context->bPrepareDataSucceeded = true;
	return true;
}

bool FPCGAppendMeshesFromPointsCElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGAppendMeshesFromPointsCElement::Execute);

	FPCGAppendMeshesFromPointsCContext* Context = static_cast<FPCGAppendMeshesFromPointsCContext*>(InContext);
	check(Context);

	const UPCGAppendMeshesFromPointsCSettings* Settings = InContext->GetInputSettings<UPCGAppendMeshesFromPointsCSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> InputPoints = InContext->InputData.GetInputsByPin(PCGAppendMeshesFromPointsC::InPointsPinLabel);
	const TArray<FPCGTaggedData> InputDynMesh = InContext->InputData.GetInputsByPin(PCGAppendMeshesFromPointsC::InDynMeshPinLabel);
	const TArray<FPCGTaggedData> InputAppendDynMesh = InContext->InputData.GetInputsByPin(PCGAppendMeshesFromPointsC::InAppendMeshPinLabel);

	const UPCGBasePointData* InPointData = !InputPoints.IsEmpty() ? Cast<const UPCGBasePointData>(InputPoints[0].Data) : nullptr;
	const UPCGDynamicMeshData* InDynMeshData = !InputDynMesh.IsEmpty() ? Cast<const UPCGDynamicMeshData>(InputDynMesh[0].Data) : nullptr;
	const UPCGDynamicMeshData* InAppendDynMeshData = !InputAppendDynMesh.IsEmpty() ? Cast<const UPCGDynamicMeshData>(InputAppendDynMesh[0].Data) : nullptr;

	if (!InPointData || !InDynMeshData || !Context->bPrepareDataSucceeded)
	{
		InContext->OutputData.TaggedData = InputDynMesh;
		return true;
	}

	FPCGTaggedData& OutputData = InContext->OutputData.TaggedData.Emplace_GetRef(InputDynMesh[0]);
	if (InPointData->IsEmpty())
	{
		return true;
	}

	UPCGDynamicMeshData* OutDynMeshData = CopyOrSteal(InputDynMesh[0], InContext);
	if (!OutDynMeshData)
	{
		return true;
	}
	
	OutputData.Data = OutDynMeshData;

	TMap<FSoftObjectPath, UE::Geometry::FDynamicMesh3> StaticMeshToDynMesh;

	

	auto ConvertStaticMesh = [Settings, &StaticMeshToDynMesh, InContext, &OutDynMeshData](const TSoftObjectPtr<UStaticMesh>& InMesh) -> bool
	{
		UE::Conversion::FStaticMeshConversionOptions ConversionOptions{};
		FText ErrorMessage;
		UE::Conversion::EMeshLODType LODType = static_cast<UE::Conversion::EMeshLODType>(Settings->RequestedLODType);
		
		UE::Geometry::FDynamicMesh3& NewMesh = StaticMeshToDynMesh.Add(InMesh.ToSoftObjectPath());
		UStaticMesh* StaticMesh = InMesh.LoadSynchronous();
	
		if (!UE::Conversion::StaticMeshToDynamicMesh(StaticMesh, NewMesh, ErrorMessage, ConversionOptions, LODType, Settings->RequestedLODIndex))
		{
			PCGLog::LogErrorOnGraph(ErrorMessage, InContext);
			return false;
		}

		// Then do the remapping if needed
		//if (Settings->bExtractMaterials)
		//{
		//	TArray<UMaterialInterface*> StaticMeshMaterials;
		//	TArray<FName> MaterialSlotNames;
		//	UGeometryScriptLibrary_StaticMeshFunctions::GetMaterialListFromStaticMesh(StaticMesh, StaticMeshMaterials, MaterialSlotNames);
		//
		//	//if (!StaticMeshMaterials.IsEmpty() && StaticMeshMaterials != OutDynMeshData->GetMaterials())
		//	//{
		//	//	PCGGeometryHelpers::RemapMaterials(NewMesh, StaticMeshMaterials, OutDynMeshData->GetMutableMaterials());
		//	//}
		//}

		return true;
	};

	switch (Settings->Mode)
	{
	case EPCGAppendMeshesFromPointsCMode::SingleStaticMesh:
	{
		if (!ConvertStaticMesh(Settings->StaticMesh))
		{
			return true;
		}

#if WITH_EDITOR
		if (Context->IsValueOverriden(GET_MEMBER_NAME_CHECKED(UPCGAppendMeshesFromPointsCSettings, StaticMesh)))
		{
			FPCGDynamicTrackingHelper::AddSingleDynamicTrackingKey(Context, FPCGSelectionKey::CreateFromPath(Settings->StaticMesh.ToSoftObjectPath()), /*bCulled=*/false);
		}
#endif // WITH_EDITOR

		break;
	}
	case EPCGAppendMeshesFromPointsCMode::StaticMeshFromAttribute:
	{
#if WITH_EDITOR
		FPCGDynamicTrackingHelper DynamicTracking;
		DynamicTracking.EnableAndInitialize(Context, Context->MeshToPointIndicesMapping.Num());
#endif // WITH_EDITOR

		StaticMeshToDynMesh.Reserve(Context->MeshToPointIndicesMapping.Num());
	
		for (TPair<FSoftObjectPath, TArray<int32>>& It : Context->MeshToPointIndicesMapping)
		{
			if (!ConvertStaticMesh(TSoftObjectPtr<UStaticMesh>(It.Key)))
			{
				return true;
			}

#if WITH_EDITOR
		DynamicTracking.AddToTracking(FPCGSelectionKey::CreateFromPath(It.Key), /*bCulled=*/false);
#endif // WITH_EDITOR
		}

#if WITH_EDITOR
		DynamicTracking.Finalize(Context);
#endif // WITH_EDITOR

		break;
	}
	case EPCGAppendMeshesFromPointsCMode::DynamicMesh:
	{
		if (!InAppendDynMeshData)
		{
			PCGLog::InputOutput::LogTypedDataNotFoundWarning(EPCGDataType::DynamicMesh, PCGAppendMeshesFromPointsC::InAppendMeshPinLabel, Context);
			return true;
		}

		// Remap materials if needed
		const TArray<TObjectPtr<UMaterialInterface>>& InputMaterials = InAppendDynMeshData->GetMaterials();
		TArray<TObjectPtr<UMaterialInterface>>& OutputMaterials = OutDynMeshData->GetMutableMaterials();
		//if (!InputMaterials.IsEmpty() && OutputMaterials != InputMaterials)
		//{
		//	// If we have a remap to do, we will need to copy or steal the append mesh.
		//	UPCGDynamicMeshData* InAppendDynamicMeshDataMutable = CopyOrSteal(InputAppendDynMesh[0], InContext);
		//	PCGGeometryHelpers::RemapMaterials(InAppendDynamicMeshDataMutable->GetMutableDynamicMesh()->GetMeshRef(), InputMaterials, OutputMaterials);
		//	InAppendDynMeshData = InAppendDynamicMeshDataMutable;
		//}

		break;
	}
	default:
		return true;
	}
	
	UE::Geometry::FMeshIndexMappings MeshIndexMappings;
	UE::Geometry::FDynamicMeshEditor Editor(OutDynMeshData->GetMutableDynamicMesh()->GetMeshPtr());

	// Get color attribute accessor if available
	TUniquePtr<const IPCGAttributeAccessor> ColorAccessor;
	TUniquePtr<const IPCGAttributeAccessorKeys> ColorKeys;
	bool bHasColorAttribute = false;


	ColorAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(InPointData, Settings->ColorAttribute);
	ColorKeys = PCGAttributeAccessorHelpers::CreateConstKeys(InPointData, Settings->ColorAttribute);

	if (ColorAccessor && ColorKeys)
	{
		bHasColorAttribute =
			PCG::Private::IsBroadcastableOrConstructible(ColorAccessor->GetUnderlyingType(), PCG::Private::MetadataTypes<FVector4>::Id) ||
			PCG::Private::IsBroadcastableOrConstructible(ColorAccessor->GetUnderlyingType(), PCG::Private::MetadataTypes<FLinearColor>::Id);
	}

	if (bHasColorAttribute)
	{
		OutDynMeshData->GetMutableDynamicMesh()->GetMeshPtr()->EnableAttributes();
		OutDynMeshData->GetMutableDynamicMesh()->GetMeshPtr()->Attributes()->EnablePrimaryColors();
	}

	auto AppendMesh = [&Editor, &MeshIndexMappings](const UE::Geometry::FDynamicMesh3* MeshToAppend, const FTransform& PointTransform)
		{
			Editor.AppendMesh(MeshToAppend, MeshIndexMappings,
				[&PointTransform](int, const FVector& Position) { return PointTransform.TransformPosition(Position); },
				[&PointTransform](int, const FVector& Normal)
				{
					const FVector& PointScale = PointTransform.GetScale3D();
					const double DetSign = FMathd::SignNonZero(PointScale.X * PointScale.Y * PointScale.Z);
					const FVector SafeInversePointScale(PointScale.Y * PointScale.Z * DetSign, PointScale.X * PointScale.Z * DetSign, PointScale.X * PointScale.Y * DetSign);
					return PointTransform.TransformVectorNoScale((SafeInversePointScale * Normal).GetSafeNormal());
				});
		};
	
	const TConstPCGValueRange<FTransform> TransformRange = InPointData->GetConstTransformValueRange();
	UE::Geometry::FDynamicMesh3* OutMesh = OutDynMeshData->GetMutableDynamicMesh()->GetMeshPtr();
	auto SetVertexColorsForAppendedMesh = [&MeshIndexMappings, &OutMesh, bHasColorAttribute, &ColorAccessor, &ColorKeys, &Context](int32 PointIndex)
		{
			
			if (!bHasColorAttribute || !OutMesh->HasAttributes() || !OutMesh->Attributes()->PrimaryColors())
			{
				if(!bHasColorAttribute)
					PCGLog::LogErrorOnGraph(LOCTEXT("UnableToSetVertexColorsForAppendedMesh", "Points Missing Color Attribute"), Context);
				
				if(!OutMesh->HasAttributes())
					PCGLog::LogErrorOnGraph(LOCTEXT("OutMeshMissingAttributes", "OutMesh Missing Attributes"), Context);

				if (!OutMesh->Attributes()->PrimaryColors())
					PCGLog::LogErrorOnGraph(LOCTEXT("OutMeshMissingColorAttributes", "OutMesh Missing Color Attributes"), Context);
				return;
			}

			// Get color for this point
			FLinearColor PointColor(1.0f, 1.0f, 1.0f, 1.0f); // Default white
			ColorAccessor->Get<FLinearColor>(PointColor, PointIndex, *ColorKeys, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible);
			FVector4f VertexColor(PointColor.R, PointColor.G, PointColor.B, PointColor.A);

			UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = OutMesh->Attributes()->PrimaryColors();
			
			// The MeshIndexMappings.GetTriangleMap() contains mapping of appended triangles
			// For each newly added triangle, set the color for its vertices
			auto& TriangleMap = MeshIndexMappings.GetTriangleMap();
			for (const TPair<int32, int32>& TriPair : TriangleMap.GetForwardMap())
			{
				int32 NewTriID = TriPair.Value;
				if (OutMesh->IsTriangle(NewTriID))
				{
					for (int i = 0; i < 3; i++)
					{
						// Check if this vertex already has a color element
						if (ColorOverlay->IsSetTriangle(NewTriID))
						{
							UE::Geometry::FIndex3i ElementTri = ColorOverlay->GetTriangle(NewTriID);
							ColorOverlay->SetElement(ElementTri[i], VertexColor);
						}
						else
						{
							UE::Geometry::FIndex3i ColorElements;
							for (int j = 0; j < 3; j++)
							{
								ColorElements[j] = ColorOverlay->AppendElement(VertexColor);
							}
							ColorOverlay->SetTriangle(NewTriID, ColorElements);
							break;
						}
					}
				}
			}
			
		};



	auto AppendMeshWithColor = [&Editor, &OutMesh, bHasColorAttribute, &InPointData]
	(const UE::Geometry::FDynamicMesh3* MeshToAppend, const FTransform& PointTransform, int32 PointIndex)
		{
			// Get the current triangle count before appending
			int32 TriCountBefore = OutMesh->TriangleCount();
			int32 MaxTriIDBefore = OutMesh->MaxTriangleID();

			UE::Geometry::FMeshIndexMappings MeshIndexMappings;
			
			Editor.AppendMesh(MeshToAppend, MeshIndexMappings,
				[&PointTransform](int, const FVector& Position) { return PointTransform.TransformPosition(Position); },
				[&PointTransform](int, const FVector& Normal)
				{
					const FVector& PointScale = PointTransform.GetScale3D();
					const double DetSign = FMathd::SignNonZero(PointScale.X * PointScale.Y * PointScale.Z);
					const FVector SafeInversePointScale(PointScale.Y * PointScale.Z * DetSign, PointScale.X * PointScale.Z * DetSign, PointScale.X * PointScale.Y * DetSign);
					return PointTransform.TransformVectorNoScale((SafeInversePointScale * Normal).GetSafeNormal());
				});

			// Now apply colors to the newly added triangles
			if (bHasColorAttribute && OutMesh->HasAttributes() && OutMesh->Attributes()->PrimaryColors())
			{
				// Alternative: Get the point directly and read its color
				auto c2 = InPointData->GetPointPropertyValue<EPCGPointNativeProperties::Color>(PointIndex);

				FVector4f VertexColor(c2.X, c2.Y, c2.Z, c2.W);
				UE_LOG(LogTemp, Warning, TEXT("Setting color for point %d: R=%.2f G=%.2f B=%.2f A=%.2f"), PointIndex, VertexColor.X, VertexColor.Y, VertexColor.Z, VertexColor.W);

				UE::Geometry::FDynamicMeshColorOverlay* ColorOverlay = OutMesh->Attributes()->PrimaryColors();

				// Iterate through newly added triangles using the triangle map
				const auto& TriangleMap = MeshIndexMappings.GetTriangleMap();
				UE_LOG(LogTemp, Warning, TEXT("Set colors for %d triangles"), TriangleMap.GetForwardMap().Num());
				for (const TPair<int32, int32>& TriPair : TriangleMap.GetForwardMap())
				{
					int32 NewTriID = TriPair.Value;

					if (OutMesh->IsTriangle(NewTriID))
					{
						// Inside the color setting section
						
						
						// Check if this triangle already has color elements set
						if (!ColorOverlay->IsSetTriangle(NewTriID))
						{
							// Create new color elements for all 3 vertices
							UE::Geometry::FIndex3i ColorElements;
							 
							ColorElements[0] = ColorOverlay->AppendElement(VertexColor);
							ColorElements[1] = ColorOverlay->AppendElement(VertexColor);
							ColorElements[2] = ColorOverlay->AppendElement(VertexColor);
							ColorOverlay->SetTriangle(NewTriID, ColorElements);
						}
						else
						{
							// Triangle already has colors, update them
							UE::Geometry::FIndex3i c = ColorOverlay->GetTriangle(NewTriID);
							ColorOverlay->SetElement(c[0], VertexColor);
							ColorOverlay->SetElement(c[1], VertexColor);
							ColorOverlay->SetElement(c[2], VertexColor);
						}
					}
				}
			}
		};


	if (Settings->Mode == EPCGAppendMeshesFromPointsCMode::StaticMeshFromAttribute)
	{
		for (TPair<FSoftObjectPath, TArray<int32>>& It : Context->MeshToPointIndicesMapping)
		{
			const UE::Geometry::FDynamicMesh3* MeshToAppend = &StaticMeshToDynMesh[It.Key];
			check(MeshToAppend);

			for (const int32 i : It.Value)
			{
				AppendMesh(MeshToAppend, TransformRange[i]);
			}
		}
	}
	else
	{
		const UE::Geometry::FDynamicMesh3* MeshToAppend = nullptr;
		if (Settings->Mode == EPCGAppendMeshesFromPointsCMode::SingleStaticMesh)
		{
			MeshToAppend = &StaticMeshToDynMesh[Settings->StaticMesh.ToSoftObjectPath()];
		}
		else
		{
			MeshToAppend = InAppendDynMeshData->GetDynamicMesh() ? InAppendDynMeshData->GetDynamicMesh()->GetMeshPtr() : nullptr;
		}

		if (!MeshToAppend)
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("InvalidDynMeshToAppend", "Invalid Dynamic mesh to append"), Context);
			return true;
		}
		int32 PointIndex = 0;
		for (const FTransform& PointTransform : TransformRange)
		{
			AppendMeshWithColor(MeshToAppend, PointTransform, PointIndex++);
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
