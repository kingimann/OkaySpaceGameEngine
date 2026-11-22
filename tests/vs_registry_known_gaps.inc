    // Every node is now proven by tests/test_vs_registry_effect.cpp (the generic scene, a rich scene with the
    // component the node drives, or a targeted scenario). Add a name here only with a reason.
    "",
    "QbSaveNumber", "QbHealFully",   // proven in test_vs_quick_blocks (they need a saved value or a health bar first)
    "QbRespawnThis",                 // NOT proven yet: wraps the plain Respawn node, which needs a dead object
    "QbTurnToFace", "QbFlashColour",   // proven in test_vs_quick_blocks (they need a named object or a mesh to colour)
