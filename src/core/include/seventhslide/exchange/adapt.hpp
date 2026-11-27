#pragma once

// What a Freeform file's documents become on the way out of one library and
// into another: the decks that go, which output each lands on, and the files
// they name. Pure: no IO, so every rule is tested on its own.
// Plain C++20 + nlohmann/json.

#include "seventhslide/exchange/bundle.hpp"
#include "seventhslide/freeform/document.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::exchange {

/// An output of the library a file is imported into.
struct LibraryOutput {
    std::string id;
    std::string name;
    geom::CanvasSize canvas;
};

/// For each of a file's outputs, by place, the library output it goes to;
/// empty: it is not imported.
using OutputMapping = std::vector<std::string>;

/// Out: ``doc`` with only the decks of ``output_ids`` (the library's outputs:
/// a deck for any other is never shown). True when a deck is left.
bool keep_decks_of(freeform::Document& doc, const std::set<std::string, std::less<>>& output_ids);

/// In: the library outputs ``output`` may go to, by place in ``outputs``:
/// those of exactly its shape (``geom::same_aspect``).
[[nodiscard]] std::vector<std::size_t> outputs_that_fit(const BundleOutput& output,
                                                        const std::vector<LibraryOutput>& outputs);

/// The first choice for each of ``file`` 's outputs, each library output
/// given at most once, in this order: the output of the same id; else of the
/// same name (case aside) and shape; else the first of its shape not given;
/// else none. Each rule is tried for every file output before the next.
[[nodiscard]] OutputMapping suggest_mapping(const std::vector<BundleOutput>& file,
                                            const std::vector<LibraryOutput>& outputs);

/// Why ``mapping`` cannot be used, or empty when it can: a target that is not
/// a library output or not of its output's shape, one given twice, or none
/// given at all.
[[nodiscard]] std::string mapping_refusal(const std::vector<BundleOutput>& file,
                                          const std::vector<LibraryOutput>& outputs,
                                          const OutputMapping& mapping);

/// Every file entry ``doc`` names (``media/…``).
[[nodiscard]] std::set<std::string> entries_named(const freeform::Document& doc);

struct Adapted {
    std::optional<freeform::Document> document;
    /// Why it was not, for people; empty when it was.
    std::string refusal;
};

/// In: ``doc`` as it lands. Each deck goes to its output's target in
/// ``mapping`` and is fitted to that output's canvas; a deck whose output
/// has none, or that is for an output the file does not list, goes. Each
/// file it names becomes ``files[entry]``, and a reference to anything not
/// in ``files`` is emptied. Refused when no deck is left, or when the
/// document is larger than a library record may be.
[[nodiscard]] Adapted adapt_document(freeform::Document doc, const std::vector<BundleOutput>& file,
                                     const std::vector<LibraryOutput>& outputs,
                                     const OutputMapping& mapping,
                                     const std::map<std::string, std::string, std::less<>>& files);

/// Out: each reference in ``doc`` that ``files`` has becomes its entry, and
/// any other is emptied (a file the library did not have).
void point_at_entries(freeform::Document& doc,
                      const std::map<std::string, std::string, std::less<>>& files);

}  // namespace seventhslide::exchange
