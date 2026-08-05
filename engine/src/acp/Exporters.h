#pragma once

#include <eng/acp/Exporter.h>

#include <memory>
#include <vector>

// Factories for the built-in exporter rows. Private to eng_acp: registration
// order lives in Registry.cpp, and each row keeps its own translation unit so
// the Assimp include, the stb include and the TOML include stay one per file.
namespace eng::acp {

std::unique_ptr<Exporter> makeMeshExporter();          // -> .rmesh
std::unique_ptr<Exporter> makeTextureExporter();       // -> .rtex
std::unique_ptr<Exporter> makeParticleExporter();      // -> .rpfx
std::unique_ptr<Exporter> makeSoundBankExporter();     // -> .rbank
std::unique_ptr<Exporter> makeObjectTemplateExporter();// -> .rtpl
std::unique_ptr<Exporter> makeAnimationTreeExporter(); // -> .rtree

// The rows the diagram routes straight into the pipeline with no processor box
// between: Material, Sound, Skeleton, Animation, plus the engine data the
// runtime reads by path (shaders, fonts, scripts, config, UI). They are copied
// into the pack and validated; the value is that the pack is complete and every
// dependency is tracked, not that the bytes changed.
std::vector<std::unique_ptr<Exporter>> makePassthroughExporters();

// The same copy, typeless, for an asset that is already an intermediate.
std::unique_ptr<Exporter> makeCopyExporter();

} // namespace eng::acp
