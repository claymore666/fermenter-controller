#pragma once

/**
 * Core utility functions
 * Common helper functions used across the codebase
 */

#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include "types.h"

namespace core {

/**
 * Safe string copy with guaranteed null termination
 * @param dst Destination buffer
 * @param src Source string
 * @param size Size of destination buffer
 */
inline void safe_strncpy(char* dst, const char* src, size_t size) {
    if (size == 0) return;
    if (!dst) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

/**
 * Validate fermenter ID (1-indexed, 1 to MAX_FERMENTERS)
 * @param id Fermenter ID to validate
 * @return true if ID is valid
 */
inline bool is_valid_fermenter_id(uint8_t id) {
    return id > 0 && id <= MAX_FERMENTERS;
}

/**
 * Convert fermenter ID (1-indexed) to array index (0-indexed)
 * @param id Fermenter ID (1-based)
 * @return Array index (0-based)
 * @note Caller must validate ID first with is_valid_fermenter_id()
 */
inline uint8_t fermenter_id_to_index(uint8_t id) {
    return id - 1;
}

/**
 * Convert array index (0-indexed) to fermenter ID (1-indexed)
 * @param index Array index (0-based)
 * @return Fermenter ID (1-based)
 */
inline uint8_t index_to_fermenter_id(uint8_t index) {
    return index + 1;
}

/**
 * Clamp a value to a range
 * @param value Value to clamp
 * @param min Minimum value
 * @param max Maximum value
 * @return Clamped value
 */
template<typename T>
inline T clamp(T value, T min, T max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * Check if a value is within a range (inclusive)
 * @param value Value to check
 * @param min Minimum value
 * @param max Maximum value
 * @return true if value is within range
 */
template<typename T>
inline bool in_range(T value, T min, T max) {
    return value >= min && value <= max;
}

/**
 * Simple bubble sort for small arrays (ascending order)
 * @param arr Array to sort
 * @param count Number of elements
 * @note For embedded use - efficient for small arrays (<20 elements)
 */
template<typename T>
inline void bubble_sort(T* arr, size_t count) {
    if (count < 2) return;
    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = 0; j < count - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                T temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

/**
 * Bubble sort with custom comparator
 * @param arr Array to sort
 * @param count Number of elements
 * @param compare Comparator returning true if first arg should come after second
 * @note For embedded use - efficient for small arrays (<20 elements)
 */
template<typename T, typename Compare>
inline void bubble_sort(T* arr, size_t count, Compare compare) {
    if (count < 2) return;
    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = 0; j < count - i - 1; j++) {
            if (compare(arr[j], arr[j + 1])) {
                T temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
}

/**
 * Safe snprintf append - appends to buffer at offset with bounds checking
 * @param buffer Target buffer
 * @param size Total buffer size
 * @param offset Current offset (updated on return)
 * @param fmt Format string
 * @param ... Format arguments
 * @return Number of characters written (not including null terminator)
 * @note Prevents buffer overflow by checking bounds before writing
 *       and clamping offset if truncation would occur
 */
inline int safe_snprintf_append(char* buffer, size_t size, int& offset, const char* fmt, ...) {
    // Check bounds before writing - if already at or past end, do nothing
    if (buffer == nullptr || size == 0 || static_cast<size_t>(offset) >= size) {
        return 0;
    }

    size_t remaining = size - static_cast<size_t>(offset);

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + offset, remaining, fmt, args);
    va_end(args);

    // vsnprintf returns the number of chars that would have been written (not including null)
    // If written >= remaining, truncation occurred
    if (written > 0) {
        if (static_cast<size_t>(written) >= remaining) {
            // Truncation occurred - clamp offset to end of buffer (before null)
            offset = static_cast<int>(size) - 1;
        } else {
            offset += written;
        }
    }

    return written > 0 ? written : 0;
}

} // namespace core
