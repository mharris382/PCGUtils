// Copyright Max Harris

#include "StaticMesh/PCGPaintStaticMeshVertexColor.h"

#include "PCGUtilsPainter.h"
#include "Factories/PCGUtilsDynMeshPainterFactory.h"
#include "StaticMesh/PCGUtilsPainterStaticMeshBackend.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Elements/PCGAddComponent.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Utils/PCGLogErrors.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#define LOCTEXT_NAMESPACE "PCGPaintStaticMeshVertexColor"

namespace
{
	const FName StaticMeshTargetPin = TEXT("Target");
	const FName StaticMeshPainterPin = TEXT("Painter");

	EPCGUtilsDynMeshPainterColorChannel GetStaticMeshWriteChannels(const FGeometryScriptColorFlags& Flags)
	{
		EPCGUtilsDynMeshPainterColorChannel Channels = EPCGUtilsDynMeshPainterColorChannel::None;
		if (Flags.bRed) Channels |= EPCGUtilsDynMeshPainterColorChannel::Red;
		if (Flags.bGreen) Channels |= EPCGUtilsDynMeshPainterColorChannel::Green;
		if (Flags.bBlue) Channels |= EPCGUtilsDynMeshPainterColorChannel::Blue;
		if (Flags.bAlpha) Channels |= EPCGUtilsDynMeshPainterColorChannel::Alpha;
		return Channels;
	}

	FORCEINLINE uint8 QuantizeStaticMeshUNorm(float Value)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255));
	}

	PCGUtilsPainterStaticMeshBackend::EBaseColorMode ToStaticMeshBackendBaseColor(EPCGPaintStaticMeshBaseColor Mode)
	{
		using EBackend = PCGUtilsPainterStaticMeshBackend::EBaseColorMode;
		switch (Mode)
		{
		case EPCGPaintStaticMeshBaseColor::AssetVertexColors: return EBackend::AssetVertexColors;
		case EPCGPaintStaticMeshBaseColor::White:             return EBackend::White;
		case EPCGPaintStaticMeshBaseColor::Black:             return EBackend::Black;
		case EPCGPaintStaticMeshBaseColor::Existing:
		default:                                              return EBackend::Existing;
		}
	}
}

UPCGPaintStaticMeshVertexColorSettings::UPCGPaintStaticMeshVertexColorSettings()
{
	TargetComponentAttribute.SetAttributeName(PCGAddComponentConstants::ComponentReferenceAttribute);
}

#if WITH_EDITOR
FText UPCGPaintStaticMeshVertexColorSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Paint Static Mesh Vertex Colors");
}

FText UPCGPaintStaticMeshVertexColorSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Applies a Painter to the per-component override vertex colors of existing Static Mesh Components referenced "
		"by a soft-object-path attribute. Does not modify the Static Mesh asset. Editor-authoring only. "
		"Nanite and ISM/HISM components are skipped with a warning.");
}
#endif

TArray<FPCGPinProperties> UPCGPaintStaticMeshVertexColorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(StaticMeshTargetPin, EPCGDataType::Point | EPCGDataType::Param).SetRequiredPin();
	Pins.Emplace_GetRef(StaticMeshPainterPin, FPCGUtilsDynMeshPainterFactoryDataTypeInfo::AsId(), false, false).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGPaintStaticMeshVertexColorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	FPCGPinProperties& DependencyPin = Pins.Emplace_GetRef(PCGPinConstants::DefaultExecutionDependencyLabel, EPCGDataType::Any);
#if WITH_EDITOR
	DependencyPin.Tooltip = PCGPinConstants::Tooltips::ExecutionDependencyTooltip;
#endif
	DependencyPin.Usage = EPCGPinUsage::DependencyOnly;
	return Pins;
}

FPCGElementPtr UPCGPaintStaticMeshVertexColorSettings::CreateElement() const
{
	return MakeShared<FPCGPaintStaticMeshVertexColorElement>();
}

bool FPCGPaintStaticMeshVertexColorElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGPaintStaticMeshVertexColorElement::Execute);
	check(Context);

	const UPCGPaintStaticMeshVertexColorSettings* Settings =
		Context->GetInputSettings<UPCGPaintStaticMeshVertexColorSettings>();
	check(Settings);

	// Resolve the single Painter.
	const UPCGUtilsDynMeshPainterFactoryData* PainterFactory = nullptr;
	if (!PCGUtilsDynMeshPainterFactories::GetSinglePainter(Context, StaticMeshPainterPin, PainterFactory, /*bRequired=*/true))
	{
		return true;
	}

	// Collect unique component soft-object-paths from the Target input.
	TArray<FSoftObjectPath> ComponentPaths;
	{
		TSet<FSoftObjectPath> Seen;
		for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(StaticMeshTargetPin))
		{
			if (!Input.Data)
			{
				continue;
			}

			TArray<FSoftObjectPath> DataPaths;
			if (!PCGAttributeAccessorHelpers::ExtractAllValues(Input.Data, Settings->TargetComponentAttribute, DataPaths, Context))
			{
				continue;
			}

			for (FSoftObjectPath& Path : DataPaths)
			{
				if (!Path.IsNull() && !Seen.Contains(Path))
				{
					Seen.Add(Path);
					ComponentPaths.Add(MoveTemp(Path));
				}
			}
		}
	}

	if (ComponentPaths.IsEmpty())
	{
		PCGLog::LogWarningOnGraph(
			LOCTEXT("NoTargets", "Paint Static Mesh Vertex Colors resolved no component references from its Target input."),
			Context);
		return true;
	}

	// Build the Painter operation once. The evaluation context is geometry-agnostic; a DynMesh-only Painter
	// (Points to Painter) rejects it in Initialize.
	TSharedPtr<FPCGUtilsDynMeshPainterOperation> Operation = PainterFactory->CreateOperation(Context);
	const FPCGUtilsDynMeshPainterEvaluationContext PainterContext(FTransform::Identity);
	if (!Operation || !Operation->Initialize(PainterContext))
	{
		PCGLog::LogErrorOnGraph(
			LOCTEXT("PainterInitFailed", "Paint Static Mesh Vertex Colors could not initialize its Painter. Points to Painter is Dynamic Mesh-only and cannot target a Static Mesh Component."),
			Context);
		return true;
	}

	const EPCGUtilsDynMeshPainterColorChannel RequestedChannels = GetStaticMeshWriteChannels(Settings->WriteChannels);
	const PCGUtilsPainterStaticMeshBackend::EBaseColorMode BaseColorMode = ToStaticMeshBackendBaseColor(Settings->BaseColor);

#if WITH_EDITOR
	const bool bUseTransactions = Context->ExecutionSource.Get()
		&& Context->ExecutionSource->GetExecutionState().UseTransactions();
	FScopedTransaction Transaction(LOCTEXT("PaintSMTransaction", "Paint Static Mesh Vertex Colors"), bUseTransactions);
#endif

	int32 UnresolvedCount = 0;
	int32 PaintedComponentCount = 0;

	for (const FSoftObjectPath& Path : ComponentPaths)
	{
		UObject* Object = Path.ResolveObject();
		if (!Object)
		{
			Object = Path.TryLoad();
		}
		if (!Object)
		{
			++UnresolvedCount;
			continue;
		}

		UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(Object);
		if (!Component)
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NotStaticMeshComponent", "'{0}' is not a Static Mesh Component and was skipped."),
				FText::FromString(Path.ToString())), Context);
			continue;
		}

		if (Component->IsA<UInstancedStaticMeshComponent>())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("InstancedComponent", "'{0}' is an Instanced Static Mesh component. Override vertex colors are one buffer shared by every instance, so this node cannot paint individual instances — use Per Instance Custom Data. Skipped."),
				FText::FromString(Path.ToString())), Context);
			continue;
		}

		if (!Component->GetStaticMesh())
		{
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NoStaticMesh", "'{0}' has no Static Mesh assigned and was skipped."),
				FText::FromString(Path.ToString())), Context);
			continue;
		}

		if (Component->HasValidNaniteData())
		{
			if (!Settings->bPaintNaniteComponents)
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("NaniteComponentSkipped", "'{0}' renders with Nanite. The Nanite raster path ignores per-component override vertex colors — use a Mesh Paint Texture for Nanite. Skipped; enable 'Paint Nanite Components' to paint anyway."),
					FText::FromString(Path.ToString())), Context);
				continue;
			}
			PCGLog::LogWarningOnGraph(FText::Format(
				LOCTEXT("NaniteComponentPainted", "'{0}' renders with Nanite: the painted override vertex colors affect ray tracing, the non-Nanite fallback mesh, and Nanite debug views, but not the Nanite raster render."),
				FText::FromString(Path.ToString())), Context);
		}

		const FTransform ComponentToWorld = Component->GetComponentTransform();
		const FMatrix NormalMatrix = ComponentToWorld.ToMatrixWithScale().Inverse().GetTransposed();

		const int32 NumLODs = PCGUtilsPainterStaticMeshBackend::GetNumLODs(Component);
		const int32 MaxLOD = (Settings->LODMode == EPCGPaintStaticMeshLODMode::LOD0Only) ? FMath::Min(1, NumLODs) : NumLODs;

		Component->Modify();

		bool bWroteAnyLOD = false;
		for (int32 LODIndex = 0; LODIndex < MaxLOD; ++LODIndex)
		{
			TArray<FVector3f> Positions;
			TArray<FVector3f> Normals;
			if (!PCGUtilsPainterStaticMeshBackend::GetLODRenderVertices(Component, LODIndex, Positions, Normals))
			{
				PCGLog::LogWarningOnGraph(FText::Format(
					LOCTEXT("LODVerticesUnavailable", "'{0}' LOD {1}: render-vertex data is unavailable and was skipped."),
					FText::FromString(Component->GetName()), FText::AsNumber(LODIndex)), Context);
				continue;
			}

			const int32 NumVertices = Positions.Num();

			TArray<FColor> Colors;
			PCGUtilsPainterStaticMeshBackend::GetBaseLODColors(Component, LODIndex, BaseColorMode, Colors);
			if (Colors.Num() != NumVertices)
			{
				Colors.Init(FColor::White, NumVertices);
			}

			for (int32 Index = 0; Index < NumVertices; ++Index)
			{
				FPCGUtilsDynMeshPainterSample Sample;
				Sample.LocalPosition = FVector(Positions[Index]);
				Sample.WorldPosition = ComponentToWorld.TransformPosition(Sample.LocalPosition);
				Sample.LocalNormal = FVector(Normals[Index]).GetSafeNormal();
				Sample.WorldNormal = NormalMatrix.TransformVector(Sample.LocalNormal).GetSafeNormal();
				Sample.VertexID = Index;

				const FColor Base = Colors[Index];
				FVector4f Value(Base.R / 255.0f, Base.G / 255.0f, Base.B / 255.0f, Base.A / 255.0f);
				PCGUtilsDynMeshPainters::ResolveValueToColor(Operation->Evaluate(Sample), RequestedChannels, Value);

				Colors[Index] = Settings->bConvertToSRGB
					? FLinearColor(Value.X, Value.Y, Value.Z, Value.W).ToFColor(/*bSRGB=*/true)
					: FColor(QuantizeStaticMeshUNorm(Value.X), QuantizeStaticMeshUNorm(Value.Y), QuantizeStaticMeshUNorm(Value.Z), QuantizeStaticMeshUNorm(Value.W));
			}

			if (PCGUtilsPainterStaticMeshBackend::SetOverrideVertexColorsForLOD(Component, LODIndex, Colors))
			{
				bWroteAnyLOD = true;
			}
		}

		if (bWroteAnyLOD)
		{
			PCGUtilsPainterStaticMeshBackend::FinalizeVertexColorEdit(Component);
			++PaintedComponentCount;
		}
	}

	if (!Settings->bSilenceUnresolvedPathWarning && UnresolvedCount > 0)
	{
		PCGLog::LogWarningOnGraph(FText::Format(
			LOCTEXT("UnresolvedPaths", "{0} component reference(s) were empty or their owning actor is not loaded, and were skipped."),
			FText::AsNumber(UnresolvedCount)), Context);
	}

	UE_LOG(LogPCGUtilsPainter, Verbose,
		TEXT("Paint Static Mesh Vertex Colors: painted %d component(s)."), PaintedComponentCount);
	return true;
}

#undef LOCTEXT_NAMESPACE
