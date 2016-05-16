#pragma once

namespace extract
{

// Used as KeyExtract for the sets.
struct identity {
    template<typename U>
    constexpr auto operator()(U&& v) const -> decltype(std::forward<U>(v)) {
        return std::forward<U>(v);
    }
};

// Used as KeyExtract for the maps.
struct first {
    template<typename U>
    constexpr auto operator()(U&& v) const -> decltype(std::get<0>(v)) {
        return std::get<0>(v);
    }
};

// Used as MappedExtract for the maps.
struct second {
    template<typename U>
    constexpr auto operator()(U&& v) const -> decltype(std::get<1>(v)) {
        return std::get<1>(v);
    }
};

}

