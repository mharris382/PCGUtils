// Copyright Max Harris
//
// Baking many meshes in one graph, without a loop: DynMesh | From Static Mesh makes one DynMesh per listed asset,
// Get Asset Save Parameters (Input Data) names each on its @Data, DynMesh | Save Static Mesh saves them all.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EDITOR

#include "Data/PCGDynamicMeshData.h"
#include "Data/PCGPointData.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Elements/AssetManagement/PCGGetAssetSaveParameters.h"
#include "Elements/Conversion/PCGDynMeshSaveStaticMesh.h"
#include "Elements/Creation/PCGDynMeshFromStaticMesh.h"
#include "Engine/StaticMesh.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "Helpers/PCGUtilsDataDomainHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataDomain.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPin.h"
#include "UDynamicMesh.h"
#include "UObject/Package.h"

namespace PCGUtilsDynMeshSaveStaticMeshTests
{
	const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* TestFolder = TEXT("/Game/__PCGUtilsTests/SaveStaticMesh");

	TArray<FPCGTaggedData> Run(UPCGSettings* Settings, TArray<FPCGTaggedData> Inputs)
	{
		FPCGDataCollection Input;
		Input.TaggedData = MoveTemp(Inputs);
		Input.TaggedData.Emplace_GetRef().Data = Settings;
		FPCGElementPtr Element = Settings->GetElement();
		TUniquePtr<FPCGContext> Context(Element->Initialize(FPCGInitializeElementParams(&Input, nullptr, nullptr)));
		Context->AsyncState.bIsRunningOnMainThread = true;
		Context->AsyncState.NumAvailableTasks = 1;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			if (Element->Execute(Context.Get()))
			{
				return Context->OutputData.TaggedData;
			}
		}
		return {};
	}

	FPCGTaggedData OnIn(const UPCGData* Data, const TCHAR* Tag = nullptr)
	{
		FPCGTaggedData Tagged;
		Tagged.Data = Data;
		Tagged.Pin = PCGPinConstants::DefaultInputLabel;
		if (Tag)
		{
			Tagged.Tags.Add(Tag);
		}
		return Tagged;
	}

	/** An asset list as an attribute set: Mesh and Label on each entry, Batch on @Data. */
	UPCGParamData* AssetList(TArrayView<const TCHAR* const> Meshes, int32 Batch)
	{
		UPCGParamData* List = NewObject<UPCGParamData>();
		FPCGMetadataAttribute<FSoftObjectPath>* Mesh = List->Metadata->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(), false, false);
		FPCGMetadataAttribute<FString>* Label = List->Metadata->CreateAttribute<FString>(TEXT("Label"), FString(), false, false);
		for (int32 Index = 0; Index < Meshes.Num(); ++Index)
		{
			const PCGMetadataEntryKey Key = List->Metadata->AddEntry();
			Mesh->SetValue(Key, FSoftObjectPath(Meshes[Index]));
			Label->SetValue(Key, FString::Printf(TEXT("Entry%d"), Index));
		}
		PCGUtilsDataDomain::WriteValue<int32>(*PCGUtilsDataDomain::GetMutableDataDomain(List), TEXT("Batch"), Batch);
		return List;
	}

	template <typename T>
	bool ReadData(const UPCGData* Data, FName Name, T& OutValue)
	{
		const FPCGMetadataDomain* Domain = Data && Data->ConstMetadata() ? Data->ConstMetadata()->GetConstMetadataDomain(PCGMetadataDomainID::Data) : nullptr;
		const FPCGMetadataAttribute<T>* Attribute = Domain ? Domain->GetConstTypedAttribute<T>(Name) : nullptr;
		if (!Attribute)
		{
			return false;
		}
		OutValue = Attribute->GetValueFromItemKey(PCGFirstEntryKey);
		return true;
	}

	int32 TriangleCount(const UPCGData* Data)
	{
		const UPCGDynamicMeshData* MeshData = Cast<const UPCGDynamicMeshData>(Data);
		return MeshData && MeshData->GetDynamicMesh() ? MeshData->GetDynamicMesh()->GetMeshRef().TriangleCount() : -1;
	}

	int32 SavedTriangleCount(const FString& ObjectPath)
	{
		UStaticMesh* Saved = FindObject<UStaticMesh>(nullptr, *ObjectPath);
		if (!Saved)
		{
			return -1;
		}
		UDynamicMesh* ReadBack = NewObject<UDynamicMesh>();
		EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
		FGeometryScriptMeshReadLOD SourceLOD;
		SourceLOD.LODType = EGeometryScriptLODType::SourceModel;
		UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(Saved, ReadBack, FGeometryScriptCopyMeshFromAssetOptions(), SourceLOD, Outcome);
		return Outcome == EGeometryScriptOutcomePins::Success ? ReadBack->GetMeshRef().TriangleCount() : -1;
	}

	/** Takes the test's assets out of the content folder again; nothing was written to disk. */
	void DiscardSaved(TArrayView<const FString> ObjectPaths)
	{
		for (const FString& ObjectPath : ObjectPaths)
		{
			if (UStaticMesh* Saved = FindObject<UStaticMesh>(nullptr, *ObjectPath))
			{
				Saved->ClearFlags(RF_Public | RF_Standalone);
				Saved->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				Saved->MarkAsGarbage();
			}
		}
	}

	FString ObjectPath(const TCHAR* Name)
	{
		return FString::Printf(TEXT("%s/%s.%s"), TestFolder, Name, Name);
	}

	UPCGDynMeshSaveStaticMeshSettings* SaveSettings()
	{
		UPCGDynMeshSaveStaticMeshSettings* Settings = NewObject<UPCGDynMeshSaveStaticMeshSettings>();
		Settings->bSaveOnExportEnded = false;
		Settings->AssetPath.Constant = TestFolder;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshFromStaticMeshTest,
	"PCGUtils.DynMesh.FromStaticMesh.OneMeshPerListedAssetKeepingItsAttributes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshFromStaticMeshTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSaveStaticMeshTests;
	UPCGDynMeshFromStaticMeshSettings* Settings = NewObject<UPCGDynMeshFromStaticMeshSettings>();
	Settings->MeshAttribute.Update(TEXT("Mesh"));
	Settings->bSynchronousLoad = true;

	const TCHAR* const TwoMeshes[] = { Cube, Sphere };
	const TArray<FPCGTaggedData> Outputs = Run(Settings, { OnIn(AssetList(TwoMeshes, 7), TEXT("Rocks")) });
	UTEST_EQUAL(TEXT("Two listed meshes give two DynMeshes"), Outputs.Num(), 2);
	const int32 CubeTriangles = TriangleCount(Outputs[0].Data);
	TestTrue(TEXT("The first is a mesh"), CubeTriangles > 0);
	TestTrue(TEXT("The second is the sphere, a different mesh"), TriangleCount(Outputs[1].Data) > CubeTriangles);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FString Which = FString::Printf(TEXT("Mesh %d"), Index);
		TestTrue(*(Which + TEXT(" keeps its input's tag")), Outputs[Index].Tags.Contains(TEXT("Rocks")));
		int32 Batch = 0;
		TestTrue(*(Which + TEXT(" keeps its input's @Data")), ReadData<int32>(Outputs[Index].Data, TEXT("Batch"), Batch) && Batch == 7);
		FString Label;
		TestTrue(*(Which + TEXT(" takes its entry's Label as @Data")), ReadData<FString>(Outputs[Index].Data, TEXT("Label"), Label)
			&& Label == FString::Printf(TEXT("Entry%d"), Index));
		FSoftObjectPath Mesh;
		TestTrue(*(Which + TEXT(" takes its entry's Mesh as @Data")), ReadData<FSoftObjectPath>(Outputs[Index].Data, TEXT("Mesh"), Mesh)
			&& Mesh == FSoftObjectPath(TwoMeshes[Index]));
	}

	// A mesh named once on @Data gives one DynMesh, and with Copy Element Attributes off only @Data comes along.
	UPCGPointData* OneOnData = NewObject<UPCGPointData>();
	OneOnData->GetMutablePoints().AddDefaulted(3);
	PCGUtilsDataDomain::WriteValue<FString>(*PCGUtilsDataDomain::GetMutableDataDomain(OneOnData), TEXT("Source"), Cube);
	Settings->MeshAttribute.Update(TEXT("@Data.Source"));
	const TArray<FPCGTaggedData> FromData = Run(Settings, { OnIn(OneOnData) });
	UTEST_EQUAL(TEXT("A mesh named on @Data gives one DynMesh, however many points there are"), FromData.Num(), 1);
	TestEqual(TEXT("It is the cube, named by a string"), TriangleCount(FromData[0].Data), CubeTriangles);

	Settings->MeshAttribute.Update(TEXT("Mesh"));
	Settings->bCopyElementAttributes = false;
	const TCHAR* const OneMesh[] = { Cube };
	const TArray<FPCGTaggedData> NoElements = Run(Settings, { OnIn(AssetList(OneMesh, 1)) });
	UTEST_EQUAL(TEXT("One listed mesh gives one DynMesh"), NoElements.Num(), 1);
	FString Label;
	TestFalse(TEXT("Copy Element Attributes off leaves the entry's attributes behind"), ReadData<FString>(NoElements[0].Data, TEXT("Label"), Label));
	Settings->bCopyElementAttributes = true;

	// Each failure names its input and element.
	AddExpectedError(TEXT("input 0: Mesh Attribute 'Missing' was not found"), EAutomationExpectedErrorFlags::Contains, 1);
	Settings->MeshAttribute.Update(TEXT("Missing"));
	TestEqual(TEXT("A missing Mesh Attribute gives nothing"), Run(Settings, { OnIn(AssetList(OneMesh, 1)) }).Num(), 0);
	Settings->MeshAttribute.Update(TEXT("Mesh"));

	const TCHAR* const NotAMesh[] = { TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial") };
	AddExpectedError(TEXT("input 0, element 0: '/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial' is a"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("An asset that is not a mesh gives nothing"), Run(Settings, { OnIn(AssetList(NotAMesh, 1)) }).Num(), 0);

	const TCHAR* const OneEmpty[] = { Cube, TEXT("") };
	AddExpectedMessage(TEXT("input 0, element 1: Mesh Attribute 'Mesh' is empty"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("An empty entry is skipped, the others are made"), Run(Settings, { OnIn(AssetList(OneEmpty, 1)) }).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGGetAssetSaveParametersInputDataTest,
	"PCGUtils.AssetManagement.GetAssetSaveParameters.InputDataNamesEachInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGGetAssetSaveParametersInputDataTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSaveStaticMeshTests;
	UPCGGetAssetSaveParametersSettings* Settings = NewObject<UPCGGetAssetSaveParametersSettings>();
	Settings->Source = EPCGUtilsAssetSaveParameterSource::InputData;
	Settings->InputAssetAttribute.Update(TEXT("@Data.Mesh"));
	Settings->Suffix = TEXT("Baked");

	UPCGDynamicMeshData* CubeMesh = NewObject<UPCGDynamicMeshData>();
	CubeMesh->Initialize(UE::Geometry::FDynamicMesh3());
	PCGUtilsDataDomain::WriteValue<FSoftObjectPath>(*PCGUtilsDataDomain::GetMutableDataDomain(CubeMesh), TEXT("Mesh"), FSoftObjectPath(Cube));
	PCGUtilsDataDomain::WriteValue<int32>(*PCGUtilsDataDomain::GetMutableDataDomain(CubeMesh), TEXT("Keep"), 3);
	// Data made from other data inherits its @Data through a parent; that must work too.
	UPCGData* Inherited = CubeMesh->DuplicateData(nullptr, /*bInitializeMetadata=*/true);

	const TArray<FPCGTaggedData> Outputs = Run(Settings, { OnIn(CubeMesh, TEXT("A")), OnIn(Inherited, TEXT("B")) });
	UTEST_EQUAL(TEXT("One output per input"), Outputs.Num(), 2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FString Which = FString::Printf(TEXT("Output %d"), Index);
		UTEST_NOT_NULL(*(Which + TEXT(" is still a DynMesh")), Cast<UPCGDynamicMeshData>(Outputs[Index].Data));
		TestTrue(*(Which + TEXT(" is a copy, not the input")), Outputs[Index].Data != CubeMesh && Outputs[Index].Data != Inherited);
		FString Name, Path;
		TestTrue(*(Which + TEXT(" has @Data AssetName Cube_Baked")), ReadData<FString>(Outputs[Index].Data, TEXT("AssetName"), Name) && Name == TEXT("Cube_Baked"));
		TestTrue(*(Which + TEXT(" has @Data AssetPath /Engine/BasicShapes")), ReadData<FString>(Outputs[Index].Data, TEXT("AssetPath"), Path) && Path == TEXT("/Engine/BasicShapes"));
		int32 Keep = 0;
		TestTrue(*(Which + TEXT(" keeps its other @Data")), ReadData<int32>(Outputs[Index].Data, TEXT("Keep"), Keep) && Keep == 3);
	}
	TestTrue(TEXT("Tags carry over"), Outputs[0].Tags.Contains(TEXT("A")) && Outputs[1].Tags.Contains(TEXT("B")));

	Settings->AssetNameAttributeName = TEXT("SaveName");
	const TArray<FPCGTaggedData> Renamed = Run(Settings, { OnIn(CubeMesh) });
	FString Name;
	TestTrue(TEXT("Asset Name Attribute Name renames the attribute"), Renamed.Num() == 1 && ReadData<FString>(Renamed[0].Data, TEXT("SaveName"), Name));
	Settings->AssetNameAttributeName = TEXT("AssetName");

	// Two points each naming a mesh are two values, and one input must give one.
	UPCGPointData* TwoPoints = NewObject<UPCGPointData>();
	TwoPoints->GetMutablePoints().AddDefaulted(2);
	TwoPoints->Metadata->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(Cube), false, false);
	Settings->InputAssetAttribute.Update(TEXT("Mesh"));
	AddExpectedError(TEXT("input 0: Asset Attribute 'Mesh' holds 2 values"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("An input with more than one value is not output"), Run(Settings, { OnIn(TwoPoints) }).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSaveStaticMeshTest,
	"PCGUtils.DynMesh.SaveStaticMesh.SavesEveryInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshSaveStaticMeshTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSaveStaticMeshTests;
	UPCGDynMeshFromStaticMeshSettings* FromSettings = NewObject<UPCGDynMeshFromStaticMeshSettings>();
	FromSettings->MeshAttribute.Update(TEXT("Mesh"));
	FromSettings->bSynchronousLoad = true;
	const TCHAR* const TwoMeshes[] = { Cube, Sphere };
	const TArray<FPCGTaggedData> Meshes = Run(FromSettings, { OnIn(AssetList(TwoMeshes, 1), TEXT("Rocks")) });
	UTEST_EQUAL(TEXT("Two meshes to save"), Meshes.Num(), 2);
	const int32 CubeTriangles = TriangleCount(Meshes[0].Data);
	const int32 SphereTriangles = TriangleCount(Meshes[1].Data);

	TArray<FString> Saved;
	ON_SCOPE_EXIT { DiscardSaved(Saved); };

	// A constant name given two meshes numbers them.
	UPCGDynMeshSaveStaticMeshSettings* Settings = SaveSettings();
	Settings->AssetName.Constant = TEXT("Numbered");
	const TArray<FPCGTaggedData> Numbered = Run(Settings, { OnIn(Meshes[0].Data, TEXT("Rocks")), OnIn(Meshes[1].Data) });
	Saved.Append({ ObjectPath(TEXT("Numbered_0")), ObjectPath(TEXT("Numbered_1")) });
	UTEST_EQUAL(TEXT("One output per saved mesh"), Numbered.Num(), 2);
	TestEqual(TEXT("Numbered_0 holds the cube"), SavedTriangleCount(ObjectPath(TEXT("Numbered_0"))), CubeTriangles);
	TestEqual(TEXT("Numbered_1 holds the sphere"), SavedTriangleCount(ObjectPath(TEXT("Numbered_1"))), SphereTriangles);
	const UPCGParamData* FirstOut = Cast<const UPCGParamData>(Numbered[0].Data);
	UTEST_NOT_NULL(TEXT("The output is an attribute set"), FirstOut);
	const FPCGMetadataAttribute<FSoftObjectPath>* OutPath = FirstOut->ConstMetadata()->GetConstTypedAttribute<FSoftObjectPath>(TEXT("AssetPath"));
	TestTrue(TEXT("It names the asset saved"), OutPath && OutPath->GetValueFromItemKey(PCGFirstEntryKey) == FSoftObjectPath(ObjectPath(TEXT("Numbered_0"))));
	TestTrue(TEXT("It keeps the mesh's tag"), Numbered[0].Tags.Contains(TEXT("Rocks")));
	TestEqual(TEXT("It is on the OutAssetPath pin"), Numbered[0].Pin, PCGDynMeshSaveStaticMeshConstants::OutputLabel);

	// One mesh with a constant name is saved under that name, as the engine node does.
	const TArray<FPCGTaggedData> Single = Run(Settings, { OnIn(Meshes[0].Data) });
	Saved.Add(ObjectPath(TEXT("Numbered")));
	TestEqual(TEXT("A single mesh is not numbered"), SavedTriangleCount(ObjectPath(TEXT("Numbered"))), CubeTriangles);

	// The whole chain: names from Get Asset Save Parameters on @Data, the folder a constant.
	UPCGGetAssetSaveParametersSettings* Naming = NewObject<UPCGGetAssetSaveParametersSettings>();
	Naming->Source = EPCGUtilsAssetSaveParameterSource::InputData;
	Naming->InputAssetAttribute.Update(TEXT("@Data.Mesh"));
	Naming->Suffix = TEXT("Baked");
	const TArray<FPCGTaggedData> Named = Run(Naming, { OnIn(Meshes[0].Data), OnIn(Meshes[1].Data) });
	UTEST_EQUAL(TEXT("Both meshes are named"), Named.Num(), 2);
	Settings->AssetName.Input = EPCGUtilsInputValueType::Attribute;
	const TArray<FPCGTaggedData> FromNames = Run(Settings, { OnIn(Named[0].Data), OnIn(Named[1].Data) });
	Saved.Append({ ObjectPath(TEXT("Cube_Baked")), ObjectPath(TEXT("Sphere_Baked")) });
	UTEST_EQUAL(TEXT("Both named meshes are saved"), FromNames.Num(), 2);
	TestEqual(TEXT("Cube_Baked holds the cube"), SavedTriangleCount(ObjectPath(TEXT("Cube_Baked"))), CubeTriangles);
	TestEqual(TEXT("Sphere_Baked holds the sphere"), SavedTriangleCount(ObjectPath(TEXT("Sphere_Baked"))), SphereTriangles);

	// Failures name the input and what is wrong with it.
	AddExpectedError(TEXT("input 1 would be saved as '/Game/__PCGUtilsTests/SaveStaticMesh/Cube_Baked', which input 0 is saved as"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Two meshes with one name: only the first is saved"), Run(Settings, { OnIn(Named[0].Data), OnIn(Named[0].Data) }).Num(), 1);

	AddExpectedError(TEXT("Asset Name (input 0)"), EAutomationExpectedErrorFlags::Contains, 1, /*bIsRegex=*/false);
	TestEqual(TEXT("A mesh without the @Data name is not saved"), Run(Settings, { OnIn(Meshes[0].Data) }).Num(), 0);

	Settings->AssetName.Input = EPCGUtilsInputValueType::Constant;
	Settings->AssetName.Constant = TEXT("   ");
	AddExpectedError(TEXT("input 0: Asset Name is empty"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("An empty name is not saved"), Run(Settings, { OnIn(Meshes[0].Data) }).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGDynMeshSaveStaticMeshLODsTest,
	"PCGUtils.DynMesh.SaveStaticMesh.LODsRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGDynMeshSaveStaticMeshLODsTest::RunTest(const FString&)
{
	using namespace PCGUtilsDynMeshSaveStaticMeshTests;
	UPCGDynMeshFromStaticMeshSettings* FromSettings = NewObject<UPCGDynMeshFromStaticMeshSettings>();
	FromSettings->MeshAttribute.Update(TEXT("Mesh"));
	FromSettings->bSynchronousLoad = true;
	const TCHAR* const TwoMeshes[] = { Cube, Sphere };
	const TArray<FPCGTaggedData> Meshes = Run(FromSettings, { OnIn(AssetList(TwoMeshes, 1)) });
	UTEST_EQUAL(TEXT("Two meshes to save"), Meshes.Num(), 2);
	const int32 CubeTriangles = TriangleCount(Meshes[0].Data);
	const int32 SphereTriangles = TriangleCount(Meshes[1].Data);

	// One LOD read writes LOD 0 and the source mesh, ready for grouping downstream.
	int32 ReadLOD = -1;
	FSoftObjectPath ReadSource;
	TestTrue(TEXT("A single read writes @Data.LOD 0"), ReadData<int32>(Meshes[0].Data, TEXT("LOD"), ReadLOD) && ReadLOD == 0);
	TestTrue(TEXT("A single read writes @Data.SourceMesh"), ReadData<FSoftObjectPath>(Meshes[0].Data, TEXT("SourceMesh"), ReadSource)
		&& ReadSource == FSoftObjectPath(Cube));

	auto AsLOD = [](const UPCGData* Data, int32 LOD)
	{
		UPCGData* Copy = Data->DuplicateData(nullptr, /*bInitializeMetadata=*/true);
		PCGUtilsDataDomain::WriteValue<int32>(*PCGUtilsDataDomain::GetMutableDataDomain(Copy), TEXT("LOD"), LOD);
		return Copy;
	};

	TArray<FString> Saved;
	ON_SCOPE_EXIT { DiscardSaved(Saved); };

	// Meshes sharing a name become its LODs, in LOD order whatever the input order.
	UPCGDynMeshSaveStaticMeshSettings* Settings = SaveSettings();
	Settings->AssetName.Constant = TEXT("Chain");
	Settings->bSaveLODsFromAttribute = true;
	const TArray<FPCGTaggedData> Outputs = Run(Settings, { OnIn(AsLOD(Meshes[0].Data, 1), TEXT("Low")), OnIn(AsLOD(Meshes[1].Data, 0), TEXT("High")) });
	Saved.Add(ObjectPath(TEXT("Chain")));
	UStaticMesh* Chain = FindObject<UStaticMesh>(nullptr, *ObjectPath(TEXT("Chain")));
	UTEST_NOT_NULL(TEXT("One asset is saved, not numbered"), Chain);
	TestEqual(TEXT("It has two LODs"), Chain->GetNumSourceModels(), 2);
	UTEST_EQUAL(TEXT("One output per mesh"), Outputs.Num(), 2);
	TestTrue(TEXT("Outputs stay in input order"), Outputs[0].Tags.Contains(TEXT("Low")) && Outputs[1].Tags.Contains(TEXT("High")));

	// All LODs reads the chain back: one DynMesh per LOD, each marked.
	FromSettings->bReadAllLODs = true;
	UPCGParamData* ChainList = NewObject<UPCGParamData>();
	ChainList->Metadata->CreateAttribute<FSoftObjectPath>(TEXT("Mesh"), FSoftObjectPath(Chain), false, false);
	ChainList->Metadata->AddEntry();
	const TArray<FPCGTaggedData> ReadBack = Run(FromSettings, { OnIn(ChainList) });
	UTEST_EQUAL(TEXT("All LODs gives one DynMesh per LOD"), ReadBack.Num(), 2);
	for (int32 LOD = 0; LOD < 2; ++LOD)
	{
		const FString Which = FString::Printf(TEXT("LOD %d"), LOD);
		TestTrue(*(Which + TEXT(" is marked with its LOD")), ReadData<int32>(ReadBack[LOD].Data, TEXT("LOD"), ReadLOD) && ReadLOD == LOD);
		TestTrue(*(Which + TEXT(" names its source mesh")), ReadData<FSoftObjectPath>(ReadBack[LOD].Data, TEXT("SourceMesh"), ReadSource)
			&& ReadSource == FSoftObjectPath(Chain));
	}
	TestEqual(TEXT("LOD 0 is the sphere"), TriangleCount(ReadBack[0].Data), SphereTriangles);
	TestEqual(TEXT("LOD 1 is the cube"), TriangleCount(ReadBack[1].Data), CubeTriangles);

	// Saving fewer LODs over the asset removes the old extra ones.
	Run(Settings, { OnIn(AsLOD(Meshes[1].Data, 0)) });
	TestEqual(TEXT("Resaving one LOD leaves one LOD"), Chain->GetNumSourceModels(), 1);

	AddExpectedError(TEXT("'/Game/__PCGUtilsTests/SaveStaticMesh/Chain' has no LOD 1 (the next LOD given is 2, from input 1)"),
		EAutomationExpectedErrorFlags::Contains, 1, /*bIsRegex=*/false);
	TestEqual(TEXT("A gap in the LODs saves nothing"), Run(Settings, { OnIn(AsLOD(Meshes[1].Data, 0)), OnIn(AsLOD(Meshes[0].Data, 2)) }).Num(), 0);

	AddExpectedError(TEXT("inputs 0 and 1 are both LOD 0 of"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Two meshes for one LOD save nothing"), Run(Settings, { OnIn(AsLOD(Meshes[1].Data, 0)), OnIn(AsLOD(Meshes[0].Data, 0)) }).Num(), 0);

	AddExpectedError(TEXT("input 0 has no @Data attribute 'LOD' (LOD Attribute)"), EAutomationExpectedErrorFlags::Contains, 1, /*bIsRegex=*/false);
	UPCGDynamicMeshData* NoLOD = NewObject<UPCGDynamicMeshData>();
	NoLOD->Initialize(UE::Geometry::FDynamicMesh3(Cast<const UPCGDynamicMeshData>(Meshes[1].Data)->GetDynamicMesh()->GetMeshRef()));
	TestEqual(TEXT("A mesh without a LOD is not saved"), Run(Settings, { OnIn(NoLOD) }).Num(), 0);
	return true;
}

#endif
