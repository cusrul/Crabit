# NanoUI Asset Contract

`manifest.json` is the shared, target-independent resource contract. The STM32
and BW16 generators may emit different resource profiles, but they must consume
the same IDs, Unicode semantics and required runtime strings.

The existing GUI `asset_builder.py` remains a material-authoring tool. The
existing `font_builder.py` scanners remain supplemental discovery inputs. They
must not silently overwrite the other target's generated files. A later R01
step will connect both to a deterministic headless compiler; this contract can
already be checked without touching generated firmware assets.
