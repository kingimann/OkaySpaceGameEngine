# Our own SIB reader (`.sib`): NOT WRITTEN

Status: **no reader**. Slot 32 of the table in `ModelImport.cpp` is reserved for it (an empty row; `.sib` is routed nowhere, and `test_own_dispatch_e2` checks that), `OKAY_OWN_SIB` does not exist.

Why: SIB is the binary file of the Silo modeller (Nevercenter). The search for a public description of its structure found only that it exists (the binary twin of the ASCII `.sia`); no field list, no chunk list, no sample file (the only one, `heffalump.sib`, is in Assimp's test tree, not here). The rule for this work is to write from a format description or from our own analysis of sample files only, never from another importer's source. With neither a description nor a sample, a reader would be invented, so none was written. To go on: a published description, or the sample file to analyse (then `ModelData`, budgets and tests as for `docs/content/model_readers/own_q3d_reader.md`).
