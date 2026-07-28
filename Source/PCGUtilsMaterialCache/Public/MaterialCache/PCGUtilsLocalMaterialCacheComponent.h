#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MaterialCache/PCGUtilsLocalMaterialTypes.h"
#include "MaterialCache/PCGUtilsMaterialVariantRequest.h"
#include "MaterialCache/PCGUtilsLocalMaterialCacheStatistics.h"
#include "PCGUtilsLocalMaterialCacheComponent.generated.h"

class UMaterialInstanceDynamic;

/**
 * Actor-local cache of mutable UMaterialInstanceDynamic objects, addressed by
 * (owner component + binding name + optional variant name).
 *
 * Complementary to (and conceptually separate from) the world-scoped, content-addressed,
 * immutable UPCGUtilsMaterialVariantCacheSubsystem: that cache exists to deduplicate
 * identical shared materials across many callers; this component exists to give one
 * specific actor's components stable, reusable, individually-mutable MIDs that gameplay
 * can retrieve and modify at runtime (e.g. a spline's "gunpowder trail" material whose
 * BurnProgress parameter is animated during play). Local MIDs are never added to the
 * global shared cache, and the global cache's results are never stored here.
 *
 * A local material is only ever created once per (owner, binding, variant) with a given
 * initialization request; re-resolving with the SAME request returns the SAME MID and
 * preserves any gameplay-driven parameter changes made since. Re-resolving with a
 * DIFFERENT initialization request replaces the MID (a new one is created and initialized;
 * the old one is not mutated to match, so stale gameplay state can never mix unpredictably
 * with a new base configuration).
 *
 * Owner components must belong to the same actor as this cache component - V1 does not
 * support addressing components on other actors. Not automatically added to actors:
 * actors that need local material ownership must explicitly contain this component.
 */
UCLASS(ClassGroup = "PCGUtils", meta = (BlueprintSpawnableComponent))
class PCGUTILSMATERIALCACHE_API UPCGUtilsLocalMaterialCacheComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Distinguishes this cache component from siblings on the same actor when more than
	 * one exists. Required whenever an actor has multiple local material cache components -
	 * lookups that don't disambiguate in that case fail with a clear diagnostic rather than
	 * silently picking one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Material Cache")
	FName CacheName = NAME_None;

	//~ Begin UActorComponent interface
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	//~ End UActorComponent interface

	// ── Resolution (create-or-reuse) ────────────────────────────────────────────

	/**
	 * Resolves the local material for (OwnerComponent, BindingName, VariantName).
	 *
	 * First resolution: creates a MID from InitialConfiguration's parent material, applies
	 * its overrides, stores it, and returns it. Later resolutions with an unchanged
	 * (canonicalized) InitialConfiguration return the same MID unmodified, preserving any
	 * gameplay-driven changes. A changed InitialConfiguration replaces the MID.
	 *
	 * OwnerComponent must be non-null and belong to this component's actor. Must be called
	 * from the game thread.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Resolve Local Material for Component"))
	UMaterialInstanceDynamic* ResolveLocalMaterial(
		UActorComponent* OwnerComponent,
		FName BindingName,
		FName VariantName,
		const FPCGUtilsMaterialVariantRequest& InitialConfiguration);

	/**
	 * Convenience overload of ResolveLocalMaterial that accepts a soft object path to the
	 * owner component instead of a direct reference - internally resolves the path,
	 * validates it, and derives the same owner key the direct-reference overload would.
	 * Callers never need to manually convert soft path -> component -> key first.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Resolve Local Material from Component Path"))
	UMaterialInstanceDynamic* ResolveLocalMaterialFromComponentPath(
		const FSoftObjectPath& OwnerComponentPath,
		FName BindingName,
		FName VariantName,
		const FPCGUtilsMaterialVariantRequest& InitialConfiguration);

	// ── Retrieval (no creation) ──────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Get Local Material for Component"))
	UMaterialInstanceDynamic* GetLocalMaterial(const UActorComponent* OwnerComponent, FName BindingName, FName VariantName = NAME_None) const;

	UFUNCTION(BlueprintPure, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Get Local Material from Component Path"))
	UMaterialInstanceDynamic* GetLocalMaterialFromComponentPath(const FSoftObjectPath& OwnerComponentPath, FName BindingName, FName VariantName = NAME_None) const;

	/** Name-based lookup for systems that already store the derived owner key - the common case should use the direct-component or soft-path overloads instead. */
	UFUNCTION(BlueprintPure, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Get Local Material by Owner Key"))
	UMaterialInstanceDynamic* GetLocalMaterialByOwnerKey(FName OwnerComponentKey, FName BindingName, FName VariantName = NAME_None) const;

	UFUNCTION(BlueprintPure, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Get All Local Materials for Component"))
	TArray<UMaterialInstanceDynamic*> GetLocalMaterialsForComponent(const UActorComponent* OwnerComponent) const;

	UFUNCTION(BlueprintPure, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Get All Local Materials for Component Path"))
	TArray<UMaterialInstanceDynamic*> GetLocalMaterialsForComponentPath(const FSoftObjectPath& OwnerComponentPath) const;

	UFUNCTION(BlueprintPure, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Get All Local Materials for Owner Key"))
	TArray<UMaterialInstanceDynamic*> GetLocalMaterialsForOwnerKey(FName OwnerComponentKey) const;

	UFUNCTION(BlueprintPure, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Get All Local Materials"))
	TArray<UMaterialInstanceDynamic*> GetAllLocalMaterials() const;

	// ── Removal ──────────────────────────────────────────────────────────────────

	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Remove Local Material for Component"))
	bool RemoveLocalMaterial(const UActorComponent* OwnerComponent, FName BindingName, FName VariantName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Remove Local Material from Component Path"))
	bool RemoveLocalMaterialFromComponentPath(const FSoftObjectPath& OwnerComponentPath, FName BindingName, FName VariantName = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Remove All Local Materials for Component"))
	int32 RemoveAllLocalMaterialsForComponent(const UActorComponent* OwnerComponent);

	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Remove All Local Materials for Component Path"))
	int32 RemoveAllLocalMaterialsForComponentPath(const FSoftObjectPath& OwnerComponentPath);

	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Remove All Local Materials for Owner Key"))
	int32 RemoveAllLocalMaterialsForOwnerKey(FName OwnerComponentKey);

	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Clear Local Materials for Component"))
	void ClearAllLocalMaterials();

	/**
	 * Removes entries whose weak OwnerComponent reference is no longer valid. The local
	 * cache is transient and does not need persistent cross-session identity, but stale
	 * entries for destroyed/reconstructed components should not accumulate indefinitely
	 * within one live actor instance. Called automatically before each resolution; also
	 * safe to call explicitly for maintenance.
	 */
	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local")
	int32 RemoveInvalidComponentEntries();

	// ── Diagnostics ──────────────────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Get Local Material Cache Statistics"))
	FPCGUtilsLocalMaterialCacheStatistics GetLocalMaterialCacheStatistics() const;

	UFUNCTION(BlueprintCallable, Category = "PCGUtils|Material Cache|Local", meta = (DisplayName = "Dump Local Material Cache"))
	void DumpLocalMaterialCache() const;

private:
	UMaterialInstanceDynamic* ResolveLocalMaterialInternal(
		UActorComponent* OwnerComponent,
		FName OwnerComponentKey,
		FName BindingName,
		FName VariantName,
		const FPCGUtilsMaterialVariantRequest& InitialConfiguration);

	bool ValidateOwnerComponent(const UActorComponent* OwnerComponent, FText& OutError) const;

	/** Reflected storage - strongly references every locally-owned MID so GC can see them. */
	UPROPERTY(Transient)
	TMap<FPCGUtilsLocalMaterialBindingKey, FPCGUtilsLocalMaterialEntry> LocalEntries;

	UPROPERTY(Transient)
	FPCGUtilsLocalMaterialCacheStatistics Statistics;
};
