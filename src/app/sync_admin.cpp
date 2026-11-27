#include "sync_admin.hpp"

#include "sync_controller.hpp"

#include <seventhslide/link/library_sync.hpp>
#include <seventhslide/link/sync_node.hpp>
#include <seventhslide/sync/admin_key.hpp>
#include <seventhslide/sync/member_list.hpp>

#include <cmath>
#include <cstdint>
#include <exception>

namespace seventhslide::app {
namespace {

using nlohmann::json;

/// The largest size limit, in gigabytes: a petabyte.
constexpr double k_most_gigabytes = 1e6;

}  // namespace

SyncAdmin::SyncAdmin(SyncController& sync) : QObject(&sync), sync_(sync) {}

QString SyncAdmin::run(const QString& action, json request)
{
    auto* open = sync_.open_sync();
    if (open == nullptr) {
        return tr("The open library is not synced.");
    }
    sync_.began();
    open->admin(std::move(request), [this, action](json reply) { sync_.done(action, reply); });
    return {};
}

QString SyncAdmin::invite(const QString& role, int days, const QString& password)
{
    const auto& me = sync_.node().identity();
    return run(QStringLiteral("invite"),
               {{"action", "invite"},
                 {"role", role.toStdString()},
                 {"days", days},
                 {"password", password.toStdString()},
                 {"name", sync_.open_name()},
                 {"from",
                  {{"machine", me.machine},
                   {"key", me.key.public_key().base64()},
                   {"addresses", sync_.node().addresses()}}}});
}

QString SyncAdmin::withdrawInvite(const QString& invite, const QString& password)
{
    return run(QStringLiteral("withdraw"), {{"action", "withdraw"},
                                            {"invite", invite.toStdString()},
                                            {"password", password.toStdString()}});
}

QString SyncAdmin::setRole(const QString& machine, const QString& role, bool holdsAdminKey, const QString& password)
{
    return run(QStringLiteral("role"), {{"action", "role"},
                                        {"computer", machine.toStdString()},
                                        {"role", role.toStdString()},
                                        {"holds", holdsAdminKey},
                                        {"password", password.toStdString()}});
}

QString SyncAdmin::removeComputer(const QString& machine, const QString& password)
{
    return run(QStringLiteral("remove"), {{"action", "remove"},
                                          {"computer", machine.toStdString()},
                                          {"password", password.toStdString()}});
}

QString SyncAdmin::setSizeLimit(double gigabytes, const QString& password)
{
    if (!std::isfinite(gigabytes) || gigabytes < 0 || gigabytes > k_most_gigabytes) {
        return tr("A size limit is a number of gigabytes, up to a million, or 0 for none.");
    }
    return run(QStringLiteral("limit"), {{"action", "limit"},
                                         {"bytes", static_cast<std::int64_t>(std::llround(gigabytes * 1e9))},
                                         {"password", password.toStdString()}});
}

QString SyncAdmin::changePassword(const QString& currentOrCode, const QString& fresh, bool withRecoveryCode)
{
    if (const auto problem = sync::passphrase_problem(fresh.toStdString())) {
        return QString::fromStdString(*problem);
    }
    json request{{"action", "password"}, {"new_password", fresh.toStdString()}};
    request[withRecoveryCode ? "recovery_code" : "password"] = currentOrCode.toStdString();
    return run(QStringLiteral("password"), std::move(request));
}

QString SyncAdmin::newRecoveryCode(const QString& password)
{
    return run(QStringLiteral("recovery"), {{"action", "recovery"}, {"password", password.toStdString()}});
}

}  // namespace seventhslide::app
