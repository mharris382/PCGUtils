# Input Shorthands

An input shorthand is a node setting that is either a **constant** on the node or a value read **per input from
that input's `@Data` domain**. It replaces a plain `double`/`int32`/`FVector`/... property so that each data set
can carry its own value. Example: `DynMesh | Extrude Faces` reads `Distance` from `@Data.PathHeight`, so every
room mesh made from a path extrudes to that path's height, with no loop subgraph.

Adapted from PCGExtendedToolkit's `PCGExInputShorthandsDetails` (MIT). PCGUtils does not depend on PCGEx; the
types live in `PCGUtilsCore` (`Details/PCGUtilsInputShorthands.h`).

## Behaviour

- **Modes.** `Constant` uses the node value. `@Data` reads the attribute named in `Attribute`.
- **Strict reads.** In `@Data` mode the attribute must exist on every input and hold a readable type. A missing
  attribute, a wrong type, a property selector (`$Position`), a non-`@Data` domain, or (for ranged types) a value out
  of range fails that input with one specific error, for example
  `Distance (input 2): attribute 'PathHeight' was not found in the input's @Data domain`. The constant is never a
  fallback.
- **Types.** Floating-point settings accept Integer32, Integer64, Float or Double attributes. Integer settings accept
  integer attributes only; a fractional value is never truncated. Vectors and rotators need the exact type. Name and
  String interconvert. Extractors work: `Size.Z`.
- **`@Data.` prefix.** Optional on DynMesh data, whose default metadata domain is `@Data`. Required on point data,
  where a bare name means the point domain; the error says so and shows the prefixed form.
- **Ranged variants** (`DoubleAbs`, `Double01`, `Integer32Abs`) reject out-of-range `@Data` values rather than
  clamping them.
- **Builders.** A Builder has no `@Data` until it is realized, so a DynMesh process with a shorthand in `@Data` mode
  refuses Builder inputs with an error (`GetBuilderInputRejectionReason`). In Constant mode Builders work as before.
- **Details panel.** One row: the constant's widget or the attribute field, plus a toggle button for the mode
  (`FPCGUtilsInputShorthandCustomization`, auto-registered for every shorthand struct).

Available types: `Boolean`, `Integer32`, `Integer32Abs`, `Integer64`, `Float`, `Double`, `DoubleAbs`, `Double01`,
`Vector2`, `Vector`, `Vector4`, `Rotator`, `Name`, `String` (all `FPCGUtilsInputShorthand<Type>`).

Not done yet: per-element reads (one value per point/vertex), and reading DynMesh vertex data such as position,
normal, color or UV as if they were attributes. Both fit behind the same struct later.

## Keeping existing graphs working

Two things must survive the change: the saved value, and any wire into the setting's override pin.

**Override pins.** PCG labels a nested override param by its leaf name, so a shorthand member `Distance` would get
pins `Constant` and `Attribute`. `PCGUtilsInputShorthands::ApplyOverrideLabels` (called from
`UPCGUtilsDynMeshProcessBaseSettings::GatherOverridableParams`) relabels them `Distance` and `DistanceAttribute`.
The Constant pin therefore keeps the exact label the old plain property had, and saved edges stay connected with
no pin rename. A label that would collide falls back to the full property path.

**Values.**

- *A plain top-level property becomes a shorthand of the same name* (for example `float Radius` →
  `FPCGUtilsInputShorthandDouble Radius`): nothing to write for scalars, Name and String. The shorthand loads the
  old bare value into `Constant` through `SerializeFromMismatchedTag`. Vector and rotator properties have no such
  path; use the next pattern.
- *The value lived elsewhere* (inside an engine options struct, or a vector): rename the old property to
  `<Name>_DEPRECATED` (UHT strips the suffix, so saved data still loads into it), copy it across in `PostLoad` when
  `GetLinkerCustomVersion(FPCGUtilsCustomVersion::GUID) < FPCGUtilsCustomVersion::InputShorthands`, and log the
  migration. `UPCGUtilsDynMeshProcessBaseSettings::Serialize` stamps the version on save. Nodes outside the DynMesh
  process base need their own `Serialize` that calls `Ar.UsingCustomVersion(FPCGUtilsCustomVersion::GUID)`.
- A node migrated this way re-migrates on every load until its asset is resaved. That is harmless; resave when
  convenient.

**Checklist for upgrading a node**

1. Replace the property with the matching shorthand; give it a default constant and a default attribute name.
2. Document in the property tooltip: what the attribute means, its expected type, that it is required in `@Data`
   mode, and that the constant is not a fallback.
3. In the operation, read with `TryReadDataValue(Source, Value, Error, Label, InputIndex)` per input and log
   `Error` on failure. DynMesh topology operations resolve into a local copy of the operation (see
   `FExtrudeOperation::Execute`), because operations are shared and immutable.
4. If the node supports Builders, return `IsConstant()` from `SupportsDeferredBuilderProcessing()`, give a
   rejection reason, and make the shorthand's property change `Structural` so the pins refresh.
5. If the value moved, add the `_DEPRECATED` + `PostLoad` migration.
6. Find graphs that use the node (`grep -rl <SettingsClassName>` over `Content` and `Plugins/*/Content`),
   snapshot their values and override wiring headlessly before building, then load them after and compare.
7. Add a row to the upgrade log below.

## Upgrade log

| Node | Setting | Type | Old storage | Migration | Project graphs checked |
| --- | --- | --- | --- | --- | --- |
| DynMesh \| Extrude Faces | Distance | `Double` (default attribute `ExtrudeDistance`) | `Options.Distance` (float in `FGeometryScriptMeshLinearExtrudeOptions`) | `Options` → `Options_DEPRECATED`; all its fields flattened onto the node (`DirectionMode`, `Direction`, `AreaMode`, `GroupOptions`, `UVScale`, `bSolidsToShells`); pin labels unchanged | `CourseGen_Template_CutterPath`, `ExtrudeAndOffsetCutter`: Distance 1.0 kept, both `Distance` override wires still connected (2026-10-17) |
| DynMesh \| Inset Faces | Distance | `Double` (default attribute `InsetDistance`) | `Options.Distance` (float in `FGeometryScriptMeshInsetOutsetFacesOptions`) | `Options` -> `Options_DEPRECATED`; fields flattened (`bReproject`, `bBoundaryOnly`, `Softness`, `AreaScale`, `AreaMode`, `GroupOptions`, `UVScale`); pin labels unchanged | GRAPHS_INSET |
| DynMesh \| Bevel Edges | BevelDistance | `DoubleAbs` (default attribute `BevelDistance`) | top-level `float BevelDistance` | none: loads through `SerializeFromMismatchedTag`; pin label unchanged | GRAPHS_BEVEL |
| DynMesh \| Decimate | Ratio (`Double01`), Use Weight Map (`Boolean`), Weight Map Name (`Name`), Weight Factor (`Double`), Iterations (`Integer32Abs`), Angle Limit (`DoubleAbs`), All Boundaries, Delimit Material / Polygroup / UV Seams / Normal Seams (`Boolean`) | defaults `DecimateRatio`, `UseWeightMap`, `WeightMapName`, `WeightFactor`, `UnSubdivideIterations`, `PlanarAngleLimit`, `AllBoundaries`, `DelimitMaterial`, `DelimitPolygroup`, `DelimitUVSeams`, `DelimitNormalSeams` | top-level plain properties of the same names | none: load through `SerializeFromMismatchedTag`; pin labels unchanged. Only the settings the Mode uses are read per input. `Mode` (enum) and `CollapseOptions` (engine struct) stay plain constants | Not yet checked (grep `DynMeshDecimateSettings` over graphs) |

| Path \| Decompose Closed | AreaWeight, LengthWeight, MaxConcavity, MaxQuadWarp, MaxPolygonWarp, MaxInputPoints | Double / Integer32 | CourseGen experimental scalar settings | Class redirect; same-name scalar shorthand deserialization; override labels retained | Native tests; spline input wires must be replaced with point paths |

## Candidates

A 2026-10-17 survey found about 150 plain numeric/vector overridable settings in PCGUtilsDynMesh, 22 in
PCGUtilsPainter and 5 in PCGUtils. Nodes that keep Geometry Script option structs (and so need the flatten
pattern Extrude used): Inset (`Options`), Mirror, Planar Cut, Self Union, Simplify, Remesh, Boolean, Sample, and
the primitive Builders. Done: Extrude, Inset and Bevel distances. Likely next: Painter strengths/radii, Bevel Subdivisions/Round Weight.
`DynMesh | Set Vertex Colors` has a hand-rolled `bUseDataAttributeColor` + `ColorAttributeName` pair that predates
this and should become a `Vector4` shorthand.
