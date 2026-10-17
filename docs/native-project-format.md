# Native DatumLoop project format (`.dld`)

The native application saves projects as UTF-8 JSON in a `.dld` file. Schema
version 1 is provisional and stores the document state supported by the current
native core:

- project UUID and name;
- object UUID, name, optional parent UUID, and typed scalar properties;
- object translation in millimetres;
- exact OCCT B-Rep geometry, encoded as Base64.

Example structure (the geometry payload is abbreviated):

```json
{
  "format": "datumloop-project",
  "schemaVersion": 1,
  "project": { "id": "…", "name": "Assembly" },
  "objects": [{
    "id": "…",
    "name": "Base",
    "parentId": null,
    "properties": {
      "material": { "type": "string", "value": "6061-T6" },
      "reference": { "type": "bool", "value": true }
    },
    "featureDefinitions": [],
    "references": [],
    "transform": { "translationMm": [0, 0, 0] },
    "geometry": {
      "format": "occt-brep-ascii-v1",
      "occtFormatVersion": 3,
      "encoding": "base64",
      "data": "…"
    }
  }]
}
```

Property types in version 1 are `bool`, signed `int64`, finite `double`, and
UTF-8 `string`. Signed 64-bit values are decimal strings in JSON so integers
above JavaScript's exact-number range remain lossless. Parent objects must
precede children in the object array; this both validates parent identity and
prevents hierarchy cycles in the current append-only document model.

`DldProjectStore` writes OCCT's current ASCII B-Rep stream with triangulation
and normals disabled. The display mesh is never saved. Loading validates the
entire file into a temporary document, recomputes shape metrics, then the UI
replaces its document and clears the old command history. A failed load leaves
the open document untouched. Saving uses `QSaveFile`; the document becomes
clean only after the atomic file commit succeeds.

Version 1 intentionally does **not** implement parametric feature definitions,
persistent topology references, rotational transforms, assemblies, engineering
entities, simulation state, or schema migrations. The `featureDefinitions` and
`references` arrays are written empty; a file with non-empty entries is rejected
rather than silently losing data. Unknown schema versions are also rejected.
Those structures require the later feature-graph and persistent-topology
milestones. Geometry is embedded as Base64, which is simple and atomic for this
first format but increases file size; a future version may move large geometry
payloads into assets with explicit migration support.

The storage tests cover exact-shape round-trip, UUID identity, hierarchy,
translation, all four property types, dirty tracking, display tessellation
regeneration, and rejection of unsupported or malformed data.
