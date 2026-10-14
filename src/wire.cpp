#include "wire.hpp"
#include <stdexcept>
namespace wm::wire {
std::optional<Json> Read(std::istream &in) {
    std::string record;
    bool started = false;
    for (;;) {
        unsigned char h[4];
        in.read(reinterpret_cast<char *>(h), 4);
        if (in.gcount() == 0 && !started)
            return std::nullopt;
        if (in.gcount() != 4)
            throw std::runtime_error("En-tête process-v2 incomplet");
        const uint32_t raw = static_cast<uint32_t>(h[0]) | (static_cast<uint32_t>(h[1]) << 8) |
                             (static_cast<uint32_t>(h[2]) << 16) |
                             (static_cast<uint32_t>(h[3]) << 24);
        const uint32_t kind = raw >> 30, length = raw & 0x3fffffff;
        if (!length || length > max_chunk)
            throw std::runtime_error("Taille de fragment process-v2 invalide");
        if ((!started && kind > 1) || (started && kind < 2))
            throw std::runtime_error("Fragments process-v2 désordonnés");
        const auto previous = record.size();
        record.resize(previous + length);
        in.read(record.data() + previous, length);
        if (static_cast<uint32_t>(in.gcount()) != length)
            throw std::runtime_error("Fragment process-v2 incomplet");
        if (kind == 0 || kind == 3)
            return Json::parse(record);
        started = true;
    }
}
void Write(std::ostream &out, const Json &frame) {
    const auto bytes = frame.dump();
    for (size_t offset = 0; offset < bytes.size(); offset += max_chunk) {
        const auto size = static_cast<uint32_t>(std::min<size_t>(max_chunk, bytes.size() - offset));
        const uint32_t kind = bytes.size() <= max_chunk       ? 0
                              : offset == 0                   ? 1
                              : offset + size == bytes.size() ? 3
                                                              : 2;
        const auto raw = (kind << 30) | size;
        unsigned char h[4] = {static_cast<unsigned char>(raw), static_cast<unsigned char>(raw >> 8),
                              static_cast<unsigned char>(raw >> 16),
                              static_cast<unsigned char>(raw >> 24)};
        out.write(reinterpret_cast<char *>(h), 4);
        out.write(bytes.data() + offset, size);
    }
    out.flush();
    if (!out)
        throw std::runtime_error("Sortie process-v2 fermée");
}
} // namespace wm::wire
