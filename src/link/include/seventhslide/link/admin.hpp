#pragma once

// Admin work on a synced library: what the Sync page does with the admin
// password on an editor computer, and ``seventhslide --server admin LIB ...``
// at a server, each as a member of the library. Every entry it writes is signed by
// the server and, unlocked by the password for that one change and then
// forgotten, the admin key.
//
// A request names its ``action`` and what it needs:
//
//   invite    role ("editor"/"viewer"), days, password, and optionally the
//             library's name and the computer making it (from: machine,
//             key, addresses)        -> code, invite_id, expires
//   withdraw  invite, password         -> invite (its whole id)
//   role      computer, role, password, and optionally holds (whether a
//             viewer holds the admin key): what differs is changed
//   key       computer, holds, password
//   remove    computer, password
//   limit     bytes (0 for none), password
//   password  new_password, and password or recovery_code
//                                     -> recovery_code (when a new one was made)
//   recovery  password                -> recovery_code
//
// ``computer`` is a member's machine id, the start of one, or its name;
// ``invite`` the id of an invite waiting, or the start of one.
// The reply is ``{"ok": true, ...}``, or ``{"ok": false, "error": why}``
// in words for the person at the server.
//
// Seam: the sync module's member list.

#include "seventhslide/crypto/password_box.hpp"
#include "seventhslide/sync/identity.hpp"
#include "seventhslide/sync/member_list.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::link {

/// ``request`` done on ``library``, whose store is ``store``, by ``me``.
/// ``params`` lock a new admin key (tests lock quicker).
[[nodiscard]] nlohmann::json admin_work(sync::SyncStore& store, const sync::Identity& me,
                                        const std::string& library,
                                        const nlohmann::json& request,
                                        crypto::BoxParams params = crypto::k_box_params);

/// The current member ``who`` names (machine id, the start of one, or name,
/// any case), or why none does; ``ambiguous`` when more than one might.
struct Found {
    const sync::Member* member = nullptr;
    std::string error;
    bool ambiguous = false;
};
[[nodiscard]] Found find_computer(const sync::Members& list, std::string_view who);

/// The invite of ``list`` still to be used (``sync::Members::invites``) that
/// ``id`` names, whole or its start, or why none does. One run out by this
/// computer's clock is found too: another's clock may not agree, so it can
/// still be withdrawn.
struct FoundInvite {
    std::optional<sync::InviteMade> invite;
    std::string error;
    bool ambiguous = false;
};
[[nodiscard]] FoundInvite find_invite(const sync::Members& list, std::string_view id);

}  // namespace seventhslide::link
