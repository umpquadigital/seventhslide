#include "seventhslide/sync/shared_log.hpp"

#include "json_read.hpp"

#include "seventhslide/sync/envelope.hpp"
#include "seventhslide/sync/now_utc.hpp"
#include "seventhslide/util/strings.hpp"
#include "seventhslide/util/text_encoding.hpp"

#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace seventhslide::sync {
namespace {

using nlohmann::json;

constexpr std::string_view k_what = "A shared log entry";

json item_to_json(const LogItem& item)
{
    json out{{"kind", item.kind}, {"key", item.key}, {"title", item.title}, {"action", item.action}};
    if (!item.fields.empty()) {
        out["fields"] = item.fields;
    }
    return out;
}

LogItem item_from_json(const JsonRead& r, const json& j)
{
    if (!j.is_object()) {
        r.malformed("an item is not an object");
    }
    LogItem item{r.text(j, "kind"), r.text(j, "key"), r.optional_text(j, "title"), r.text(j, "action"), {}};
    if (const auto fields = j.find("fields"); fields != j.end()) {
        if (!fields->is_array()) {
            r.malformed("fields is not a list");
        }
        for (const auto& f : *fields) {
            if (!f.is_string()) {
                r.malformed("a field is not text");
            }
            item.fields.push_back(f.get<std::string>());
        }
    }
    return item;
}

LogCounts counts_from_json(const JsonRead& r, const json& j)
{
    LogCounts out;
    const auto it = j.find("counts");
    if (it == j.end()) {
        return out;
    }
    if (!it->is_object()) {
        r.malformed("counts is not an object");
    }
    for (const auto& [kind, actions] : it->items()) {
        if (!actions.is_object()) {
            r.malformed("counts is not an object of objects");
        }
        for (const auto& [action, n] : actions.items()) {
            if (!n.is_number_integer() || n.get<std::int64_t>() < 0) {
                r.malformed("a count is not a whole number");
            }
            out[kind][action] = static_cast<int>(std::min<std::int64_t>(n.get<std::int64_t>(), 1 << 30));
        }
    }
    return out;
}

}  // namespace

json log_entry_to_json(const LogEntry& entry, std::string_view library)
{
    json out{{"format", k_log_format},
             {"library", std::string(library)},
             {"machine", entry.machine},
             {"sequence", entry.sequence},
             {"order", entry.order},
             {"time", entry.time},
             {"kind", entry.kind}};
    if (entry.kind == k_log_message) {
        out["text"] = entry.text;
    } else if (entry.kind == k_log_changes) {
        json items = json::array();
        for (const auto& item : entry.items) {
            items.push_back(item_to_json(item));
        }
        out["items"] = std::move(items);
        out["counts"] = entry.counts;
    }
    return out;
}

ReadLogEntry read_log_entry(std::string_view text)
{
    const auto envelope = unseal(text, k_what);
    const auto* signer = envelope.signed_as(k_signed_as_computer);
    const JsonRead r{std::string(k_what)};
    if (signer == nullptr) {
        r.malformed("it is not signed by a computer");
    }
    return r.whole(envelope.body, [&](const json& b) {
        if (!b.is_object()) {
            r.malformed("it is not an object");
        }
        ReadLogEntry out;
        out.signer = *signer;
        out.format = static_cast<int>(r.number(b, "format"));
        out.library = r.text(b, "library");
        auto& e = out.entry;
        e.machine = r.text(b, "machine");
        e.sequence = r.number(b, "sequence");
        e.order = r.counter(b, "order");
        e.time = r.optional_text(b, "time");
        e.kind = r.text(b, "kind");
        if (!is_machine_id(e.machine) || e.sequence < 1) {
            r.malformed("it does not say whose it is");
        }
        if (out.format > k_log_format) {
            return out;  // its label alone: a later SeventhSlide's
        }
        if (e.kind == k_log_message) {
            e.text = r.text(b, "text");
        } else if (e.kind == k_log_changes) {
            if (const auto items = b.find("items"); items != b.end()) {
                if (!items->is_array()) {
                    r.malformed("items is not a list");
                }
                for (const auto& item : *items) {
                    e.items.push_back(item_from_json(r, item));
                }
            }
            e.counts = counts_from_json(r, b);
        }
        return out;
    });
}

LogBook::LogBook(SyncStore& store, Identity me, std::string library)
    : store_(store), me_(std::move(me)), library_(std::move(library))
{
}

LogEntry LogBook::post_message(std::string_view text)
{
    auto trimmed = util::trim(text);
    if (trimmed.empty()) {
        throw std::invalid_argument("A message says something.");
    }
    if (trimmed.size() > k_longest_message || !util::is_valid_utf8(trimmed)) {
        throw std::invalid_argument("That message is too long to send.");
    }
    LogEntry entry;
    entry.kind = std::string(k_log_message);
    entry.text = std::move(trimmed);
    return write(std::move(entry));
}

LogEntry LogBook::post_changes(std::vector<LogItem> items, LogCounts counts)
{
    if (items.size() > k_most_log_items) {
        items.resize(k_most_log_items);
    }
    LogEntry entry;
    entry.kind = std::string(k_log_changes);
    entry.items = std::move(items);
    entry.counts = std::move(counts);
    return write(std::move(entry));
}

LogEntry LogBook::write(LogEntry entry)
{
    entry.machine = me_.machine;
    entry.time = now_utc();
    // The last number found taken: the next write takes one past it, though
    // the listing has not caught up.
    std::int64_t taken = 0;
    for (;;) {
        // One past every computer's last: each computer's entries rise in
        // order with their numbers, so its last is its highest.
        std::int64_t order = 0;
        std::int64_t sequence = 0;
        for (const auto& machine : store_.machines(Numbered::Log)) {
            const auto numbers = store_.sequences(Numbered::Log, machine);
            if (numbers.empty()) {
                continue;
            }
            if (machine == me_.machine) {
                sequence = numbers.back();
            }
            try {
                order = std::max(order, read_log_entry(store_.read(Numbered::Log, machine, numbers.back())).entry.order);
            } catch (const std::exception&) {
                // unreadable here: it orders nothing
            }
        }
        entry.sequence = std::max(sequence, taken) + 1;
        entry.order = order + 1;
        try {
            store_.write(Numbered::Log, me_.machine, entry.sequence,
                         seal(log_entry_to_json(entry, library_), {{k_signed_as_computer, &me_.key}}));
            return entry;
        } catch (const AlreadyThere&) {
            // Its number taken meanwhile by another write of this computer's
            // (the library's thread noting a change file as a message goes):
            // the next, however often. Any other failure is the caller's.
            taken = entry.sequence;
        }
    }
}

std::vector<LogEntry> LogReader::read_new(SyncStore& store)
{
    std::vector<LogEntry> out;
    for (const auto& machine : store.machines(Numbered::Log)) {
        for (const auto n : store.sequences(Numbered::Log, machine)) {
            if (!read_.insert({machine, n}).second) {
                continue;
            }
            try {
                auto read = read_log_entry(store.read(Numbered::Log, machine, n));
                if (read.library == library_ && read.entry.machine == machine && read.entry.sequence == n) {
                    out.push_back(std::move(read.entry));
                }
            } catch (const std::exception&) {
                // let be: the gate let in only whole entries, so this one was
                // damaged here
            }
        }
    }
    std::ranges::sort(out, log_before);
    return out;
}

bool log_before(const LogEntry& a, const LogEntry& b)
{
    return std::tie(a.order, a.time, a.machine, a.sequence) < std::tie(b.order, b.time, b.machine, b.sequence);
}

}  // namespace seventhslide::sync
