#include <catch2/catch_test_macros.hpp>

#include "test_temp_dir.hpp"

#include <seventhslide/library/cloud_folder.hpp>

#include <filesystem>
#include <fstream>

using seventhslide::library::cloud_sync_service;

namespace fs = std::filesystem;

TEST_CASE("cloud_sync_service names the service by its folder", "[library][cloud]")
{
    // None of these need exist: the names alone say what syncs them.
    CHECK(cloud_sync_service("/home/pat/Dropbox/Church") == "Dropbox");
    CHECK(cloud_sync_service("/home/pat/dropbox/Church") == "Dropbox");
    CHECK(cloud_sync_service("C:/Users/Pat/Dropbox (Grace Church)/Media") == "Dropbox");
    CHECK(cloud_sync_service("G:/My Drive/SeventhSlide") == "Google Drive");
    CHECK(cloud_sync_service("/Users/pat/Google Drive/SeventhSlide") == "Google Drive");
    CHECK(cloud_sync_service("/run/user/1000/gvfs/google-drive:host=gmail.com,user=pat/x")
          == "Google Drive");
    CHECK(cloud_sync_service("C:/Users/Pat/OneDrive - Grace Church/Library") == "OneDrive");
    CHECK(cloud_sync_service("C:/Users/Pat/OneDrive/Documents/Library") == "OneDrive");
    CHECK(cloud_sync_service("/Users/pat/Library/Mobile Documents/com~apple~CloudDocs/Lib")
          == "iCloud Drive");
    CHECK(cloud_sync_service("C:/Users/Pat/iCloudDrive/Lib") == "iCloud Drive");
    CHECK(cloud_sync_service("/home/pat/Nextcloud/Church") == "Nextcloud");
    CHECK(cloud_sync_service("/home/pat/pCloudDrive/Church") == "pCloud");

    // The whole folder name, not a part of it.
    CHECK(cloud_sync_service("/home/pat/Dropboxes/Church").empty());
    CHECK(cloud_sync_service("/home/pat/Old Dropbox Stuff/Church").empty());
    CHECK(cloud_sync_service("/home/pat/Documents/SeventhSlide").empty());
    CHECK(cloud_sync_service("").empty());
}

TEST_CASE("cloud_sync_service reads macOS CloudStorage folders", "[library][cloud]")
{
    CHECK(cloud_sync_service("/Users/pat/Library/CloudStorage/GoogleDrive-pat@gmail.com/My Drive/x")
          == "Google Drive");
    CHECK(cloud_sync_service("/Users/pat/Library/CloudStorage/OneDrive-Personal/x") == "OneDrive");
    CHECK(cloud_sync_service("/Users/pat/Library/CloudStorage/Box-Box/x") == "Box");
    // A service with no rule of its own is named by its folder.
    CHECK(cloud_sync_service("/Users/pat/Library/CloudStorage/SynologyDrive-church/x")
          == "SynologyDrive");
    // The CloudStorage folder itself syncs nothing.
    CHECK(cloud_sync_service("/Users/pat/Library/CloudStorage").empty());
    // Only Library/CloudStorage is the File Provider folder.
    CHECK(cloud_sync_service("/Users/pat/Projects/CloudStorage/Thing-1/x").empty());
}

TEST_CASE("cloud_sync_service finds a synced folder by its marker", "[library][cloud]")
{
    const seventhslide::test::TempDir tmp;
    const fs::path synced = tmp.path / "Church Files";
    const fs::path library = synced / "Libraries" / "Grace";
    fs::create_directories(library);
    CHECK(cloud_sync_service(library).empty());

    // Dropbox leaves a .dropbox file at the top of what it syncs, wherever
    // that folder was moved and whatever it is called.
    std::ofstream(synced / ".dropbox") << "{}";
    CHECK(cloud_sync_service(library) == "Dropbox");
    CHECK(cloud_sync_service(synced) == "Dropbox");
    CHECK(cloud_sync_service(tmp.path).empty());

    // Syncthing's marker is a folder; a library not yet made under it counts.
    const fs::path shared = tmp.path / "Shared";
    fs::create_directories(shared / ".stfolder");
    CHECK(cloud_sync_service(shared / "New Library") == "Syncthing");
}
