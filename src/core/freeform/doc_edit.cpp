// Freeform documents: ids, construction, loading and normalising.
// The edits are split by what they work on: doc_slides.cpp (slides),
// doc_cues.cpp (cues and spans), doc_linked.cpp (cue-linked text) and
// doc_objects.cpp (objects on a slide).

#include "seventhslide/freeform/doc_edit.hpp"

#include "doc_internal.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/util/find.hpp"
#include "seventhslide/util/random_id.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace seventhslide::freeform {
std::vector<int> identity_map(int n)
{
    std::vector<int> m(static_cast<std::size_t>(std::max(0, n)));
    for (int i = 0; i < n; ++i) {
        m[static_cast<std::size_t>(i)] = i;
    }
    return m;
}

namespace {

// One past the largest number alloc_id counts out.
constexpr int k_id_top = std::numeric_limits<int>::max();

[[nodiscard]] int max_slide_count(const Document& doc) noexcept
{
    int n = 0;
    for (const auto& deck : doc.decks) {
        n = std::max(n, deck.slide_count());
    }
    return n;
}

void bump_next_id(Document& doc, std::string_view id)
{
    if (id.size() < 2 || (id[0] != 'c' && id[0] != 's' && id[0] != 'o')) {
        return;
    }
    int n = 0;
    for (std::size_t i = 1; i < id.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(id[i]);
        if (c < '0' || c > '9') {
            return;
        }
        // A number at or past k_id_top is no id alloc_id counts out, and
        // next_id stops at k_id_top.
        if (n > (k_id_top - 1 - (c - '0')) / 10) {
            return;
        }
        n = n * 10 + (c - '0');
    }
    doc.next_id = std::max(doc.next_id, n + 1);
}

void scan_ids(Document& doc)
{
    for (const auto& cue : doc.cues) {
        bump_next_id(doc, cue.id);
    }
    for (const auto& deck : doc.decks) {
        for (const auto& slide : deck.slides) {
            bump_next_id(doc, slide.id);
            for (const auto& obj : slide.objects) {
                bump_next_id(doc, obj.id);
            }
        }
    }
}

} // namespace

[[nodiscard]] Slide empty_slide(Document& doc)
{
    Slide s;
    s.id = alloc_id(doc, 's');
    return s;
}

[[nodiscard]] Slide clone_slide(Document& doc, const Slide& src)
{
    Slide copy = src;
    copy.id = alloc_id(doc, 's');
    for (auto& obj : copy.objects) {
        obj.id = alloc_id(doc, 'o');
    }
    return copy;
}

void make_non_decreasing(std::vector<int>& map, int slide_count)
{
    const int last = std::max(0, slide_count - 1);
    int floor = 0;
    for (int& v : map) {
        if (v < floor) {
            v = floor;
        }
        if (v > last) {
            v = last;
        }
        floor = v;
    }
}

void pad_map(Deck& deck, int cue_count)
{
    const int last = std::max(0, deck.slide_count() - 1);
    while (static_cast<int>(deck.line_to_slide.size()) < cue_count) {
        deck.line_to_slide.push_back(last);
    }
    if (static_cast<int>(deck.line_to_slide.size()) > cue_count) {
        deck.line_to_slide.resize(static_cast<std::size_t>(std::max(0, cue_count)));
    }
    make_non_decreasing(deck.line_to_slide, deck.slide_count());
}

std::string alloc_id(Document& doc, char prefix)
{
    if (doc.next_id < 1) {
        doc.next_id = 1;
    }
    std::string id;
    id.push_back(prefix);
    if (doc.next_id >= k_id_top) {
        // Every number is spent: the document holds an id at the top of the
        // count. An "x" and random digits are no number, so they neither
        // match a counted id nor move the count.
        id.push_back('x');
        id += util::random_hex_id(2);
        return id;
    }
    id += std::to_string(doc.next_id);
    ++doc.next_id;
    return id;
}

Deck* find_deck(Document& doc, std::string_view output_id)
{
    return util::find_by(doc.decks, output_id, &Deck::output_id);
}

const Deck* find_deck(const Document& doc, std::string_view output_id)
{
    return util::find_by(doc.decks, output_id, &Deck::output_id);
}

Document make_document(std::string name, const std::vector<std::string>& output_ids)
{
    Document doc;
    doc.name = std::move(name);
    for (const auto& out : output_ids) {
        Deck deck;
        deck.output_id = out;
        deck.slides.push_back(empty_slide(doc));
        doc.decks.push_back(std::move(deck));
    }
    return doc;
}

void normalize(Document& doc)
{
    if (doc.next_id < 1) {
        doc.next_id = 1;
    }
    scan_ids(doc);
    for (auto& cue : doc.cues) {
        if (cue.id.empty()) {
            cue.id = alloc_id(doc, 'c');
        }
    }
    for (auto& deck : doc.decks) {
        for (auto& slide : deck.slides) {
            if (slide.id.empty()) {
                slide.id = alloc_id(doc, 's');
            }
            for (auto& obj : slide.objects) {
                if (obj.id.empty()) {
                    obj.id = alloc_id(doc, 'o');
                }
            }
            // The stacking commands need one dense back-to-front ``z`` order.
            normalize_object_order(slide);
        }
    }

    const int n = max_slide_count(doc);
    if (!doc.line_control) {
        for (auto& deck : doc.decks) {
            while (deck.slide_count() < n) {
                deck.slides.push_back(empty_slide(doc));
            }
            deck.line_to_slide = identity_map(deck.slide_count());
        }
    } else {
        const int cues = static_cast<int>(doc.cues.size());
        for (auto& deck : doc.decks) {
            if (deck.slides.empty()) {
                deck.slides.push_back(empty_slide(doc));
            }
            pad_map(deck, cues);
        }
    }

    tidy_section_starts(doc.cues);
    prune_cue_overlays(doc);
}

std::string document_refusal(const nlohmann::json& blob)
{
    if (blob.is_null() || (blob.is_object() && blob.empty())) {
        return {};
    }
    const auto schema = blob.is_object() ? blob.find("schema") : blob.end();
    const auto decks = blob.is_object() ? blob.find("decks") : blob.end();
    if (schema == blob.end() || !schema->is_string() || schema->get<std::string>() != k_schema
        || decks == blob.end() || !decks->is_array()) {
        return "It is not a Freeform document.";
    }
    const auto version = blob.find("schema_version");
    if (version == blob.end() || !version->is_number_integer()
        || version->get<std::int64_t>() != k_schema_version) {
        const std::string found = (version != blob.end() && version->is_number_integer())
                                      ? std::to_string(version->get<std::int64_t>())
                                      : std::string("none");
        return "It was saved by a different version of SeventhSlide (document version " + found
               + ", this version reads " + std::to_string(k_schema_version) + ").";
    }
    return {};
}

bool document_is_native(const nlohmann::json& blob)
{
    return blob.is_object() && !blob.empty() && document_refusal(blob).empty();
}

std::optional<Document> load_document(const nlohmann::json& blob, std::string_view name)
{
    if (blob.is_null() || (blob.is_object() && blob.empty())) {
        // No document yet: an item with no decks plays nothing until the
        // editor gives it one for the output being edited.
        Document doc = make_document(std::string(name), {});
        normalize(doc);
        return doc;
    }
    if (!document_is_native(blob)) {
        return std::nullopt;
    }
    Document doc = Document::from_json(blob);
    if (doc.name.empty()) {
        doc.name = std::string(name);
    }
    normalize(doc);
    return doc;
}

bool document_has_deck(const nlohmann::json& blob, std::string_view output_id)
{
    const auto* decks = json::find(blob, "decks");
    if (decks == nullptr || !decks->is_array()) {
        return false;
    }
    return std::any_of(decks->begin(), decks->end(), [output_id](const nlohmann::json& deck) {
        const auto* id = json::find(deck, "output");
        return id != nullptr && id->is_string() && id->get_ref<const std::string&>() == output_id;
    });
}

bool slide_counts_equal(const Document& doc) noexcept
{
    if (doc.decks.empty()) {
        return true;
    }
    const int n = doc.decks.front().slide_count();
    return std::all_of(doc.decks.begin(), doc.decks.end(),
                       [n](const Deck& d) { return d.slide_count() == n; });
}

bool can_disable_line_control(const Document& doc) noexcept
{
    return slide_counts_equal(doc);
}

} // namespace seventhslide::freeform
