#include "seventhslide/exchange/adapt.hpp"

#include "seventhslide/db/sync_records.hpp"
#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/freeform/media_refs.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>

namespace seventhslide::exchange {

namespace {

[[nodiscard]] const LibraryOutput* find_output(const std::vector<LibraryOutput>& outputs, std::string_view id)
{
    const auto it = std::ranges::find(outputs, id, &LibraryOutput::id);
    return it == outputs.end() ? nullptr : &*it;
}

void repoint(freeform::Document& doc, const std::map<std::string, std::string, std::less<>>& files)
{
    freeform::visit_document_media(doc, [&files](std::string& path, canvas::MediaRefKind) {
        if (path.empty()) {
            return;
        }
        const auto it = files.find(path);
        path = it == files.end() ? std::string() : it->second;
    });
}

}  // namespace

bool keep_decks_of(freeform::Document& doc, const std::set<std::string, std::less<>>& output_ids)
{
    std::erase_if(doc.decks, [&](const freeform::Deck& d) { return !output_ids.contains(d.output_id); });
    return !doc.decks.empty();
}

std::vector<std::size_t> outputs_that_fit(const BundleOutput& output, const std::vector<LibraryOutput>& outputs)
{
    std::vector<std::size_t> fit;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        if (geom::same_aspect(output.canvas, outputs[i].canvas)) {
            fit.push_back(i);
        }
    }
    return fit;
}

OutputMapping suggest_mapping(const std::vector<BundleOutput>& file, const std::vector<LibraryOutput>& outputs)
{
    OutputMapping mapping(file.size());
    std::vector<bool> given(outputs.size(), false);
    const auto give = [&](std::size_t from, std::size_t to) {
        mapping[from] = outputs[to].id;
        given[to] = true;
    };
    const auto rule = [&](auto&& matches) {
        for (std::size_t f = 0; f < file.size(); ++f) {
            if (!mapping[f].empty()) {
                continue;
            }
            for (const auto to : outputs_that_fit(file[f], outputs)) {
                if (!given[to] && matches(file[f], outputs[to])) {
                    give(f, to);
                    break;
                }
            }
        }
    };
    rule([](const BundleOutput& a, const LibraryOutput& b) { return a.id == b.id; });
    rule([](const BundleOutput& a, const LibraryOutput& b) {
        return !a.name.empty() && util::iequals(a.name, b.name);
    });
    // The rest, in order: the first output of its shape not given. Shapes
    // match exactly, so outputs of one shape fit the same outputs here and
    // outputs of two shapes fit none in common: giving them in order gives
    // each shape as many as it can have.
    rule([](const BundleOutput&, const LibraryOutput&) { return true; });
    return mapping;
}

std::string mapping_refusal(const std::vector<BundleOutput>& file, const std::vector<LibraryOutput>& outputs,
                            const OutputMapping& mapping)
{
    if (mapping.size() != file.size()) {
        return "Choose where each of the file's outputs goes.";
    }
    std::set<std::string, std::less<>> given;
    for (std::size_t f = 0; f < file.size(); ++f) {
        if (mapping[f].empty()) {
            continue;
        }
        const auto* target = find_output(outputs, mapping[f]);
        if (target == nullptr) {
            return "An output chosen is not in this library.";
        }
        if (!geom::same_aspect(file[f].canvas, target->canvas)) {
            return "“" + target->name + "” is not the shape of “" + file[f].name + "”.";
        }
        if (!given.insert(mapping[f]).second) {
            return "“" + target->name + "” is chosen for two of the file's outputs.";
        }
    }
    if (given.empty()) {
        return "Choose an output for at least one of the file's outputs.";
    }
    return {};
}

std::set<std::string> entries_named(const freeform::Document& doc)
{
    std::set<std::string> named;
    freeform::visit_document_media(doc, [&named](const std::string& path, canvas::MediaRefKind) {
        if (!path.empty()) {
            named.insert(path);
        }
    });
    return named;
}

void point_at_entries(freeform::Document& doc, const std::map<std::string, std::string, std::less<>>& files)
{
    repoint(doc, files);
}

Adapted adapt_document(freeform::Document doc, const std::vector<BundleOutput>& file,
                       const std::vector<LibraryOutput>& outputs, const OutputMapping& mapping,
                       const std::map<std::string, std::string, std::less<>>& files)
{
    Adapted out;
    std::vector<freeform::Deck> decks;
    // An output here takes one deck, whatever the file or the mapping says.
    std::set<std::string, std::less<>> given;
    for (auto& deck : doc.decks) {
        const auto it = std::ranges::find(file, deck.output_id, &BundleOutput::id);
        if (it == file.end()) {
            continue;
        }
        const auto place = static_cast<std::size_t>(it - file.begin());
        const auto* target = place < mapping.size() && !mapping[place].empty()
            ? find_output(outputs, mapping[place])
            : nullptr;
        if (target == nullptr || !geom::same_aspect(file[place].canvas, target->canvas)
            || !given.insert(target->id).second) {
            continue;
        }
        deck.output_id = target->id;
        freeform::resize_deck(deck, target->canvas);
        decks.push_back(std::move(deck));
    }
    if (decks.empty()) {
        out.refusal = "None of its outputs is imported.";
        return out;
    }
    doc.decks = std::move(decks);
    repoint(doc, files);
    freeform::normalize(doc);
    const auto stored = doc.to_json();
    if (const auto why = freeform::document_refusal(stored); !why.empty()) {
        out.refusal = why;
        return out;
    }
    if (stored.dump().size() > db::k_largest_record) {
        out.refusal = "It is larger than a library item may be.";
        return out;
    }
    out.document = std::move(doc);
    return out;
}

}  // namespace seventhslide::exchange
