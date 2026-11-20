// Copyright Max Harris

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Data/PCGPointData.h"
#include "Elements/AssetManagement/PCGGetActorBakeSaveParameters.h"
#include "Elements/PCGTransformPoints.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "PCGActorBase.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGParamData.h"
#include "PCGPin.h"

namespace PCGActorBakeSaveParametersTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	struct FExecutor : FPCGGetActorBakeSaveParametersElement { using FPCGGetActorBakeSaveParametersElement::ExecuteInternal; };

	APCGActorBase* Actor(const FString& Name = TEXT("Cliff"))
	{
		APCGActorBase* Result = NewObject<APCGActorBase>();
		Result->BakeSettings.BakedAssetSaveName = Name;
		Result->BakeSettings.BakedAssetSavePath.Path = TEXT("/Game/Baked/");
		Result->BakeSettings.BakedAssetGroupLabel = TEXT("DoNotDuplicateGroup");
		return Result;
	}

	struct FFixture
	{
		UPCGGraph* Graph = NewObject<UPCGGraph>();
		UPCGGetActorBakeSaveParametersSettings* Settings = nullptr;
		UPCGNode* Node = Graph->AddNodeOfType(Settings);

		TArray<FPCGTaggedData> Run(APCGActorBase* Self, const TArray<FPCGTaggedData>& Inputs = {})
		{
			Node->UpdateAfterSettingsChangeDuringCreation();
			FPCGContext Context;
			Context.Node = Node;
			Context.InputData.TaggedData = Inputs;
			UPCGComponent* Component = Self ? NewObject<UPCGComponent>(Self) : nullptr;
			Context.ExecutionSource = Component;
			FExecutor().ExecuteInternal(&Context);
			return Context.OutputData.TaggedData;
		}
	};

	FPCGTaggedData Input(UPCGData* Data, const FString& Tag)
	{
		FPCGTaggedData Result;
		Result.Data = Data;
		Result.Pin = PCGPinConstants::DefaultInputLabel;
		Result.Tags.Add(Tag);
		return Result;
	}

	template<typename T>
	T Value(const UPCGData* Data, FName Name, FPCGMetadataDomainID Domain = PCGMetadataDomainID::Elements)
	{
		const FPCGMetadataAttribute<T>* Attribute = Data && Data->ConstMetadata()
			? Data->ConstMetadata()->GetConstTypedAttribute<T>(FPCGAttributeIdentifier(Name, Domain)) : nullptr;
		return Attribute ? Attribute->GetValueFromItemKey(0) : T{};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorBakeConstantTest, "PCGUtils.BakeParameters.ConstantAndActor", PCGActorBakeSaveParametersTests::Flags)
bool FActorBakeConstantTest::RunTest(const FString&)
{
	using namespace PCGActorBakeSaveParametersTests;
	FFixture F;
	APCGActorBase* Self = Actor();
	TestTrue(TEXT("Self is the default source"), F.Settings->bUseSelf);
	TestFalse(TEXT("No input is the default"), F.Settings->bUseInputData);
	TestTrue(TEXT("No data input in constant mode"), F.Settings->InputPinProperties().IsEmpty());
	F.Settings->AssetLabel = TEXT("_Barnacles");
	TArray<FPCGTaggedData> Outputs = F.Run(Self);
	if (!TestEqual(TEXT("One Attribute Set"), Outputs.Num(), 1)) return false;
	TestNotNull(TEXT("Param data output"), Cast<const UPCGParamData>(Outputs[0].Data));
	TestEqual(TEXT("Label appended once; group prefix not duplicated"), Value<FString>(Outputs[0].Data, TEXT("AssetName")), FString(TEXT("Cliff_Barnacles")));
	TestEqual(TEXT("Folder is normalized"), Value<FString>(Outputs[0].Data, TEXT("AssetPath")), FString(TEXT("/Game/Baked")));
	TestEqual(TEXT("Soft reference includes object name"), Value<FSoftObjectPath>(Outputs[0].Data, TEXT("Asset")), FSoftObjectPath(TEXT("/Game/Baked/Cliff_Barnacles.Cliff_Barnacles")));
	const FPCGMetadataDomain* DataDomain = Outputs[0].Data->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data);
	TestTrue(TEXT("Param outputs are regular attributes, not @Data"), !DataDomain || !DataDomain->GetConstAttribute(TEXT("AssetName")));
	TestEqual(TEXT("Exactly one param row"), Outputs[0].Data->ConstMetadata()->GetItemCountForChild(), int64(1));
	F.Settings->bUseSelf = false;
	F.Settings->SourceActor = Actor(TEXT("Explicit"));
	F.Settings->AssetLabel.Empty();
	F.Settings->OutputAssetNameAttribute = TEXT("SaveName");
	F.Settings->OutputAssetPathAttribute = TEXT("Folder");
	F.Settings->OutputAssetReferenceAttribute = TEXT("Target");
	Outputs = F.Run(Self);
	if (!TestEqual(TEXT("Explicit actor still outputs one dataset"), Outputs.Num(), 1)) return false;
	TestEqual(TEXT("Explicit source and renamed output"), Value<FString>(Outputs[0].Data, TEXT("SaveName")), FString(TEXT("Explicit")));
	TestEqual(TEXT("Renamed folder"), Value<FString>(Outputs[0].Data, TEXT("Folder")), FString(TEXT("/Game/Baked")));
	TestEqual(TEXT("Renamed reference"), Value<FSoftObjectPath>(Outputs[0].Data, TEXT("Target")), FSoftObjectPath(TEXT("/Game/Baked/Explicit.Explicit")));
	CastChecked<APCGActorBase>(F.Settings->SourceActor.Get())->BakeSettings.BakedAssetSaveName = TEXT("Updated");
	Outputs = F.Run(Self);
	TestEqual(TEXT("A new generation reads actor changes"), Value<FString>(Outputs[0].Data, TEXT("SaveName")), FString(TEXT("Updated")));
	TestFalse(TEXT("Actor state cannot be hidden by input-only caching"), FExecutor().IsCacheable(F.Settings));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorBakeStreamParamsTest, "PCGUtils.BakeParameters.StreamAttributeSets", PCGActorBakeSaveParametersTests::Flags)
bool FActorBakeStreamParamsTest::RunTest(const FString&)
{
	using namespace PCGActorBakeSaveParametersTests;
	FFixture F;
	F.Settings->bUseInputData = true;
	F.Settings->AssetLabelAttributeName = TEXT("BakeLabel");
	F.Settings->AssetLabel = TEXT("_IgnoredInStreamMode");
	UPCGPointData* First = NewObject<UPCGPointData>();
	First->GetMutablePoints().SetNum(3);
	UPCGPointData* Second = NewObject<UPCGPointData>();
	UPCGParamData* Third = NewObject<UPCGParamData>();
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(First), TEXT("BakeLabel"), FString(TEXT("_A")));
	// A regular point/param attribute must not be mistaken for @Data.
	Second->Metadata->CreateAttribute<FString>(TEXT("BakeLabel"), TEXT("_WrongDomain"), false, false);
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Third), TEXT("BakeLabel"), FName(TEXT("_C")));
	TArray<FPCGTaggedData> Outputs = F.Run(Actor(), {Input(First, TEXT("First")), Input(Second, TEXT("Second")), Input(Third, TEXT("Third"))});
	if (!TestEqual(TEXT("One output per dataset, not per point or label"), Outputs.Num(), 3)) return false;
	const FString Names[] = {TEXT("Cliff_A"), TEXT("Cliff"), TEXT("Cliff_C")};
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		TestNotNull(TEXT("Each result is an Attribute Set"), Cast<const UPCGParamData>(Outputs[Index].Data));
		TestEqual(TEXT("Correct dataset suffix; missing is valid"), Value<FString>(Outputs[Index].Data, TEXT("AssetName")), Names[Index]);
		TestEqual(TEXT("Matching reference for each dataset"), Value<FSoftObjectPath>(Outputs[Index].Data, TEXT("Asset")), FSoftObjectPath(TEXT("/Game/Baked/") + Names[Index] + TEXT(".") + Names[Index]));
		TestEqual(TEXT("Tags survive the conversion"), Outputs[Index].Tags.Num(), 1);
	}
	TestNull(TEXT("Input is never annotated in Attribute Set mode"), First->ConstMetadata()->GetConstAttribute(FPCGAttributeIdentifier(TEXT("Asset"), PCGMetadataDomainID::Data)));
	TestTrue(TEXT("Input mode with no datasets produces no output"), F.Run(Actor()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorBakeStreamDataTest, "PCGUtils.BakeParameters.StreamDataPreservation", PCGActorBakeSaveParametersTests::Flags)
bool FActorBakeStreamDataTest::RunTest(const FString&)
{
	using namespace PCGActorBakeSaveParametersTests;
	FFixture F;
	F.Settings->bUseInputData = F.Settings->bWriteToDataDomain = true;
	APCGActorBase* Self = Actor();
	UPCGPointData* Parent = NewObject<UPCGPointData>();
	Parent->TargetActor = Self;
	FPCGPoint& Point = Parent->GetMutablePoints().Emplace_GetRef();
	Point.Transform.SetLocation(FVector(12, 34, 56));
	Point.Density = 0.37f;
	Point.MetadataEntry = Parent->Metadata->AddEntry();
	Parent->Metadata->CreateAttribute<int32>(TEXT("PointAttribute"), 0, false, false)->SetValue(Point.MetadataEntry, 77);
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Parent), TEXT("KeepMe"), FString(TEXT("Inherited")));
	UPCGPointData* InputData = CastChecked<UPCGPointData>(Parent->DuplicateData(nullptr, true));
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(InputData), TEXT("Label"), FString(TEXT("_Paint")));
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(InputData), TEXT("AssetName"), int32(123));
	TArray<FPCGTaggedData> Outputs = F.Run(Self, {Input(InputData, TEXT("PreserveTag"))});
	if (!TestEqual(TEXT("One annotated output"), Outputs.Num(), 1)) return false;
	const UPCGPointData* Result = Cast<const UPCGPointData>(Outputs[0].Data);
	if (!TestNotNull(TEXT("Data class is preserved"), Result)) return false;
	TestTrue(TEXT("Upstream object is not mutated"), Result != InputData);
	TestTrue(TEXT("Target actor is preserved"), Result->TargetActor.Get() == Self);
	TestTrue(TEXT("Tags are preserved"), Outputs[0].Tags.Contains(TEXT("PreserveTag")));
	TestEqual(TEXT("Number of points preserved"), Result->GetPoints().Num(), 1);
	TestEqual(TEXT("Point transform preserved"), Result->GetPoints()[0].Transform.GetLocation(), FVector(12, 34, 56));
	TestEqual(TEXT("Density preserved"), Result->GetPoints()[0].Density, 0.37f);
	TestEqual(TEXT("Element metadata preserved"), Value<int32>(Result, TEXT("PointAttribute")), 77);
	TestEqual(TEXT("Inherited @Data preserved"), Value<FString>(Result, TEXT("KeepMe"), PCGMetadataDomainID::Data), FString(TEXT("Inherited")));
	TestEqual(TEXT("Name overwritten in selected domain and type"), Value<FString>(Result, TEXT("AssetName"), PCGMetadataDomainID::Data), FString(TEXT("Cliff_Paint")));
	TestEqual(TEXT("Input attribute type/value untouched"), Value<int32>(InputData, TEXT("AssetName"), PCGMetadataDomainID::Data), 123);
	TestEqual(TEXT("Reference written to @Data"), Value<FSoftObjectPath>(Result, TEXT("Asset"), PCGMetadataDomainID::Data), FSoftObjectPath(TEXT("/Game/Baked/Cliff_Paint.Cliff_Paint")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorBakePinsTest, "PCGUtils.BakeParameters.BidirectionalPins", PCGActorBakeSaveParametersTests::Flags)
bool FActorBakePinsTest::RunTest(const FString&)
{
	using namespace PCGActorBakeSaveParametersTests;
	FFixture F;
	F.Settings->bUseInputData = F.Settings->bWriteToDataDomain = true;
	F.Node->UpdateAfterSettingsChangeDuringCreation();
	UPCGPin* In = F.Node->GetInputPin(PCGPinConstants::DefaultInputLabel);
	UPCGPin* Out = F.Node->GetOutputPin(PCGPinConstants::DefaultOutputLabel);
	const FPCGDataTypeIdentifier Any{EPCGDataType::Any}, Points{EPCGDataType::Point}, Param{EPCGDataType::Param};
	TestTrue(TEXT("Disconnected input is generic"), In->GetCurrentTypesID() == Any);
	TestTrue(TEXT("Disconnected output is generic"), Out->GetCurrentTypesID() == Any);
	UPCGTransformPointsSettings* TransformSettings = nullptr;
	UPCGNode* Transform = F.Graph->AddNodeOfType(TransformSettings);
	UPCGPin* PointSource = Transform->GetOutputPin(PCGPinConstants::DefaultOutputLabel);
	UPCGPin* PointSink = Transform->GetInputPin(PCGPinConstants::DefaultInputLabel);
	In->AddEdgeTo(PointSource);
	TestTrue(TEXT("Upstream points specialize output"), Out->GetCurrentTypesID() == Points);
	TestTrue(TEXT("Upstream points specialize input"), In->GetCurrentTypesID() == Points);
	In->BreakAllEdges();
	Out->AddEdgeTo(PointSink);
	TestTrue(TEXT("Downstream points specialize input"), In->GetCurrentTypesID() == Points);
	TestTrue(TEXT("Downstream points specialize output"), Out->GetCurrentTypesID() == Points);
	Out->BreakAllEdges();
	TestTrue(TEXT("Disconnect resets input"), In->GetCurrentTypesID() == Any);
	TestTrue(TEXT("Disconnect resets output"), Out->GetCurrentTypesID() == Any);
	UPCGGetActorBakeSaveParametersSettings* OtherSettings = nullptr;
	UPCGNode* Other = F.Graph->AddNodeOfType(OtherSettings);
	OtherSettings->bUseInputData = OtherSettings->bWriteToDataDomain = true;
	Other->UpdateAfterSettingsChangeDuringCreation();
	Out->AddEdgeTo(Other->GetInputPin(PCGPinConstants::DefaultInputLabel));
	TestTrue(TEXT("Two unconstrained dynamic nodes terminate"), In->GetCurrentTypesID() == Any);
	Other->GetOutputPin(PCGPinConstants::DefaultOutputLabel)->AddEdgeTo(PointSink);
	TestTrue(TEXT("Downstream type propagates through two nodes"), In->GetCurrentTypesID() == Points);
	Out->BreakAllEdges();
	F.Settings->bWriteToDataDomain = false;
	F.Node->UpdateAfterSettingsChangeDuringCreation();
	TestFalse(TEXT("Attribute Set output does not use passthrough inference"), F.Settings->HasDynamicPins());
	TestTrue(TEXT("Stream-to-param input remains generic"), F.Node->GetInputPin(PCGPinConstants::DefaultInputLabel)->GetCurrentTypesID() == Any);
	TestTrue(TEXT("Stream-to-param output is Param"), F.Node->GetOutputPin(PCGPinConstants::DefaultOutputLabel)->GetCurrentTypesID() == Param);
	F.Settings->bUseInputData = false;
	F.Node->UpdateAfterSettingsChangeDuringCreation();
	TestNull(TEXT("No-input mode removes In"), F.Node->GetInputPin(PCGPinConstants::DefaultInputLabel));
	TestTrue(TEXT("No-input output is Param"), F.Node->GetOutputPin(PCGPinConstants::DefaultOutputLabel)->GetCurrentTypesID() == Param);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FActorBakeValidationTest, "PCGUtils.BakeParameters.ValidationPreservesInputs", PCGActorBakeSaveParametersTests::Flags)
bool FActorBakeValidationTest::RunTest(const FString&)
{
	using namespace PCGActorBakeSaveParametersTests;
	FFixture F;
	F.Settings->bUseInputData = F.Settings->bWriteToDataDomain = true;
	UPCGPointData* Data = NewObject<UPCGPointData>();
	F.Settings->OutputAssetReferenceAttribute = F.Settings->OutputAssetNameAttribute;
	AddExpectedError(TEXT("Output Asset Reference Attribute duplicates 'AssetName'"), EAutomationExpectedErrorFlags::Contains, 1);
	TArray<FPCGTaggedData> Outputs = F.Run(Actor(), {Input(Data, TEXT("Keep"))});
	TestEqual(TEXT("Invalid settings preserve the stream"), Outputs.Num(), 1);
	if (Outputs.Num()) TestTrue(TEXT("Original data preserved unchanged"), Outputs[0].Data == Data);
	F.Settings->OutputAssetReferenceAttribute = TEXT("Asset");
	AddExpectedError(TEXT("Self has no source actor"), EAutomationExpectedErrorFlags::Contains, 1);
	Outputs = F.Run(nullptr, {Input(Data, TEXT("Keep"))});
	TestEqual(TEXT("Missing actor preserves the stream"), Outputs.Num(), 1);
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Data), TEXT("Label"), 17);
	AddExpectedError(TEXT("expected String or Name"), EAutomationExpectedErrorFlags::Contains, 1);
	Outputs = F.Run(Actor(), {Input(Data, TEXT("Keep"))});
	if (!TestEqual(TEXT("Wrong optional label type does not drop input"), Outputs.Num(), 1)) return false;
	TestEqual(TEXT("Wrong label type uses base name"), Value<FString>(Outputs[0].Data, TEXT("AssetName"), PCGMetadataDomainID::Data), FString(TEXT("Cliff")));
	PCGUtilsDataDomain::WriteValue(*PCGUtilsDataDomain::GetMutableDataDomain(Data), TEXT("Label"), FString(TEXT("_Bad/Name")));
	AddExpectedError(TEXT("input 0 produces invalid asset reference"), EAutomationExpectedErrorFlags::Contains, 1);
	Outputs = F.Run(Actor(), {Input(Data, TEXT("Keep"))});
	if (!TestEqual(TEXT("Invalid target path does not drop the stream"), Outputs.Num(), 1)) return false;
	TestTrue(TEXT("Invalid path preserves original data"), Outputs[0].Data == Data);
	APCGActorBase* EmptyFolder = Actor();
	EmptyFolder->BakeSettings.BakedAssetSavePath.Path.Empty();
	AddExpectedError(TEXT("BakeSettings.BakedAssetSavePath is empty"), EAutomationExpectedErrorFlags::Contains, 1);
	Outputs = F.Run(EmptyFolder, {Input(Data, TEXT("Keep"))});
	TestEqual(TEXT("Empty folder preserves the stream"), Outputs.Num(), 1);
	return true;
}

#endif
