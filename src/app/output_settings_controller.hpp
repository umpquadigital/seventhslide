#pragma once

// Create, rename, resize, reorder, switch on and off, and delete outputs.
//
// The outputs are the session's; each command edits them in place, saves them
// through the hook the application gives (set_save_outputs) and emits
// outputsEdited, on which the application rebuilds. A deleted output's windows,
// stored destinations and theme files go with it, once the outputs without it
// are saved. Whether an output is
// on is this computer's alone (library::DestinationStore). SessionFacade owns one
// and exposes it as `session.outputSettings`.

#include "open_library.hpp"

#include <seventhslide/model/output_config.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace seventhslide::render {
class OutputWindowHost;
}

namespace seventhslide::app {

class OutputSettingsController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

public:
    OutputSettingsController(session::Session& session,
                             OpenLibrary& library,
                             QObject* parent = nullptr);

    /// The windows and sinks outputs paint to; null detaches (tests, headless).
    void set_output_host(render::OutputWindowHost* host) { windows_ = host; }
    /// Saves the session's outputs, as they are now, to the open library;
    /// throws when it cannot. Every edit saves through it before
    /// ``outputsEdited``.
    void set_save_outputs(std::function<void()> save) { save_outputs_ = std::move(save); }

    /// The General page's fields: {id, name, canvasWidth, canvasHeight,
    /// enabledStackIds, disabledContentTypes, exemptFromGlobalBlank, exemptFromGlobalFreeze, icon,
    /// destinations}. Any output may be deleted, the last one too.
    Q_INVOKABLE [[nodiscard]] QVariantMap getOutputSettings(const QString& output_name);
    /// Save the General fields given in ``fields`` (named as getOutputSettings
    /// names them; any left out keep their values). Returns {name} or {error};
    /// a new name may be made unique. A changed canvas carries every text
    /// theme of the output and every Freeform deck keyed to it onto the new
    /// canvas, once, saved with the outputs in one transaction; the result then
    /// says so in ``fitted`` ("3 themes and 12 Freeform items were fitted to
    /// 1080 × 1920"), as does canvasFitted.
    Q_INVOKABLE [[nodiscard]] QVariantMap updateOutputSettings(const QString& output_name,
                                                               const QVariantMap& fields);
    /// The icons an output may have, in picker order.
    Q_INVOKABLE [[nodiscard]] QStringList iconIds() const;
    /// The canvas sizes offered by name, by shape in menu order:
    /// [{heading, presets: [{name, title, width, height}]}] (geom::k_canvas_presets).
    Q_INVOKABLE [[nodiscard]] QVariantList canvasPresetGroups() const;
    /// What the General page says of a canvas size: {preset (the title of
    /// the preset it is, or empty), aspect (its shape: "16:9", "2.37:1"; empty
    /// for no size), problem (why it cannot be saved, or empty)}.
    Q_INVOKABLE [[nodiscard]] QVariantMap describeCanvas(int width, int height) const;
    /// Create a seeded output; returns {name} or {error}.
    Q_INVOKABLE [[nodiscard]] QVariantMap createOutput(const QString& name = QString());
    /// Delete output (not the last). Empty = ok.
    Q_INVOKABLE [[nodiscard]] QString deleteOutput(const QString& output_name);
    /// Absolute reorder by names (must list every output once). Empty = ok.
    Q_INVOKABLE [[nodiscard]] QString reorderOutputs(const QVariantList& ordered_names);
    /// Switch an output on or off on this computer. Off, it keeps its
    /// settings, themes and destinations, and is left out of the show: nothing
    /// is built or painted for it. Not the last one on. Empty = ok.
    Q_INVOKABLE [[nodiscard]] QString setOutputOn(const QString& output_name, bool on);

signals:
    void errorOccurred(const QString& message);
    /// The output list was edited in place and saved: the application
    /// rebuilds what is live.
    void outputsEdited();
    /// An output was switched on or off (the session has rebuilt): the
    /// application opens or closes what it paints to.
    void outputsSwitched();
    /// An output changed canvas and its themes and Freeform decks were carried
    /// over: ``message`` says how many (updateOutputSettings's ``fitted``).
    void canvasFitted(const QString& message);

private:
    /// Carry what is laid out on ``outputs[index]`` onto its new canvas and
    /// save it with the outputs. The message for the operator, or empty when
    /// nothing moved.
    [[nodiscard]] QString fit_to_canvas(std::vector<model::OutputConfig>& outputs,
                                        std::size_t index);
    /// Save the outputs through ``save_outputs_``; throws when that fails.
    void save_outputs() const;

    session::Session& session_;
    OpenLibrary& library_;
    render::OutputWindowHost* windows_ = nullptr;
    std::function<void()> save_outputs_;
};

} // namespace seventhslide::app
