#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace engine_net::state_archive
{
using bytes = std::vector<std::uint8_t>;
constexpr std::size_t byte_limit = 4 * 1024 * 1024, item_limit = 16384, allocation_limit = 32 * 1024 * 1024;
template<class T> struct is_vector : std::false_type {};
template<class T, class A> struct is_vector<std::vector<T, A>> : std::true_type {};
template<class T> struct is_array : std::false_type {};
template<class T, std::size_t N> struct is_array<std::array<T, N>> : std::true_type {};

// フィールドを決めた順序で記録する。構造体全体・ポインター・paddingは書き出さない。
// 整数は64bitのビッグエンディアン、floatはIEEE754の32bit、boolは0/1の1バイト。
// 独自型は同じ名前空間のstate_fields(archive, value)でフィールドを列挙する。
class writer
{
public:
    static constexpr bool loading = false;
    template<class... T> void operator()(const T&... values) { (value(values), ...); }
    bytes finish() && { return std::move(data_); }
private:
    bytes data_;
    void number(std::uint64_t bits, unsigned count)
    {
        if (count > byte_limit - data_.size()) throw std::length_error("state byte limit exceeded");
        for (unsigned i = count; i > 0; --i) data_.push_back(static_cast<std::uint8_t>(bits >> ((i - 1) * 8)));
    }
    template<class T> void value(const T& v)
    {
        if constexpr (std::is_same_v<T, bool>) number(v ? 1 : 0, 1);
        else if constexpr (std::is_integral_v<T>) number(static_cast<std::uint64_t>(v), 8);
        else if constexpr (std::is_enum_v<T>) value(static_cast<std::underlying_type_t<T>>(v));
        else if constexpr (std::is_same_v<T, float>)
        {
            static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
            if (!std::isfinite(v)) throw std::invalid_argument("nonfinite state float");
            std::uint32_t bits; std::memcpy(&bits, &v, 4); number(bits, 4);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (v.size() > item_limit) throw std::length_error("state string limit exceeded");
            number(v.size(), 8); for (unsigned char c : v) number(c, 1);
        }
        else if constexpr (is_vector<T>::value)
        {
            if (v.size() > item_limit) throw std::length_error("state vector limit exceeded");
            number(v.size(), 8); for (const auto& item : v) value(item);
        }
        else if constexpr (std::is_array_v<T> || is_array<T>::value) { for (const auto& item : v) value(item); }
        else
        {
            static_assert(!std::is_pointer_v<T>, "Stateにポインターは保存できません");
            // 共通のフィールド列挙を読むための参照。writerはフィールドを変更しない。
            state_fields(*this, const_cast<T&>(v));
        }
    }
};

// 不正長・切れたデータ・余分な末尾・非有限数を拒否する。
// 受信内容を現行Sceneへ直接読み込まず、一時オブジェクトを完成させてから置き換える。
class reader
{
public:
    static constexpr bool loading = true;
    explicit reader(const bytes& data) : data_(data)
    { if (data.size() > byte_limit) throw std::length_error("state byte limit exceeded"); }
    template<class... T> void operator()(T&... values) { (value(values), ...); }
    void finish() const { if (position_ != data_.size()) throw std::invalid_argument("trailing state bytes"); }
private:
    const bytes& data_;
    std::size_t position_ = 0, allocated_ = 0;
    std::uint64_t number(unsigned count)
    {
        if (count > data_.size() - position_) throw std::invalid_argument("truncated state");
        std::uint64_t bits = 0; for (unsigned i = 0; i < count; ++i) bits = (bits << 8) | data_[position_++];
        return bits;
    }
    std::size_t length(std::size_t stride)
    {
        const auto count = number(8);
        if (count > item_limit || count > (allocation_limit - allocated_) / stride)
            throw std::length_error("state allocation limit exceeded");
        allocated_ += static_cast<std::size_t>(count) * stride;
        return static_cast<std::size_t>(count);
    }
    template<class T> void value(T& v)
    {
        if constexpr (std::is_same_v<T, bool>)
        { auto b = number(1); if (b > 1) throw std::invalid_argument("invalid state bool"); v = b != 0; }
        else if constexpr (std::is_integral_v<T>)
        {
            auto bits = number(8);
            if constexpr (std::is_signed_v<T>)
            {
                const std::int64_t signed_value = bits <= INT64_MAX ? static_cast<std::int64_t>(bits) :
                    -1 - static_cast<std::int64_t>(UINT64_MAX - bits);
                if (signed_value < (std::numeric_limits<T>::min)() || signed_value > (std::numeric_limits<T>::max)())
                    throw std::invalid_argument("state signed integer out of range");
                v = static_cast<T>(signed_value);
            }
            else
            {
                if (bits > (std::numeric_limits<T>::max)()) throw std::invalid_argument("state unsigned integer out of range");
                v = static_cast<T>(bits);
            }
        }
        else if constexpr (std::is_enum_v<T>) { std::underlying_type_t<T> item{}; value(item); v = static_cast<T>(item); }
        else if constexpr (std::is_same_v<T, float>)
        {
            std::uint32_t bits = static_cast<std::uint32_t>(number(4)); std::memcpy(&v, &bits, 4);
            if (!std::isfinite(v)) throw std::invalid_argument("nonfinite state float");
        }
        else if constexpr (std::is_same_v<T, std::string>)
        { v.resize(length(1)); for (char& c : v) c = static_cast<char>(number(1)); }
        else if constexpr (is_vector<T>::value)
        { v.resize(length(sizeof(typename T::value_type))); for (auto& item : v) value(item); }
        else if constexpr (std::is_array_v<T> || is_array<T>::value) { for (auto& item : v) value(item); }
        else
        {
            static_assert(!std::is_pointer_v<T>, "Stateにポインターは復元できません");
            state_fields(*this, v);
        }
    }
};
}
