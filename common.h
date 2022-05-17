#ifndef SIK_2022_COMMON_H
#define SIK_2022_COMMON_H

#include <optional>
#include <vector>
#include <stdint.h>
#include <string>
#include <unordered_map>

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
    uint8_t result = (uint8_t)(**buffer);
    *buffer += 1;
    *bytes_to_read -= 1;
    return result;
}

template<>
std::optional<uint32_t> parse<uint32_t>(char* *buffer, size_t *bytes_to_read) {
    if (*bytes_to_read < 4)
        return {};
    uint8_t result = (uint32_t)(**buffer);
    *buffer += 4;
    *bytes_to_read -= 4;
    return result;
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
std::optional<std::vector<T>> parse_vec(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint32_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    std::vector<T> result;
    for (uint32_t i = 0; i < size_value; i++) {
        auto to_push_option = parse<T>(buffer, bytes_to_read);
        if (!to_push_option)
            return {};
        result.push_back(to_push_option.value());
    }
}

template<typename K, typename V>
std::optional<std::unordered_map<K, V>> parse_map(char* *buffer, size_t *bytes_to_read) {
    auto size = parse<uint32_t>(buffer, bytes_to_read);
    if (!size)
        return {};
    auto size_value = size.value();
    std::unordered_map<K, V> result;
    for (uint32_t i = 0; i < size_value; i++) {
        auto key_option = parse<K>(buffer, bytes_to_read);
        if (!key_option)
            return {};
        auto value_option = parse<V>(buffer, bytes_to_read);
        if (!value_option)
            return {};
        result.insert({key_option.value(), value_option.value()});
    }
    return result;
}

/* = = = = = *
 * SERIALIZE *
 * = = = = = */

#endif //SIK_2022_COMMON_H
