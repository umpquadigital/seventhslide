#include "profile_controller.hpp"

#include "entry_error.hpp"
#include "refusals.hpp"
#include "qml_ids.hpp"

#include <QString>
#include <QVariantMap>

#include <cstdint>
#include <exception>

namespace seventhslide::app {

ProfileController::ProfileController(OpenLibrary& library, QObject* parent)
    : QObject(parent)
    , library_(library)
{
}

QVariantList ProfileController::styleProfiles() const
{
    QVariantList out;
    auto* repo = library_.profile_repository();
    if (repo == nullptr) {
        return out;
    }
    const auto active = activeProfileId();
    for (const auto& profile : repo->list_summaries()) {
        if (!profile.id) {
            continue;
        }
        QVariantMap row;
        const auto id = qml_id(profile.id);
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("name"), QString::fromStdString(profile.name));
        row.insert(QStringLiteral("active"), id == active);
        out.push_back(row);
    }
    return out;
}

qint64 ProfileController::activeProfileId() const
{
    auto* service = library_.profile_service();
    if (service == nullptr) {
        return -1;
    }
    const auto id = service->active_profile_id();
    return qml_id(id);
}

QString ProfileController::activeProfileName() const
{
    auto* repo = library_.profile_repository();
    const auto active = activeProfileId();
    if (repo == nullptr || active < 0) {
        return {};
    }
    for (const auto& profile : repo->list_summaries()) {
        if (qml_id(profile.id) == active) {
            return QString::fromStdString(profile.name);
        }
    }
    return {};
}

void ProfileController::capture_active_profile()
{
    auto* service = library_.profile_service();
    // A library this computer only views keeps what its editors wrote.
    if (service == nullptr || library_.read_only()) {
        return;
    }
    try {
        service->capture_active();
    } catch (const std::exception& ex) {
        // Never block a shutdown / library switch on a profile write.
        library_.set_last_error(report_entry_error(__func__, ex));
    }
}

QString ProfileController::activateProfile(qint64 id)
try {
    auto* service = library_.profile_service();
    if (service == nullptr) {
        return refusal::no_library();
    }
    if (id < 0) {
        return refusal::profile_gone();
    }
    if (activeProfileId() == id) {
        return {};
    }
    if (!service->activate(id)) {
        return refusal::profile_gone();
    }
    emit profileSwitched();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString ProfileController::createProfile(const QString& name)
try {
    auto* service = library_.profile_service();
    if (service == nullptr) {
        return refusal::no_library();
    }
    const auto clean = name.trimmed();
    if (clean.isEmpty()) {
        return refusal::name_required();
    }
    (void)service->create_blank(clean.toStdString());
    emit profilesChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString ProfileController::duplicateProfile(qint64 id, const QString& name)
try {
    auto* service = library_.profile_service();
    auto* repo = library_.profile_repository();
    if (service == nullptr || repo == nullptr) {
        return refusal::no_library();
    }
    if (id < 0) {
        return refusal::profile_gone();
    }
    auto clean = name.trimmed();
    if (clean.isEmpty()) {
        const auto source = repo->get(id);
        if (!source) {
            return refusal::profile_gone();
        }
        clean = tr("%1 copy").arg(QString::fromStdString(source->name));
    }
    if (!service->duplicate(id, clean.toStdString())) {
        return refusal::profile_gone();
    }
    emit profilesChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString ProfileController::renameProfile(qint64 id, const QString& name)
try {
    auto* repo = library_.profile_repository();
    if (repo == nullptr) {
        return refusal::no_library();
    }
    const auto clean = name.trimmed();
    if (clean.isEmpty()) {
        return refusal::name_required();
    }
    if (id < 0 || !repo->get(id)) {
        return refusal::profile_gone();
    }
    repo->rename(id, clean.toStdString());
    emit profilesChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString ProfileController::deleteProfile(qint64 id)
try {
    auto* service = library_.profile_service();
    auto* repo = library_.profile_repository();
    if (service == nullptr || repo == nullptr) {
        return refusal::no_library();
    }
    if (id < 0 || !repo->get(id)) {
        return refusal::profile_gone();
    }
    if (repo->list_summaries().size() <= 1) {
        return tr("The last profile cannot be deleted.");
    }
    const bool was_active = activeProfileId() == id;
    if (!service->remove(id)) {
        return tr("That profile could not be deleted.");
    }
    if (was_active) {
        // remove() activated a survivor, so live assignments have changed.
        emit profileSwitched();
    } else {
        emit profilesChanged();
    }
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

} // namespace seventhslide::app
