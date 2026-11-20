# Param | Get Actor Bake Save Parameters

Reads the source actor's `FPCGUtilsBakeSettings` property named `BakeSettings`. **Use Self** defaults on and uses
the original PCG component's owner, including partitioned generation. Disable it to select another loaded actor.
This node computes save parameters; it neither saves nor loads an asset.

| Use Input Data | Write To Data Domain | Result |
| --- | --- | --- |
| Off | Ignored | One single-row Attribute Set, using the optional constant Asset Label. No data input pin. |
| On | Off | One single-row Attribute Set per input dataset, retaining that dataset's tags. |
| On | On | Each original dataset, duplicated with its geometry, element metadata, other @Data attributes, tags and target actor preserved. Save outputs are added to @Data. |

All three outputs are always written; each name is editable:

| Setting | Default | Type / meaning |
| --- | --- | --- |
| Output Asset Name Attribute | `AssetName` | String: `BakedAssetSaveName` plus the optional label. |
| Output Asset Path Attribute | `AssetPath` | String: normalized folder such as `/Game/Baked`. |
| Output Asset Reference Attribute | `Asset` | FSoftObjectPath: `/Game/Baked/Name.Name`, including when the asset does not yet exist. |

Attribute Set outputs use regular row attributes. Stream outputs use the `@Data` domain. Existing attributes
with the configured output names are replaced on the duplicate, including when their prior type differs.

In input mode, **Asset Label Attribute Name** defaults to `Label`. Add a String attribute upstream with
**Add Attribute** targeting `@Data.Label`. A Name attribute is also supported. Only `@Data` is read: an element
attribute named `Label` is not a dataset label. Missing or empty labels are valid and leave the base name unchanged.
A wrong label type warns and uses the base name so the stream remains available.

Labels append verbatim, matching String Append in the existing helper chain. For a base name `Cliff`, use label
`_Barnacles` to obtain `Cliff_Barnacles`. No separator or group prefix is inserted automatically. PCGActorBase
already constructs `BakedAssetSaveName` with its group; it must not be prefixed a second time. Name/folder
validation reports invalid planned object paths without silently changing the requested asset identity.

With both input mode and data-domain output enabled, In and Out start with generic Any typing. An upstream type
specializes both pins; with no upstream constraint, a typed downstream connection specializes both pins. Removing
connections restores Any. Attribute Set output always stays Param and does not constrain the incoming streams.

Actor reads execute on the main thread and do not use input-only result caching, so changed bake settings are read
on the next generation. Invalid settings preserve original input streams unchanged in data-domain mode, with a
precise graph diagnostic. Attribute Set mode cannot fabricate missing actor settings.

This is separate from **Get Asset Save Parameters**, which derives parameters from an existing asset reference.
Replacing retained bake subgraph usages and relocating obsolete helpers remain release migration tasks; adding
this native element does not edit saved graphs or verify their current override precedence.
