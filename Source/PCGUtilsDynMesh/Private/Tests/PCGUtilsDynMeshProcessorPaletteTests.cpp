// Copyright Max Harris
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Elements/Attributes/PCGMaterial.h"
#include "Elements/Attributes/PCGSetVertexColor.h"
#include "Factories/PCGUtilsDynMeshFactoryData.h"
#include "Factories/PCGUtilsDynMeshFactoryProvider.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPCGUtilsCoreModuleContractTest,
	"PCGUtils.DynMesh.Core.ModuleContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPCGUtilsCoreModuleContractTest::RunTest(const FString&)
{
	TestEqual(TEXT("Shared data belongs to Core"),
		UPCGUtilsDynMeshFactoryData::StaticClass()->GetOutermost()->GetName(), FString(TEXT("/Script/PCGUtilsCore")));
	TestEqual(TEXT("Shared settings belong to Core"),
		UPCGUtilsDynMeshFactoryProviderSettings::StaticClass()->GetOutermost()->GetName(), FString(TEXT("/Script/PCGUtilsCore")));
	TestNotNull(TEXT("Priority remains shared factory state"),
		UPCGUtilsDynMeshFactoryData::StaticClass()->FindPropertyByName(TEXT("Priority")));
	const UPCGMaterialSettings* Material = GetDefault<UPCGMaterialSettings>();
	const UPCGSetVertexColorSettings* Color = GetDefault<UPCGSetVertexColorSettings>();
	TestEqual(TEXT("Material title is explicit"), Material->GetDefaultNodeTitle().ToString(), FString(TEXT("DynMesh|Set Material")));
	TestEqual(TEXT("Vertex color title is explicit"), Color->GetDefaultNodeTitle().ToString(), FString(TEXT("DynMesh|Set Vertex Colors")));
	for (const UPCGSettings* Settings : { static_cast<const UPCGSettings*>(Material), static_cast<const UPCGSettings*>(Color) })
	{
		TestEqual(TEXT("Processor stays in the mesh palette"), Settings->GetType(), EPCGSettingsType::DynamicMesh);
		TestTrue(TEXT("Selection search finds the processor"), Settings->GetClass()->GetMetaData(TEXT("Keywords")).Contains(TEXT("selection")));
	}
	return true;
}
#endif
