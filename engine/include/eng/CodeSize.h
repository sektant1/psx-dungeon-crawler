#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// How many bytes of machine code each function compiled to, read out of this
// process's own symbol table.
//
// This is the third axis of the same question the profiler and the heap
// profiler ask. A frame has three budgets that all get spent without anyone
// noticing: time (eng::Profiler), heap (eng::memprof), and *space* -- and the
// last one is the one with no tool pointed at it. Code size matters for the
// same reason data size does: instruction cache misses cost more than most of
// the arithmetic they interrupt, a template instantiated four hundred times is
// four hundred copies of the same loop, and none of that shows up in a frame
// time until it does.
//
// It is read from the running binary rather than produced by the build, so it
// needs no build step, no map file and no external tool -- the same reasoning
// that made Connector speak RESP itself rather than require a Redis install.
//
// Linux/ELF only. Everywhere else this returns an empty list and the callers
// say "not available" rather than pretending. A stripped binary has no .symtab
// either; the default Debug build has one.

namespace eng::codesize {

struct Symbol {
    std::string name;      // demangled where the ABI could
    std::uint64_t bytes = 0;
};

// Every function symbol with a non-zero size, largest first.
//
// Built on each call and NOT cached. That is deliberate: this binary has over a
// hundred thousand function symbols, and keeping their demangled names alive
// costs about 5 MB for the life of the process -- which the heap profiler next
// door duly reported as the single largest allocation site in the engine. Every
// aggregation below makes its own pass and keeps only its result, so nothing
// here is retained between calls. Prefer byOwner() unless you genuinely want
// per-function rows.
std::vector<Symbol> functions();

// How many function symbols there are, without building any of their names.
std::size_t functionCount();

// Functions folded into their owning namespace/class -- `eng::Renderer::draw`
// and `eng::Renderer::cull` become one `eng::Renderer` entry. Largest first.
//
// This is what a treemap of code size should show. Individual functions are
// mostly small and mostly uninteresting; the thing worth seeing is that one
// subsystem is a third of the binary, and that only shows up once its functions
// are added together.
//
// Computed once and cached, because demangling this binary's symbol table takes
// roughly 290 ms and the table cannot change while the process runs. Only the
// aggregate is retained (tens of kilobytes), never the per-function names.
// Call warm() first, from somewhere a pause does not matter.
const std::vector<Symbol>& byOwner();

// Pays the one-off parse now, so the first byOwner() on a frame is free. The
// app calls this during load; without it the cost lands on whichever frame
// happens to ask first, which is a visible hitch.
void warm();

// Total bytes across every module, from the cached aggregate -- unlike
// totalBytes(), which re-reads the symbol table.
std::uint64_t codeBytes();

// Total bytes of code across every function symbol. Smaller than the file: this
// counts functions, not data, headers, relocations or debug info.
std::uint64_t totalBytes();

// False when there was no symbol table to read (stripped, or not ELF).
bool available();

} // namespace eng::codesize
