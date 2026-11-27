#pragma once

// Reading the Freeform editor's list models the way QML does, through roles.

#include "freeform_editor_models.hpp"

#include <QString>

namespace seventhslide::test {

/// The thumbnail URL of slide ``slide_index``'s row, or empty if it has none.
[[nodiscard]] inline QString slide_thumb_url(const app::FreeformSlideListModel& model,
                                             int slide_index)
{
    using Model = app::FreeformSlideListModel;
    for (int row = 0; row < model.rowCount(); ++row) {
        const auto index = model.index(row);
        if (model.data(index, Model::KindRole).toString() == QStringLiteral("slide")
            && model.data(index, Model::IndexRole).toInt() == slide_index) {
            return model.data(index, Model::ThumbUrlRole).toString();
        }
    }
    return {};
}

}  // namespace seventhslide::test
