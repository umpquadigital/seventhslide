#include "seventhslide/sync/change_file.hpp"

#include "json_read.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <stdexcept>

namespace seventhslide::sync {
namespace {

using nlohmann::json;

const JsonRead k_read("A sync change file");

}  // namespace

json change_to_json(const Change& c)
{
    json base = json::object();
    for (const auto& [field, version] : c.base) {
        base[field] = version ? db::version_to_json(*version) : json();
    }
    json change{{"table", c.table}, {"key", c.key}, {"base", std::move(base)}};
    if (c.deleted) {
        change["deleted"] = true;
        if (!c.referrers.empty()) {
            json referrers = json::array();
            for (const auto& r : c.referrers) {
                referrers.push_back(json{{"table", r.table},
                                         {"key", r.key},
                                         {"versions", db::versions_to_json(r.versions)}});
            }
            change["referrers"] = std::move(referrers);
        }
    } else {
        change["document"] = c.document;
        if (!c.before.empty()) {
            change["before"] = json(c.before);
        }
    }
    return change;
}

namespace {

Change read_change(const json& c)
{
    if (!c.is_object()) {
        k_read.malformed("a change is not an object");
    }
    Change change;
    change.table = k_read.text(c, "table");
    change.key = k_read.text(c, "key");
    change.deleted = k_read.optional_flag(c, "deleted", false);
    const auto& base = k_read.member(c, "base");
    if (!base.is_object()) {
        k_read.malformed("a change's base is not an object");
    }
    try {
        for (const auto& [field, version] : base.items()) {
            change.base.emplace(field, version.is_null()
                                           ? std::nullopt
                                           : std::optional(db::version_from_json(version)));
        }
    } catch (const std::invalid_argument& ex) {
        k_read.malformed(ex.what());
    }
    if (!change.deleted) {
        change.document = k_read.member(c, "document");
        if (!change.document.is_object()) {
            k_read.malformed("a change's document is not an object");
        }
        if (const auto it = c.find("before"); it != c.end()) {
            if (!it->is_object()) {
                k_read.malformed("a change's before is not an object");
            }
            for (const auto& [field, value] : it->items()) {
                change.before.emplace(field, value);
            }
        }
    } else if (const auto it = c.find("referrers"); it != c.end()) {
        if (!it->is_array()) {
            k_read.malformed("a delete's referrers are not a list");
        }
        for (const auto& r : *it) {
            Referrer referrer{k_read.text(r, "table"), k_read.text(r, "key"), {}};
            try {
                referrer.versions = db::versions_from_json(k_read.member(r, "versions"));
            } catch (const std::invalid_argument& ex) {
                k_read.malformed(ex.what());
            }
            change.referrers.push_back(std::move(referrer));
        }
    }
    return change;
}

}  // namespace

Change change_from_json(const json& c)
{
    return k_read.whole(c, read_change);
}

json change_file_to_json(const ChangeFile& file)
{
    json seen = json::object();
    for (const auto& [machine, sequence] : file.seen) {
        seen[machine] = sequence;
    }
    json media = json::array();
    for (const auto& m : file.media) {
        media.push_back(db::media_digest_to_json(m));
    }
    json changes = json::array();
    for (const auto& c : file.changes) {
        changes.push_back(change_to_json(c));
    }
    return json{{"format", file.format},     {"schema", file.schema},
                {"library", file.library},
                {"machine", file.machine},   {"computer", file.computer},
                {"time", file.time},         {"sequence", file.sequence},
                {"counter", file.counter},   {"seen", std::move(seen)},
                {"media", std::move(media)}, {"changes", std::move(changes)}};
}


namespace {

/// What every format of change file has as format 1 has it
/// (``change_file_label``).
ChangeFile read_label(const json& j)
{
    if (!j.is_object()) {
        k_read.malformed("not a JSON object");
    }
    ChangeFile file;
    file.format = k_read.format(j);
    file.schema = k_read.schema(j);
    file.library = k_read.text(j, "library");
    file.machine = k_read.text(j, "machine");
    if (!is_machine_id(file.machine)) {
        k_read.malformed("machine is not a machine id");
    }
    file.computer = k_read.text(j, "computer");
    file.time = k_read.text(j, "time");
    file.sequence = k_read.number(j, "sequence");
    const auto& seen = k_read.member(j, "seen");
    if (!seen.is_object()) {
        k_read.malformed("seen is not an object");
    }
    for (const auto& [machine, sequence] : seen.items()) {
        if (!is_machine_id(machine) || !sequence.is_number_integer()
            || sequence.get<std::int64_t>() < 0) {
            k_read.malformed("seen holds something but computers' file numbers");
        }
        file.seen.emplace(machine, sequence.get<std::int64_t>());
    }
    file.media = k_read.media(j);
    return file;
}

ChangeFile read_change_file(const json& j)
{
    if (j.is_object()) {
        // Newer is said first: what a later format holds may not read here.
        refuse_newer(k_read, j, k_change_format);
    }
    ChangeFile file = read_label(j);
    file.counter = k_read.counter(j, "counter");
    const auto& changes = k_read.member(j, "changes");
    if (!changes.is_array()) {
        k_read.malformed("changes is not a list");
    }
    for (const auto& c : changes) {
        file.changes.push_back(read_change(c));
    }
    return file;
}

}  // namespace

ChangeFile change_file_from_json(const json& j)
{
    return k_read.whole(j, read_change_file);
}

ChangeFile change_file_label(const json& j)
{
    return k_read.whole(j, read_label);
}

}  // namespace seventhslide::sync
