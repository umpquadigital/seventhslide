// SeventhSlide's own updates: versions, GitHub's list of releases, which
// release is offered, the links that are trusted, and release notes as plain
// text. fixtures/updates/releases.json is a reply as GitHub writes it.

#include <catch2/catch_test_macros.hpp>

#include <seventhslide/update/releases.hpp>
#include <seventhslide/util/read_file.hpp>
#include <seventhslide/version.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace seventhslide::update;

namespace {

const Repository k_repo{"umpquadigital", "seventhslide"};

Version v(std::string_view text)
{
    const auto parsed = parse_version(text);
    REQUIRE(parsed);
    return *parsed;
}

std::vector<Release> fixture_releases()
{
    const auto text = seventhslide::util::read_file(std::filesystem::path(SEVENTHSLIDE_FIXTURES_DIR)
                                                    / "updates" / "releases.json");
    REQUIRE(text);
    auto releases = parse_releases(*text, k_repo);
    REQUIRE(releases);
    return *releases;
}

Release release(std::string_view version, bool beta = false)
{
    Release r;
    r.version = v(version);
    r.tag = "v" + std::string(version);
    r.beta = beta || r.version.is_beta();
    return r;
}

} // namespace

TEST_CASE("A version reads with or without its v, and with a label", "[update]")
{
    CHECK(v("1.3.0") == Version{1, 3, 0, {}});
    CHECK(v("v1.3.0") == Version{1, 3, 0, {}});
    CHECK(v("V0.0.12") == Version{0, 0, 12, {}});
    CHECK(v("v1.3.0-beta.2") == Version{1, 3, 0, {"beta", "2"}});
    CHECK(v("1.3.0-rc-1") == Version{1, 3, 0, {"rc-1"}});
    CHECK(v("1.3.0+build.7") == Version{1, 3, 0, {}});
    CHECK(v("1.3.0-beta.2+20261127") == Version{1, 3, 0, {"beta", "2"}});

    CHECK(v("v1.3.0-beta.2").text() == "1.3.0-beta.2");
    CHECK(v("v1.3.0").text() == "1.3.0");
    CHECK(v("1.3.0").is_beta() == false);
    CHECK(v("1.3.0-beta.1").is_beta());
}

TEST_CASE("The app's own version is one the updater reads", "[update]")
{
    const auto own = parse_version(seventhslide::version_string());
    REQUIRE(own);
    CHECK(own->text() == seventhslide::version_string());
}

TEST_CASE("Anything else is not a version", "[update]")
{
    for (const char* text : {"", "v", "1", "1.3", "1.3.0.1", "01.3.0", "1.03.0", "1.3.00",
                             "1.3.0-", "1.3.0-beta..1", "1.3.0-beta.01", "1.3.0-beta_1",
                             "1.3.0+", "1.3.0+a..b", " 1.3.0", "1.3.0 ", "nightly", "1.-3.0",
                             "1.3.x", "99999999999.0.0", "vv1.3.0"}) {
        INFO(text);
        CHECK_FALSE(parse_version(text));
    }
}

TEST_CASE("Versions are ordered as Semantic Versioning orders them", "[update]")
{
    // Each one before the next, from the example in the specification.
    const std::vector<Version> ordered = {
        v("1.0.0-alpha"),      v("1.0.0-alpha.1"), v("1.0.0-alpha.beta"), v("1.0.0-beta"),
        v("1.0.0-beta.2"),     v("1.0.0-beta.11"), v("1.0.0-rc.1"),       v("1.0.0"),
        v("1.0.1"),            v("1.2.0-beta.1"),  v("1.2.0"),            v("1.10.0"),
        v("2.0.0-beta.99999999999999999999"),      v("2.0.0"),
    };
    for (std::size_t i = 0; i + 1 < ordered.size(); ++i) {
        INFO(ordered[i].text() << " < " << ordered[i + 1].text());
        CHECK(ordered[i] < ordered[i + 1]);
        CHECK(ordered[i + 1] > ordered[i]);
        CHECK(ordered[i] != ordered[i + 1]);
    }
    CHECK((v("v1.3.0") <=> v("1.3.0+build.5")) == std::strong_ordering::equal);
}

TEST_CASE("GitHub's list of releases reads newest first, without drafts and stray tags",
          "[update]")
{
    const auto releases = fixture_releases();
    // The draft 1.5.0, the "nightly" tag, and 1.2.0, whose page is in
    // another repository, are left out.
    REQUIRE(releases.size() == 3);
    CHECK(releases[0].tag == "v1.4.0-beta.1");
    CHECK(releases[1].tag == "v1.3.1");
    CHECK(releases[2].tag == "v1.3.0");

    const Release& beta = releases[0];
    CHECK(beta.beta);
    CHECK(beta.version == v("1.4.0-beta.1"));
    CHECK(beta.title == "v1.4.0-beta.1");  // It has no title of its own.

    const Release& r = releases[1];
    CHECK_FALSE(r.beta);
    CHECK(r.title == "SeventhSlide 1.3.1");
    CHECK(r.page_url == "https://github.com/umpquadigital/seventhslide/releases/tag/v1.3.1");
    CHECK(r.published_at == "2026-11-20T18:00:00Z");
    CHECK(r.notes.starts_with("<!-- Release notes"));

    // The six files the release workflow makes. The file linked from
    // another site, and the one whose upload did not finish, are left out.
    REQUIRE(r.files.size() == 6);
    const ReleaseFile* deb = r.file("seventhslide-ubuntu-24.04-amd64.deb");
    REQUIRE(deb != nullptr);
    CHECK(deb->url
          == "https://github.com/umpquadigital/seventhslide/releases/download/v1.3.1/"
             "seventhslide-ubuntu-24.04-amd64.deb");
    CHECK(deb->size == 91234567);
    CHECK(deb->sha256.size() == 64);
    CHECK(deb->sha256.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(r.file("seventhslide-extra.zip") == nullptr);
    CHECK(r.file("SeventhSlide-Linux-arm64.deb") == nullptr);
    CHECK(r.file("seventhslide") == nullptr);

    // A file GitHub reported no digest for keeps none.
    const ReleaseFile* dmg = releases[2].file("SeventhSlide-macOS-arm64.dmg");
    REQUIRE(dmg != nullptr);
    CHECK(dmg->sha256.empty());
}

TEST_CASE("A reply that is not a list of releases is refused", "[update]")
{
    CHECK_FALSE(parse_releases("", k_repo));
    CHECK_FALSE(parse_releases("{\"message\": \"API rate limit exceeded\"}", k_repo));
    CHECK_FALSE(parse_releases("[{\"tag_name\": \"v1.0.0\"", k_repo));
    CHECK_FALSE(parse_releases("<html>Unicorn!</html>", k_repo));

    const auto empty = parse_releases("[]", k_repo);
    REQUIRE(empty);
    CHECK(empty->empty());
    const auto junk = parse_releases(R"([1, "two", null, {"tag_name": 3}])", k_repo);
    REQUIRE(junk);
    CHECK(junk->empty());
}

TEST_CASE("A file must be named as its link names it, and carry a whole digest", "[update]")
{
    const auto releases = parse_releases(R"([{
        "tag_name": "v2.0.0",
        "html_url": "https://github.com/UmpquaDigital/SeventhSlide/releases/tag/v2.0.0",
        "assets": [
            {"name": "SeventhSlide-macOS-arm64.dmg", "size": 10, "digest": "sha256:short",
             "browser_download_url": "https://github.com/umpquadigital/seventhslide/releases/download/v2.0.0/SeventhSlide-macOS-arm64.dmg"},
            {"name": "SeventhSlide-Windows-x64-Setup.exe", "size": -4,
             "digest": "sha256:ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789",
             "browser_download_url": "https://github.com/umpquadigital/seventhslide/releases/download/v2.0.0/SeventhSlide-Windows-x64-Setup.exe"},
            {"name": "seventhslide-fedora-x86_64.rpm", "size": 10,
             "browser_download_url": "https://github.com/umpquadigital/seventhslide/releases/download/v2.0.0/other.rpm"},
            {"name": "", "size": 10,
             "browser_download_url": "https://github.com/umpquadigital/seventhslide/releases/download/v2.0.0/"}
        ]}])",
                                         k_repo);
    REQUIRE(releases);
    REQUIRE(releases->size() == 1);
    const Release& r = releases->front();
    REQUIRE(r.files.size() == 2);
    CHECK(r.files[0].sha256.empty());
    CHECK(r.files[1].sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    CHECK(r.files[1].size == 0);
}

TEST_CASE("Only links under the repository's releases on github.com are trusted", "[update]")
{
    for (const char* url : {
             "https://github.com/umpquadigital/seventhslide/releases/tag/v1.3.1",
             "https://github.com/umpquadigital/seventhslide/releases/download/v1.3.1/"
             "SeventhSlide-Windows-x64-Setup.exe",
             "https://github.com/UmpquaDigital/SeventhSlide/releases/tag/v1.4.0-beta.1",
             "https://github.com/umpquadigital/seventhslide/releases/latest",
         }) {
        INFO(url);
        CHECK(is_release_url(url, k_repo));
    }
    for (const char* url : {
             "",
             "http://github.com/umpquadigital/seventhslide/releases/tag/v1.3.1",
             "https://github.com.evil.example/umpquadigital/seventhslide/releases/tag/v1.3.1",
             "https://evil.example/umpquadigital/seventhslide/releases/tag/v1.3.1",
             "https://user@github.com/umpquadigital/seventhslide/releases/tag/v1.3.1",
             "https://github.com:8443/umpquadigital/seventhslide/releases/tag/v1.3.1",
             "HTTPS://GITHUB.COM/umpquadigital/seventhslide/releases/tag/v1.3.1",
             "https://github.com/someone-else/seventhslide/releases/tag/v1.3.1",
             "https://github.com/umpquadigital/seventhslide-fork/releases/tag/v1.3.1",
             "https://github.com/umpquadigital/seventhslide/releases",
             "https://github.com/umpquadigital/seventhslide/releases/",
             "https://github.com/umpquadigital/seventhslide/archive/v1.3.1.zip",
             "https://github.com/umpquadigital/seventhslide/releases/../../other/releases/x",
             "https://github.com/umpquadigital/seventhslide/releases/tag/./v1",
             "https://github.com/umpquadigital/seventhslide/releases//tag",
             "https://github.com/umpquadigital/seventhslide/releases/tag/v1.3.1?x=1",
             "https://github.com/umpquadigital/seventhslide/releases/tag/v1.3.1#notes",
             "https://github.com/umpquadigital/seventhslide/releases/tag/v1%2F..",
             "https://github.com/umpquadigital/seventhslide/releases/tag/v1\\..",
             "https://github.com/umpquadigital/seventhslide/releases/tag/v1 3",
             "javascript:alert(1)//https://github.com/umpquadigital/seventhslide/releases/x",
         }) {
        INFO(url);
        CHECK_FALSE(is_release_url(url, k_repo));
    }
    CHECK_FALSE(is_release_url("https://github.com/umpquadigital/seventhslide/releases/latest",
                               Repository{}));
}

TEST_CASE("The stable channel offers final versions, the beta channel betas too", "[update]")
{
    const auto releases = fixture_releases();
    const Release* stable = newest_release(releases, Channel::stable);
    REQUIRE(stable != nullptr);
    CHECK(stable->tag == "v1.3.1");
    const Release* beta = newest_release(releases, Channel::beta);
    REQUIRE(beta != nullptr);
    CHECK(beta->tag == "v1.4.0-beta.1");

    // A release GitHub marks as a prerelease is a beta, whatever its tag.
    const std::vector<Release> marked = {release("2.0.0", true), release("1.9.0")};
    CHECK(newest_release(marked, Channel::stable)->tag == "v1.9.0");
    CHECK(newest_release(marked, Channel::beta)->tag == "v2.0.0");

    CHECK(newest_release({}, Channel::beta) == nullptr);
    CHECK(newest_release({release("2.0.0-beta.1")}, Channel::stable) == nullptr);

    CHECK(default_channel(v("1.3.0")) == Channel::stable);
    CHECK(default_channel(v("1.4.0-beta.1")) == Channel::beta);
}

TEST_CASE("Only a newer release is offered, and a skipped version is not", "[update]")
{
    const auto releases = fixture_releases();
    const auto offered = [&](Channel channel, std::string_view running,
                             std::optional<Version> skipped = std::nullopt) {
        const Release* r = release_to_offer(releases, channel, v(running), skipped);
        return r == nullptr ? std::string{} : r->tag;
    };

    CHECK(offered(Channel::stable, "1.3.0") == "v1.3.1");
    CHECK(offered(Channel::stable, "1.3.1").empty());
    CHECK(offered(Channel::stable, "1.4.0").empty());
    CHECK(offered(Channel::beta, "1.3.1") == "v1.4.0-beta.1");
    CHECK(offered(Channel::beta, "1.4.0-beta.1").empty());
    CHECK(offered(Channel::beta, "1.4.0").empty());

    // A beta tester who moves to stable hears of nothing until a final
    // version passes the beta they have.
    CHECK(offered(Channel::stable, "1.4.0-beta.1").empty());
    CHECK(offered(Channel::stable, "1.3.1-beta.3") == "v1.3.1");

    // Skipping a version hides that version only.
    CHECK(offered(Channel::stable, "1.3.0", v("1.3.1")).empty());
    CHECK(offered(Channel::stable, "1.3.0", v("1.3.0")) == "v1.3.1");
    CHECK(offered(Channel::beta, "1.3.0", v("1.3.1")) == "v1.4.0-beta.1");
    CHECK(offered(Channel::beta, "1.3.0", v("1.4.0-beta.1")).empty());
}

TEST_CASE("Release notes read as plain text", "[update]")
{
    const auto releases = fixture_releases();
    CHECK(notes_text(releases[1].notes)
          == "What's Changed\n"
             "Fixes\n"
             "• Outputs: A window keeps its screen after sleep by @UmpquaRiver in "
             "https://github.com/umpquadigital/seventhslide/pull/41\n"
             "• Fix --server start on Fedora by @UmpquaRiver in "
             "https://github.com/umpquadigital/seventhslide/pull/43\n"
             "\n"
             "Full Changelog: https://github.com/umpquadigital/seventhslide/compare/v1.3.0...v1.3.1");

    CHECK(notes_text("") == "");
    CHECK(notes_text("\n\n  \n") == "");
    CHECK(notes_text("# Title\n\n\n\nBody") == "Title\n\nBody");
    CHECK(notes_text("####### seven") == "####### seven");
    CHECK(notes_text("###") == "");
    CHECK(notes_text("See [the guide](https://example.com/guide) and ![a picture](x.png).")
          == "See the guide and .");
    CHECK(notes_text("[**Bold** link](u)") == "Bold link");
    CHECK(notes_text("An [unclosed link and a ] lone bracket") == "An [unclosed link and a ] lone bracket");
    CHECK(notes_text("Visit <https://seventhslide.org>.") == "Visit https://seventhslide.org.");
    CHECK(notes_text("A \\*star\\* and a file_name and 2 * 3") == "A *star* and a file_name and 2 * 3");
    CHECK(notes_text("> Quoted\n- one\n  * nested\n+ three") == "Quoted\n• one\n  • nested\n• three");
    CHECK(notes_text("Above\n\n---\n\nBelow") == "Above\n\nBelow");
    CHECK(notes_text("~~gone~~ and __under__") == "gone and under");
    CHECK(notes_text("Run:\n```bash\nsudo apt install ./**x**.deb\n```\nDone")
          == "Run:\nsudo apt install ./**x**.deb\nDone");
    CHECK(notes_text("Before <!-- hidden\nacross lines --> after") == "Before  after");
    CHECK(notes_text("Kept <!-- never closed") == "Kept");
    CHECK(notes_text("<b>Not HTML</b>") == "<b>Not HTML</b>");
}

TEST_CASE("Each package is installed its own way", "[update]")
{
    CHECK(installer_for("SeventhSlide-Windows-x64-Setup.exe") == Installer::windows_setup);
    CHECK(installer_for("SeventhSlide-macOS-arm64.dmg") == Installer::disk_image);
    CHECK(installer_for("seventhslide-ubuntu-24.04-amd64.deb") == Installer::deb);
    CHECK(installer_for("seventhslide-fedora-x86_64.rpm") == Installer::rpm);
    CHECK(installer_for("SETUP.EXE") == Installer::windows_setup);
    CHECK(installer_for("SHA2-256SUMS") == Installer::none);
    CHECK(installer_for(".deb") == Installer::none);
    CHECK(installer_for("") == Installer::none);

    CHECK(install_command(Installer::deb, "/home/me/.local/share/SeventhSlide/updates/s.deb")
          == std::vector<std::string>{"pkexec", "apt-get", "install", "--yes",
                                      "/home/me/.local/share/SeventhSlide/updates/s.deb"});
    CHECK(install_command(Installer::rpm, "/tmp/s.rpm")
          == std::vector<std::string>{"pkexec", "dnf", "install", "--assumeyes", "/tmp/s.rpm"});
    // A relative path could read as an option: "-o..." is apt-get's.
    CHECK(install_command(Installer::deb, "-oAPT::x=1").empty());
    CHECK(install_command(Installer::deb, "s.deb").empty());
    CHECK(install_command(Installer::windows_setup, "/x/Setup.exe").empty());
    CHECK(install_command(Installer::disk_image, "/x/s.dmg").empty());
    CHECK(install_command(Installer::none, "/x/s").empty());
}
