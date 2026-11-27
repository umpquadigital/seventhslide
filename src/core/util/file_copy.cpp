#include "seventhslide/util/file_copy.hpp"

#include <cerrno>
#include <cstdio>
#include <memory>
#include <system_error>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <sys/clonefile.h>
#endif

namespace seventhslide::util {
namespace {

struct FileCloser {
    void operator()(std::FILE* f) const noexcept { std::fclose(f); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

[[nodiscard]] File open_file(const std::filesystem::path& path, bool write)
{
#if defined(_WIN32)
    return File(_wfopen(path.c_str(), write ? L"wb" : L"rb"));
#else
    return File(std::fopen(path.c_str(), write ? "wb" : "rb"));
#endif
}

/// Unbuffered, so the stdio streams and the kernel copy below share one file
/// offset and the buffer here is the only copy of the data in user space.
void unbuffer(std::FILE* f) noexcept
{
    std::setvbuf(f, nullptr, _IONBF, 0);
}

#if defined(__linux__)
enum class KernelCopy { Done, Stopped, Failed, Unsupported };

/// The bytes never leave the kernel. Large chunks keep the call count low;
/// 16 MiB still checks back several times a second at disk speed.
[[nodiscard]] KernelCopy kernel_copy(std::FILE* in,
                                     std::FILE* out,
                                     std::uintmax_t& copied,
                                     const CopyProgress& progress)
{
    constexpr std::size_t k_chunk = 16u * 1024u * 1024u;
    const int in_fd = fileno(in);
    const int out_fd = fileno(out);
    for (;;) {
        const auto n = ::copy_file_range(in_fd, nullptr, out_fd, nullptr, k_chunk, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            // Some filesystems (and older kernels across filesystems) refuse
            // outright. That is only safe to fall back from before any byte
            // has moved.
            return copied == 0 ? KernelCopy::Unsupported : KernelCopy::Failed;
        }
        if (n == 0) {
            // Some pseudo filesystems report end of file on the first call
            // whatever their size; the plain loop reads them correctly, and
            // an empty file costs it one read.
            return copied == 0 ? KernelCopy::Unsupported : KernelCopy::Done;
        }
        copied += static_cast<std::uintmax_t>(n);
        if (progress && !progress(copied)) {
            return KernelCopy::Stopped;
        }
    }
}
#endif

[[nodiscard]] CopyOutcome buffered_copy(std::FILE* in,
                                        std::FILE* out,
                                        std::uintmax_t& copied,
                                        const CopyProgress& progress)
{
    constexpr std::size_t k_chunk = 1024u * 1024u;
    std::vector<char> buffer(k_chunk);
    for (;;) {
        const auto n = std::fread(buffer.data(), 1, buffer.size(), in);
        if (n > 0) {
            if (std::fwrite(buffer.data(), 1, n, out) != n) {
                return CopyOutcome::Failed;
            }
            copied += n;
            if (progress && !progress(copied)) {
                return CopyOutcome::Stopped;
            }
        }
        if (n < buffer.size()) {
            return std::ferror(in) != 0 ? CopyOutcome::Failed : CopyOutcome::Copied;
        }
    }
}

[[nodiscard]] CopyOutcome copy_streams(std::FILE* in,
                                       std::FILE* out,
                                       const CopyProgress& progress)
{
    std::uintmax_t copied = 0;
#if defined(__linux__)
    switch (kernel_copy(in, out, copied, progress)) {
    case KernelCopy::Done:
        return CopyOutcome::Copied;
    case KernelCopy::Stopped:
        return CopyOutcome::Stopped;
    case KernelCopy::Failed:
        return CopyOutcome::Failed;
    case KernelCopy::Unsupported:
        break;
    }
#endif
    return buffered_copy(in, out, copied, progress);
}

}  // namespace

CopyOutcome copy_file_chunked(const std::filesystem::path& from,
                              const std::filesystem::path& to,
                              const CopyProgress& progress)
{
#if defined(__APPLE__)
    // On one APFS volume, a clone: instant whatever the size, and no space
    // used until one of the two changes. Another volume, or another
    // filesystem, refuses it, and the bytes are copied as elsewhere.
    {
        std::error_code ec;
        std::filesystem::remove(to, ec);
        if (::clonefile(from.c_str(), to.c_str(), CLONE_NOOWNERCOPY) == 0) {
            const auto size = std::filesystem::file_size(to, ec);
            if (progress && !progress(ec ? 0 : size)) {
                std::filesystem::remove(to, ec);
                return CopyOutcome::Stopped;
            }
            return CopyOutcome::Copied;
        }
    }
#endif
    auto outcome = CopyOutcome::Failed;
    {
        File in = open_file(from, false);
        if (!in) {
            return CopyOutcome::Failed;
        }
        File out = open_file(to, true);
        if (!out) {
            return CopyOutcome::Failed;
        }
        unbuffer(in.get());
        unbuffer(out.get());
        outcome = copy_streams(in.get(), out.get(), progress);
        // A close that fails can mean the last write never reached the disk.
        if (std::fclose(out.release()) != 0 && outcome == CopyOutcome::Copied) {
            outcome = CopyOutcome::Failed;
        }
    }
    if (outcome != CopyOutcome::Copied) {
        std::error_code ec;
        std::filesystem::remove(to, ec);
    }
    return outcome;
}

}  // namespace seventhslide::util
