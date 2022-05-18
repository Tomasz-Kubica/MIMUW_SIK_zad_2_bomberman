#ifndef SIK_2022_COMMON_H
#define SIK_2022_COMMON_H

#include <arpa/inet.h>
#include <optional>
#include <vector>
#include <stdint.h>
#include <string>
#include <unordered_map>
#include <concepts>

/* = = = *
 * TYPES *
 * = = = */


/* = = = *
 * PARSE *
 * = = = */

template<typename T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) = delete;

template<>
std::optional<uint8_t> parse<uint8_t>(char* *buffer, size_t *bytes_to_read) {
    if (*bytes_to_read < 1)
        return {};
    auto result = *(uint8_t*)*buffer;
    *buffer += 1;
    *bytes_to_read -= 1;
    return result;
}

template<>
std::optional<uint16_t> parse<uint16_t>(char* *buffer, size_t *bytes_to_read) {
    if (*bytes_to_read < 2)
        return {};
    auto result = *(uint16_t*)*buffer;
    *buffer += 2;
    *bytes_to_read -= 2;
    return ntohs(result);
}

template<>
std::optional<uint32_t> parse<uint32_t>(char* *buffer, size_t *bytes_to_read) {
    if (*bytes_to_read < 4)
        return {};
    auto result = *(uint32_t*)*buffer;
    *buffer += 4;
    *bytes_to_read -= 4;
    return ntohl(result);
}

template<>
std::optional<std::string> parse(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint8_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    if (*bytes_to_read < size_value)
        return {}; // not enough bytes left
    std::string result(*buffer, size_value);
    *buffer += size_value;
    *bytes_to_read -= size_value;
    return result;
}

template<typename T>
concept List = requires(T t, typename T::value_type e, size_t i) {
    typename T::value_type;
    t.push_back(e);
    { t.at(i) } -> std::convertible_to<typename T::value_type>;
    { t.size() } -> std::same_as<size_t>;
};

template<List T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint32_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    T result;
    for (uint32_t i = 0; i < size_value; i++) {
        auto to_push_option = parse<typename T::value_type>(buffer, bytes_to_read);
        if (!to_push_option)
            return {};
        result.push_back(to_push_option.value());
    }
    return result;
}

template<typename T>
concept Map = requires(
    T t,
    typename T::value_type e,
    typename T::mapped_type v,
    typename T::key_type k
) {
    typename T::value_type;
    typename T::mapped_type;
    typename T::key_type;
    typename T::iterator;
    t.insert(e);
    { t.size() } -> std::convertible_to<size_t>;
    { t.begin() } -> std::same_as<typename T::iterator>;
    { t.end() } -> std::same_as<typename T::iterator>;
    requires std::same_as<typename T::value_type, std::pair<const typename T::key_type, typename T::mapped_type>>;
};

template<Map T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint32_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    T result;
    for (uint32_t i = 0; i < size_value; i++) {
        auto key_option = parse<typename T::key_type>(buffer, bytes_to_read);
        if (!key_option)
            return {};
        auto value_option = parse<typename T::mapped_type>(buffer, bytes_to_read);
        if (!value_option)
            return {};
        result.insert({key_option.value(), value_option.value()});
    }
    return result;
}

template<typename T>
concept Pair = requires(T t) {
    typename T::first_type;
    typename T::second_type;
    t.first;
    t.second;
    requires std::same_as<decltype(t.first), typename T::first_type>;
    requires std::same_as<decltype(t.second), typename T::second_type>;
};

template<Pair T>
std::optional<T> parse(char* *buffer, size_t *bytes_to_read) {
    auto first = parse<typename T::first_type>(buffer, bytes_to_read);
    auto second = parse<typename T::second_type>(buffer, bytes_to_read);
    if (first && second) {
        T result;
        result.first = first.value();
        result.second = second.value();
        return result;
    }
    return {};
}

/* = = = = = *
 * SERIALIZE *
 * = = = = = */

#endif //SIK_2022_COMMON_H
