# Native DatumLoop project format (`.dld`)

The native application saves projects as UTF-8 JSON in a `.dld` file. Schema
version 5 is provisional and stores the document state supported by the current
native core:

- project UUID and name;
- object UUID, name, optional parent UUID, and typed scalar properties;
- object translation in millimetres;
- object-scoped feature definitions and selected output feature UUID;
- exact OCCT B-Rep geometry, encoded as Base64 with its OCCT stream version;
- persistent face, edge, and vertex references with owner identity, optional
  feature identity, geometric/topological signatures, and parent references;
- engineering mechanisms and datum axes linked to CAD body object IDs;
- motors with optional CAD representation, physical properties, actuator data,
  mounting references, encoder, and supplier data;
- common material definitions and states with optional composition, phase,
  microstructure, process-history, property-model, provenance, and dataset
  reference data;
- circuit documents with UUID-backed components, pins, nets, optional values,
  and explicit units;
- PCB documents with stack-up/layer material references, placed components,
  pads, traces, vias, and common-material regions.

Example structure (the geometry payload is abbreviated):

```json
{
  "format": "datumloop-project",
  "schemaVersion": 5,
  "project": { "id": "…", "name": "Assembly" },
  "objects": [{
    "id": "…",
    "name": "Base",
    "parentId": null,
    "properties": {
      "material": { "type": "string", "value": "6061-T6" },
      "reference": { "type": "bool", "value": true }
    },
    "featureDefinitions": [{
      "id": "…",
      "ownerObjectId": "…",
      "type": "primitive.box",
      "inputFeatureIds": [],
      "parameters": { "xMm": 40, "yMm": 30, "zMm": 20 },
      "kernelBackend": "occt"
    }],
    "outputFeatureId": "…",
    "references": [],
    "transform": { "translationMm": [0, 0, 0] },
    "geometry": {
      "format": "occt-brep-ascii-v1",
      "occtFormatVersion": 3,
      "encoding": "base64",
      "data": "…"
    }
  }],
  "topologyReferences": [{
    "id": "…",
    "objectId": "…",
    "featureId": "…",
    "subshapeType": "face",
    "topologySignature": "face|plane|…",
    "geometrySignature": {
      "geometryType": "plane",
      "measure": 1200,
      "centroidMm": [20, 0, 0],
      "direction": [1, 0, 0],
      "adjacent": []
    },
    "parentReferenceIds": [],
    "transientIndexHint": 0
  }],
  "engineeringModel": {
    "mechanisms": [{
      "id": "…",
      "name": "Linear axis",
      "bodyObjectIds": ["…", "…"],
      "axes": [{
        "id": "…",
        "name": "X travel",
        "type": "linear",
        "baseObjectId": "…",
        "movingObjectId": "…",
        "originInBaseFrameM": [0, 0, 0],
        "directionInBaseFrame": [1, 0, 0],
        "actuatorMotorId": "…"
      }]
    }],
    "motors": [{
      "id": "…",
      "mechanismId": "…",
      "name": "Servo motor",
      "cadRepresentationObjectId": "…",
      "massKg": null,
      "inertiaTensorKgM2": null,
      "centerOfMassLocalM": null,
      "ratedSpeedRadPerSec": null,
      "maximumSpeedRadPerSec": null,
      "ratedTorqueNm": null,
      "peakTorqueNm": null,
      "torqueCurve": [],
      "electrical": {
        "ratedVoltageV": null,
        "ratedCurrentA": null,
        "peakCurrentA": null
      },
      "thermal": {
        "thermalResistanceKPerW": null,
        "thermalCapacityJPerK": null,
        "maximumWindingTemperatureK": null
      },
      "encoder": null,
      "mountingReferences": [],
      "supplierData": null
    }]
  }
}
```

The property types remain `bool`, signed `int64`, finite `double`, and
UTF-8 `string`. Signed 64-bit values are decimal strings in JSON so integers
above JavaScript's exact-number range remain lossless. Parent objects must
precede children in the object array; this both validates parent identity and
prevents hierarchy cycles in the current append-only document model.

The topology resolver captures faces, edges, and vertices from exact OCCT
shapes. Its initial deterministic matcher compares subshape kind and geometry
type, relative area or length (mm² for faces, mm for edges, zero for vertices),
centroid in millimetres, direction when available, and sorted adjacency
descriptors. It accepts only a unique best match under declared
tolerances. A tie or a poor match remains unresolved; the transient enumeration
index is diagnostic and never breaks a tie. Parent-reference IDs retain local
adjacency relationships (edges can name incident faces; vertices can name
incident edges). Parent references must appear before child references in the
topology-reference array. Feature-backed references are matched only against
their current output feature; the resolver does not yet migrate them across
arbitrary graph edits.

`DldProjectStore` writes OCCT's current ASCII B-Rep stream with triangulation
and normals disabled. The display mesh is never saved. Loading validates the
entire file into a temporary document, recomputes shape metrics, then the UI
replaces its document and clears the old command history. A failed load leaves
the open document untouched. Saving uses `QSaveFile`; the document becomes
clean only after the atomic file commit succeeds.

Schema version 5 reads versions 1 through 4. Version 1 files receive initial
face, edge, and vertex references; version 2 through 4 references, object
identities, and supported engineering data are preserved. Versions 1 through 3
load with an empty engineering model; versions 1 through 4 load with an empty
electronics model. Engineering data in a schema 1, 2, or 3 file and electronics
data in a schema 1 through 4 file are rejected rather than discarded. Schema 5
requires its electronics model. Unknown schema versions are rejected. Equation
definitions, physics solver requests/results, robot/sensor/action specs, and
simulation state are not yet persisted in schema 5. The object feature graph
is an append-ordered DAG with
explicit feature UUIDs, owner, type, input feature UUIDs,
parameters in millimetres, and a geometry backend identifier. The first native
OCCT backend supports box, cylinder, sphere, and cone primitives, translation,
and Boolean union, cut, and intersection. Inputs must refer to earlier features
in the same object's graph, making recompute deterministic and cycle-free.

Each in-memory feature records recompute status, an error string, and its exact
generated B-Rep. The `.dld` stores definitions plus the object's final exact
B-Rep; individual intermediate B-Reps and display tessellation are not stored.
Loading recomputes the graph and checks the final shape's volume, surface area,
center of mass, and bounds against the persisted B-Rep before accepting it.
These metrics provide a consistency guard, not proof of exact geometric
identity. An unsupported backend or failed node is never silently routed through OCCT;
failed recomputes retain their status/error and do not replace the last valid
object shape.

Feature graph replacement is an undoable document command. It creates a new
initial topology-reference set for that object's output; it does not yet
preserve subshape UUIDs through arbitrary feature changes. Object-to-object
references, sketch and manufacturing features, rotational transforms,
assembly relationships, joints, simulation state, and a general migration
framework remain unsupported. `DatumEngineeringModel` currently supports
mechanisms, linear/rotary datum axes, and motors only. An axis records body
references, a metre-valued origin, and a dimensionless direction; it does not
apply a kinematic constraint. Motor mass is in kg, inertia in kg m², center of
mass and axis origin in m, speed in rad/s, torque in N·m, voltage in V, current
in A, thermal resistance in K/W, thermal capacity in J/K, and temperature in
K. Optional physical fields remain unknown when `null`; the application does
not infer them from CAD or display tessellation. Non-empty per-object
`references` arrays are rejected rather than silently discarded. Geometry is
embedded as Base64, which keeps the write atomic but increases file size; a future version may move large
geometry payloads into assets.

The tests cover exact-shape and feature-graph round-trip, primitive and Boolean
recompute, UUID identity, hierarchy, translation, all four property types, dirty
tracking, schema-v1/v2/v3/v4 migration, engineering and electronics/material
round-trip and reference validation, face/edge/vertex reference resolution and point picking, ambiguity
rejection, parent relationships, display tessellation regeneration, and
rejection of unsupported or malformed data.
