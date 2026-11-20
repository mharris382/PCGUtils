# Authored path and marker names

`FPathComponentData.PathName` and `FPointComponentData.MarkerName` are editable, Blueprint-readable/writable
`FName` labels. They default to `None` and are independent of component object names, tags and numeric group IDs.
They are descriptive labels; several components may use the same name.

- On PCG Spline and Shape Path components, set **Path Data > Path Name**.
- On PCG Marker components, set **Point Data > Marker Name**.
- Provider implementations returning the shared path/point data structs can supply these names too.

The corresponding reader settings expose **Extract Path Name** and **Extract Marker Name**, both off by default,
with editable `FName` output attribute names defaulting to `PathName` and `MarkerName`. The attributes have PCG
**Name** type, not String type.

Path names are written to `@Data` on each output path, for both point and spline representations. Marker names
follow the reader's existing **Attribute Domain** setting: point elements by default, or `@Data` when selected.
The shared extraction settings also apply to Get PCG Point Data, Get PCG Spline Data and Get Shape Path Data.

For example, name a path `DisplayBoundary`, enable **Extract Path Name**, and filter datasets by
`@Data.PathName == DisplayBoundary`. Name a marker `DemoOrigin`, enable **Extract Marker Name**, and filter its
point attribute `MarkerName` (or `@Data.MarkerName` in Data mode). The component modifier stack carries the source
name into its copies; it does not generate unique names for each copy.

An authored `None` is emitted as a valid Name value when extraction is enabled. A missing output attribute name
produces a warning and skips that optional attribute. Existing attributes of a different type are preserved if
they collide with the requested name. Existing assets need no migration: the new labels default to `None`, and
the new extraction flags default to off.
