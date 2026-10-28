#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace engine_net
{
// 配布物の取り違えを見つける内容指紋。暗号署名や不正改造対策には使わない。
// 全ファイルをメモリへ読み込まず、一定サイズで読み、名前順に結合する。
class content_fingerprint
{
public:
    void append(const char* data, std::size_t size)
    { for (std::size_t i = 0; i < size; ++i) { hash_ ^= static_cast<unsigned char>(data[i]); hash_ *= 1099511628211ull; } }
    void file(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("互換性を確認するファイルを開けません：" + path.u8string());
        std::array<char, 65536> buffer{};
        while (stream.read(buffer.data(), buffer.size()) || stream.gcount()) append(buffer.data(), static_cast<std::size_t>(stream.gcount()));
        if (!stream.eof()) throw std::runtime_error("互換性を確認するファイルを最後まで読めません：" + path.u8string());
        const char end = 0; append(&end, 1);
    }
    void files(std::vector<std::filesystem::path> paths, const std::filesystem::path& root)
    {
        std::sort(paths.begin(), paths.end()); paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        for (const auto& path : paths)
        {
            const auto name = path.lexically_relative(root).generic_u8string();
            append(name.c_str(), name.size() + 1); file(path);
        }
    }
    std::uint64_t value() const { return hash_; }
private:
    std::uint64_t hash_ = 14695981039346656037ull;
};
}
