#include "seventhslide/library/computer_key.hpp"

#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/read_file.hpp"

#include <stdexcept>
#include <system_error>

namespace seventhslide::library {

std::filesystem::path computer_key_file(const std::filesystem::path& data_dir)
{
    return data_dir / std::string{k_computer_key_filename};
}

crypto::SigningKey load_computer_key(const std::filesystem::path& data_dir)
{
    const auto file = computer_key_file(data_dir);
    const auto shown = file.string();
    if (const auto pem = util::read_file(file)) {
        auto key = crypto::SigningKey::from_pem(*pem);
        if (!key) {
            throw std::runtime_error("This computer's sync key file is not a key: " + shown
                                     + ". Move it away to make a new one (other computers will "
                                       "then need to invite this one again).");
        }
        return *key;
    }

    auto key = crypto::SigningKey::generate();
    std::error_code ec;
    std::filesystem::create_directories(data_dir, ec);
    // The key is this user's alone, from the moment its file exists.
    if (!util::write_file_atomically(file, key.pem(), util::FileAccess::OwnerOnly)) {
        throw std::runtime_error("This computer's sync key could not be written: " + shown);
    }
    return key;
}

}  // namespace seventhslide::library
