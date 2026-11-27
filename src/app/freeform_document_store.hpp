#pragma once

// Where the Freeform editor reads the document it opens and writes it back.
//
// FreeformLibraryController implements it over the library database. It is
// an interface so the editor runs, and is tested, without a database: the
// rest of what the editor needs (the outputs, the preview resources) are
// plain objects it borrows.
//
// Seam: Qt Core. Call on the GUI thread.

#include <seventhslide/freeform/document.hpp>

#include <QString>

#include <cstdint>
#include <optional>
#include <string>

namespace seventhslide::app {

class IFreeformDocumentStore {
public:
    IFreeformDocumentStore() = default;
    virtual ~IFreeformDocumentStore() = default;
    IFreeformDocumentStore(const IFreeformDocumentStore&) = delete;
    IFreeformDocumentStore& operator=(const IFreeformDocumentStore&) = delete;
    IFreeformDocumentStore(IFreeformDocumentStore&&) = delete;
    IFreeformDocumentStore& operator=(IFreeformDocumentStore&&) = delete;

    /// The stored document for ``id``. nullopt when there is no such item, or
    /// when its document cannot be read (the store reports why).
    [[nodiscard]] virtual std::optional<freeform::Document> load_freeform_document(
        std::int64_t id) = 0;
    /// Write ``doc`` back to item ``id``, with what follows from that: its
    /// thumbnails, the library listing, and stacks already playing the item.
    /// Empty on success, else a message for the operator.
    [[nodiscard]] virtual QString commit_freeform_document(std::int64_t id,
                                                           const freeform::Document& doc) = 0;
    /// The owner of item ``id``'s own pictures and videos
    /// (freeform/own_media.hpp): where a file brought into it goes. Empty
    /// when it has none, as with no library.
    [[nodiscard]] virtual std::string freeform_media_owner(std::int64_t id)
    {
        (void)id;
        return {};
    }
    /// The editor is done with item ``id``, saved or not: the files brought
    /// into it that no document names go.
    virtual void freeform_document_closed(std::int64_t id) { (void)id; }
};

}  // namespace seventhslide::app
