#pragma once

// SessionFacade with every controller it holds and the repositories behind
// them. The facade's header only names the controllers; tests reach into all
// of them.

#include "destinations_controller.hpp"
#include "freeform_library_controller.hpp"
#include "web_library_controller.hpp"
#include "library_controller.hpp"
#include "library_registry_controller.hpp"
#include "line_map_model.hpp"
#include "live_controller.hpp"
#include "media_controller.hpp"
#include "mixer_controller.hpp"
#include "output_list_model.hpp"
#include "output_preview_controller.hpp"
#include "output_settings_controller.hpp"
#include "playback_controller.hpp"
#include "profile_controller.hpp"
#include "service_item_model.hpp"
#include "session_facade.hpp"
#include "stack_controller.hpp"
#include "side_view.hpp"
#include "theme_controller.hpp"
#include "slide_edit_session.hpp"

#include <seventhslide/db/bible_repository.hpp>
#include <seventhslide/db/database.hpp>
#include <seventhslide/db/freeform_repository.hpp>
#include <seventhslide/db/media_repository.hpp>
#include <seventhslide/db/service_repository.hpp>
#include <seventhslide/db/song_repository.hpp>
#include <seventhslide/library/library.hpp>

#include <nlohmann/json.hpp>
