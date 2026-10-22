#include "Elements/PCGDecomposeClosedPath.h"
#include "Geometry/ClosedPathDecomposition.h"
#include "PCGContext.h"
#include "Data/PCGPointArrayData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataDomain.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGDecomposeClosedPath)

#define LOCTEXT_NAMESPACE "PCGDecomposeClosedPath"
#if WITH_EDITOR
FText UPCGDecomposeClosedPathSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("Title", "Path | Decompose Closed");
}
FText UPCGDecomposeClosedPathSettings::GetNodeTooltipText() const
{
	return LOCTEXT("Tooltip", "Experimental 3D decomposition of ordered closed point paths into smaller closed PointArrayData paths. Supports overhangs and uses existing vertices only. Each input is an independent closed boundary. Requires a true Data-domain closure attribute (default IsClosed). If decomposition is unavailable, the input path is preserved with a warning; internal surface checks never discard it.");
}
TArray<FPCGSettingsOverridableParam> UPCGDecomposeClosedPathSettings::GatherOverridableParams() const
{
	auto Params = Super::GatherOverridableParams();
	PCGUtilsInputShorthands::ApplyOverrideLabels(Params);
	return Params;
}
#endif
TArray<FPCGPinProperties> UPCGDecomposeClosedPathSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point).SetRequiredPin();
	return Pins;
}
TArray<FPCGPinProperties> UPCGDecomposeClosedPathSettings::OutputPinProperties() const
{
	return {FPCGPinProperties(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point)};
}
FPCGElementPtr UPCGDecomposeClosedPathSettings::CreateElement() const
{
	return MakeShared<FPCGDecomposeClosedPathElement>();
}
bool FPCGDecomposeClosedPathElement::ExecuteInternal(FPCGContext* Context) const
{
	const auto* Settings = Context->GetInputSettings<UPCGDecomposeClosedPathSettings>();
	if (!Settings)
	{
		for (auto Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
		{ Input.Pin=PCGPinConstants::DefaultOutputLabel; Context->OutputData.TaggedData.Add(MoveTemp(Input)); }
		PCGLog::LogErrorOnGraph(LOCTEXT("MissingSettings", "Decompose Closed settings are unavailable; inputs forwarded unchanged."), Context);
		return true;
	}
	const auto Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	for (int32 Dataset = 0; Dataset < Inputs.Num(); ++Dataset)
	{
		const FPCGTaggedData& Input = Inputs[Dataset];
		const auto* Source = Cast<UPCGBasePointData>(Input.Data);
		const int32 OutputStart = Context->OutputData.TaggedData.Num();
		// Every recoverable exit goes through this handler. Never discard the source
		// because the heuristic or a setting could not produce a decomposition.
		auto Fail = [&](const FString& Reason, bool bContractViolation = false)
		{
			Context->OutputData.TaggedData.SetNum(OutputStart);
			auto& Tagged = Context->OutputData.TaggedData.Emplace_GetRef(Input);
			Tagged.Pin = PCGPinConstants::DefaultOutputLabel;
			if (Source)
			{
				auto* Preserved = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
				Preserved->InitializeFromData(Source);
				TArray<int32> Indices; Indices.Reserve(Source->GetNumPoints());
				for (int32 I=0; I<Source->GetNumPoints(); ++I) Indices.Add(I);
				Preserved->SetPointsFrom(Source, MakeArrayView(Indices));
				if (Settings->bOutputIsCut && !Settings->IsCutAttributeName.IsNone() && FPCGMetadataAttributeBase::IsValidName(Settings->IsCutAttributeName))
				{
					// No new cuts on pass-through, even if the source had an old IsCut attribute.
					auto* Elements=Preserved->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
					Elements->DeleteAttribute(Settings->IsCutAttributeName);
					Elements->CreateAttribute<bool>(Settings->IsCutAttributeName, false, false);
				}
				Tagged.Data=Preserved;
			}
			const FText Message=FText::FromString(FString::Printf(TEXT("Input dataset %d: forwarded without decomposition. %s"),Dataset,*Reason));
			if (bContractViolation) PCGLog::LogErrorOnGraph(Message,Context);
			else PCGLog::LogWarningOnGraph(Message,Context);
		};
		if (!Source) { Fail(TEXT("Data type is not Point; expected ordered point path data."), true); continue; }
		if (Settings->ClosedAttributeName.IsNone()) { Fail(TEXT("Closed Attribute Name=None; expected a Data-domain bool attribute name.")); continue; }
		if (!FPCGMetadataAttributeBase::IsValidName(Settings->ClosedAttributeName))
        { Fail(FString::Printf(TEXT("Closed Attribute Name='%s'; expected a valid bare metadata attribute name."), *Settings->ClosedAttributeName.ToString()), true); continue; }
        if (Settings->bOutputIsCut && !Settings->IsCutAttributeName.IsNone() && !FPCGMetadataAttributeBase::IsValidName(Settings->IsCutAttributeName))
        { Fail(FString::Printf(TEXT("Is Cut Attribute Name='%s'; expected a valid bare metadata attribute name."), *Settings->IsCutAttributeName.ToString())); continue; }
        const auto* DataDomain = Source->Metadata->GetConstMetadataDomain(PCGMetadataDomainID::Data);
		const auto* Closure = DataDomain ? DataDomain->GetConstAttribute(Settings->ClosedAttributeName) : nullptr;
		if (!Closure) { Fail(FString::Printf(TEXT("@Data.%s is missing; expected a required bool attribute."), *Settings->ClosedAttributeName.ToString()), true); continue; }
		if (Closure->GetTypeId() != PCG::Private::MetadataTypes<bool>::Id)
		{
			Fail(FString::Printf(TEXT("@Data.%s has type ID %d; expected bool (type ID %d)."), *Settings->ClosedAttributeName.ToString(), Closure->GetTypeId(), PCG::Private::MetadataTypes<bool>::Id), true); continue;
		}
		if (!static_cast<const FPCGMetadataAttribute<bool>*>(Closure)->GetValueFromItemKey(DataDomain->GetItemCountForChild() > 0 ? PCGFirstEntryKey : PCGInvalidEntryKey))
		{
			Fail(FString::Printf(TEXT("@Data.%s=false; expected true for a closed path."), *Settings->ClosedAttributeName.ToString()), true); continue;
		}
		const bool bWriteCut = Settings->bOutputIsCut && !Settings->IsCutAttributeName.IsNone();
		if (Settings->bOutputIsCut && !bWriteCut)
		{
			PCGLog::LogWarningOnGraph(LOCTEXT("MissingCutName", "Is Cut Attribute Name=None; optional point bool output is skipped. Set a nonempty attribute name."), Context);
		}
		ClosedPathDecomposition::FOptions Options;
		Options.bBiasFlatCuts = Settings->bBiasFlatCuts;
		FText ReadError;
		bool bOptionsValid = true;
		auto Read = [&](const auto& Setting, auto& Value, const TCHAR* Label)
		{
			if (!bOptionsValid) return;
			if (!Setting.TryReadDataValue(Source, Value, ReadError, FText::FromString(Label), Dataset))
			{
				bOptionsValid = false;
			}
		};
		Read(Settings->AreaWeight, Options.AreaWeight, TEXT("Area Weight"));
		Read(Settings->LengthWeight, Options.LengthWeight, TEXT("Length Weight"));
		Read(Settings->MaxConcavity, Options.Concavity, TEXT("Max Concavity"));
		Read(Settings->MaxQuadWarp, Options.QuadWarp, TEXT("Max Quad Warp"));
		Read(Settings->MaxPolygonWarp, Options.PolygonWarp, TEXT("Max Polygon Warp"));
		Read(Settings->MaxInputPoints, Options.MaxPoints, TEXT("Max Input Points"));
		if (!bOptionsValid) { Fail(ReadError.ToString()); continue; }
		TArray<FVector> Positions;
		for (const FTransform& Transform : Source->GetConstTransformValueRange()) Positions.Add(Transform.GetLocation());
		TArray<ClosedPathDecomposition::FLoop> Loops;
		FString Error;
		bool bFinite = true;
		for (int32 I=0; I<Positions.Num() && bFinite; ++I) for (int32 Axis=0; Axis<3; ++Axis)
			if (!FMath::IsFinite(Positions[I][Axis]))
			{ Fail(FString::Printf(TEXT("Point %d coordinate %d=%g; expected finite."),I,Axis,Positions[I][Axis]),true); bFinite=false; break; }
		if (!bFinite) continue;
		if (!ClosedPathDecomposition::Solve(Positions, Options, Loops, Error)) { Fail(Error); continue; }
		for (const auto& Loop : Loops)
		{
			auto* Output = FPCGContext::NewObject_AnyThread<UPCGPointArrayData>(Context);
			Output->InitializeFromData(Source, nullptr, Settings->bInheritMetadata, Settings->bInheritMetadata);
			Output->SetPointsFrom(Source, MakeArrayView(Loop));
			auto Keys = Output->GetMetadataEntryValueRange();
			if (!Settings->bInheritMetadata) for (int64& Key : Keys) Key = PCGInvalidEntryKey;
			// The closure contract remains present even when inheritance is explicitly disabled.
			auto* OutData = Output->Metadata->GetMetadataDomain(PCGMetadataDomainID::Data);
			OutData->DeleteAttribute(Settings->ClosedAttributeName);
			auto* Closed = OutData->CreateAttribute<bool>(Settings->ClosedAttributeName, true, false);
			if (!Closed) { Fail(TEXT("Unable to write the required closure attribute.")); break; }
			if (bWriteCut)
			{
				auto* Elements = Output->Metadata->GetMetadataDomain(PCGMetadataDomainID::Elements);
				Elements->DeleteAttribute(Settings->IsCutAttributeName);
				auto* Cut = Elements->CreateAttribute<bool>(Settings->IsCutAttributeName, false, false);
				if (!Cut) { Fail(TEXT("Unable to write the requested cut attribute.")); break; }
				for (int32 I = 0; I < Loop.Num(); ++I)
				{
					// Allocate a child entry per occurrence: the same source vertex may start
					// a cut in one patch and an exterior edge in another.
					Keys[I] = Elements->AddEntry(Keys[I]);
					const int32 A = Loop[I], B = Loop[(I + 1) % Loop.Num()];
					const bool bBoundary = (A + 1) % Positions.Num() == B || (B + 1) % Positions.Num() == A;
					Cut->SetValue(Keys[I], !bBoundary);
				}
			}
			auto& Tagged = Context->OutputData.TaggedData.Emplace_GetRef(Input);
			Tagged.Data = Output;
			Tagged.Pin = PCGPinConstants::DefaultOutputLabel;
		}
	}
	return true;
}
#undef LOCTEXT_NAMESPACE
