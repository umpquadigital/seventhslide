#pragma once

// SeventhSlide's own updates: the releases published on GitHub, and which of
// them to offer this copy of the app.
//
// The app asks GitHub for the repository's releases (``GET
// /repos/<owner>/<repo>/releases``) and hands the reply to
// ``parse_releases``. A version is ``MAJOR.MINOR.PATCH``, with a label after
// a dash for a beta (``1.3.0-beta.2``), and its tag is the version with a
// ``v`` in front. Versions are ordered as Semantic Versioning 2.0 orders
// them, so ``1.3.0-beta.2`` comes after ``1.3.0-beta.1`` and before
// ``1.3.0``.
//
// Nothing from the reply reaches the user unchecked. Every link kept points
// under the repository's own releases on github.com (``is_release_url``),
// and the release notes are shown as plain text (``notes_text``), never as
// Markdown or HTML. Each file keeps the SHA-256 digest GitHub reports for
// it, which a download must match before it is used.
//
// Nothing here opens a connection: the app does, with these.
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::update {

/// A version, as Semantic Versioning 2.0 writes it.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    /// The label after the dash, split at its dots: ``{"beta", "2"}``.
    /// Empty for a final version.
    std::vector<std::string> label;

    /// A beta, or any other version with a label.
    [[nodiscard]] bool is_beta() const noexcept { return !label.empty(); }
    /// ``1.3.0`` or ``1.3.0-beta.2``, without the ``v``.
    [[nodiscard]] std::string text() const;

    [[nodiscard]] bool operator==(const Version&) const = default;
};

/// Semantic Versioning's order. Build metadata (after a ``+``) is not kept,
/// so it plays no part.
[[nodiscard]] std::strong_ordering operator<=>(const Version& a, const Version& b);

/// ``1.3.0``, ``v1.3.0`` or ``1.3.0-beta.2``; nullopt for anything else,
/// including a number with a leading zero. Build metadata is accepted and
/// dropped.
[[nodiscard]] std::optional<Version> parse_version(std::string_view text);

/// The repository the releases come from.
struct Repository {
    std::string owner;
    std::string name;
};

/// One file of a release.
struct ReleaseFile {
    std::string name;
    /// Its download link, under the repository's releases.
    std::string url;
    std::int64_t size = 0;
    /// Lower-case hex SHA-256 that GitHub reports for the file; empty when
    /// it reported none, and then the file is not downloaded by the app.
    std::string sha256;

    [[nodiscard]] bool operator==(const ReleaseFile&) const = default;
};

/// One published release.
struct Release {
    Version version;
    std::string tag;
    /// The release's title on GitHub; the tag when it has none.
    std::string title;
    /// GitHub marks it as a beta ("prerelease" in its reply), or its
    /// version has a label.
    bool beta = false;
    /// Its page on GitHub, under the repository's releases.
    std::string page_url;
    /// The release notes, as written (Markdown). Shown through
    /// ``notes_text``.
    std::string notes;
    /// When it was published, as GitHub writes it (``2026-11-27T18:04:11Z``).
    std::string published_at;
    std::vector<ReleaseFile> files;

    /// The file called ``name``, or nullptr.
    [[nodiscard]] const ReleaseFile* file(std::string_view name) const;

    [[nodiscard]] bool operator==(const Release&) const = default;
};

/// Whether ``url`` is a page or a file of ``repo``'s releases:
/// ``https://github.com/<owner>/<repo>/releases/...``, with no query,
/// fragment, port, user name, ``..`` or other characters a release's tag
/// and file names never hold. The owner and repository match in any case,
/// as GitHub matches them.
[[nodiscard]] bool is_release_url(std::string_view url, const Repository& repo);

/// The releases in GitHub's reply to ``/repos/<owner>/<repo>/releases``,
/// newest version first. Left out: drafts, a release whose tag is not a
/// version, and one whose page is not under ``repo``'s releases. A file
/// whose link is not under them, or whose upload did not finish, is left
/// out of its release. Nullopt when the reply is not a list of releases.
[[nodiscard]] std::optional<std::vector<Release>> parse_releases(std::string_view json_text,
                                                                 const Repository& repo);

/// Which releases are offered.
enum class Channel {
    /// Final versions only.
    stable,
    /// Betas as well.
    beta,
};

/// The channel to use until the user picks one: beta for a beta, so a beta
/// tester hears of the next beta, and stable otherwise.
[[nodiscard]] Channel default_channel(const Version& running) noexcept;

/// The newest of ``releases`` that ``channel`` offers, or nullptr.
[[nodiscard]] const Release* newest_release(const std::vector<Release>& releases,
                                            Channel channel);

/// The release to tell the user about: the newest that ``channel`` offers,
/// when it is newer than ``running`` and is not the version the user
/// chose to skip. Nullptr otherwise. A version newer than the skipped one is
/// offered again.
[[nodiscard]] const Release* release_to_offer(const std::vector<Release>& releases,
                                              Channel channel,
                                              const Version& running,
                                              const std::optional<Version>& skipped);

/// How a release file is installed, by its kind.
enum class Installer {
    /// Not a kind the app installs.
    none,
    /// The Windows installer (``.exe``), opened as the user would open it.
    windows_setup,
    /// The macOS disk image (``.dmg``), opened in the Finder.
    disk_image,
    /// A ``.deb``, installed by apt-get as root.
    deb,
    /// An ``.rpm``, installed by dnf as root.
    rpm,
};

/// The installer for ``file_name``, by its extension.
[[nodiscard]] Installer installer_for(std::string_view file_name) noexcept;

/// The command that installs the package at ``path`` as root, through
/// pkexec (which asks for an administrator's password): apt-get for a deb,
/// dnf for an rpm. ``path`` must be absolute, so neither reads it as one of
/// its options. Empty for any other installer, or a relative path.
[[nodiscard]] std::vector<std::string> install_command(Installer installer, std::string_view path);

/// Release notes as plain text: headings, emphasis, code marks, pictures and
/// HTML comments are taken out, a link keeps only its text, and a list item
/// starts with a bullet. Runs of blank lines become one.
[[nodiscard]] std::string notes_text(std::string_view markdown);

} // namespace seventhslide::update
