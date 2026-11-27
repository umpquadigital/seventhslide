#pragma once

// The open library's destinations: what each output sends to, and the rules
// the list keeps - one destination per target, unique NDI source names, a
// port of its own for each new SRT stream, a title for each. Holds no
// windows or sinks (see OutputWindowHost).
// Seam: Qt Gui (screens are looked up to choose a target). Stores nothing;
// see library::DestinationStore.

#include "seventhslide/render/destination.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <nlohmann/json.hpp>

namespace seventhslide::render {

class DestinationRegistry {
public:
    [[nodiscard]] const QVector<Destination>& all() const noexcept { return destinations_; }
    /// The destinations, to change their fields; ``add`` and ``remove`` change
    /// the list.
    [[nodiscard]] QVector<Destination>& all() noexcept { return destinations_; }
    [[nodiscard]] Destination* find(const QString& dest_id);
    [[nodiscard]] const Destination* find(const QString& dest_id) const;

    /// The outputs' display names, by output id. False when nothing changed.
    bool set_output_names(QHash<QString, QString> names_by_id);
    /// The display name of ``output_id``; the id itself when none was given.
    [[nodiscard]] QString output_label(const QString& output_id) const;

    /// Add a disabled destination of ``kind`` to ``output_id``, pointed at a
    /// target of its kind nothing else uses, with a title of its own.
    Destination& add(const QString& output_id, DestinationKind kind);
    /// False when there is no ``dest_id``.
    bool remove(const QString& dest_id);
    /// Move ``dest_id`` past the next destination of its own output, up
    /// (``step`` < 0, nearer the top) or down. False when there is no such
    /// destination or none to pass.
    bool move(const QString& dest_id, int step);
    /// Remove every destination of ``output_id``; returns their ids.
    QStringList remove_output(const QString& output_id);

    /// Enabled destinations of ``kind`` on ``target_key`` other than
    /// ``keep_id``. A target (a screen, a camera device, a DeckLink
    /// sub-device) can serve one destination at a time.
    [[nodiscard]] QStringList others_on_target(DestinationKind kind,
                                               const QString& target_key,
                                               const QString& keep_id) const;
    /// The output of another enabled destination on ``d``'s target; empty
    /// when there is none.
    [[nodiscard]] QString conflict_output(const Destination& d) const;
    /// What the destination list calls each destination, in ``all()`` order:
    /// its title, or for one without a usable title ("", "0x40a0") the next
    /// free "Display 1", "Display 2", ... on its output.
    [[nodiscard]] QStringList titles() const;

    /// A camera device not already claimed by another enabled destination,
    /// SeventhSlide's own loopback first.
    [[nodiscard]] QString unused_camera_key() const;
    /// A DeckLink device not already claimed by another enabled destination.
    [[nodiscard]] QString unused_bmd_device() const;
    /// A port no SRT destination streams on yet, from
    /// ``stream::k_default_port`` up.
    [[nodiscard]] int unused_srt_port() const;
    /// A source name not already used by another NDI destination.
    [[nodiscard]] QString unused_ndi_name(const QString& output_id,
                                          const QString& exclude_id = {}) const;
    /// An NDI destination other than ``dest_id`` is already called ``name``
    /// (ignoring case). Two senders with one name collide on the network.
    [[nodiscard]] bool ndi_name_taken(const QString& name, const QString& dest_id) const;

    /// Every destination as a record naming its output's id in "output": what
    /// ``replace`` reads back.
    [[nodiscard]] nlohmann::json records() const;
    /// Swap in another library's destinations. A record of no known kind is
    /// skipped; one without a usable title gets one, as a new destination
    /// would. Anything but an array is no destinations.
    void replace(const nlohmann::json& records);

private:
    [[nodiscard]] QString unused_screen_key() const;
    /// Operator title for a new dest: "Display 1", "Display 2", "NDI 1", ...
    [[nodiscard]] QString unique_title(DestinationKind kind, const QString& output_id,
                                       const QString& exclude_id = {}) const;
    /// Point a new destination at a target of its kind nothing else uses.
    void choose_target(Destination& dest);

    QVector<Destination> destinations_;
    QHash<QString, QString> output_names_;  // output id -> display name
};

}  // namespace seventhslide::render
