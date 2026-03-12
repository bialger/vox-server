#ifndef VOX_COMMON_EXPECTED_POLYFILL_HPP
#define VOX_COMMON_EXPECTED_POLYFILL_HPP

// Polyfill for std::expected when Clang/clangd fails to resolve it from MinGW libstdc++.
// Only used for Clang (IDE); GCC build uses the real <expected>.
#include <utility>
#include <variant>

namespace std {

template<typename E>
class unexpected {
public:
  constexpr explicit unexpected(const E& e) : value_(e) {
  }
  constexpr explicit unexpected(E&& e) : value_(std::move(e)) {
  }
  constexpr const E& value() const& {
    return value_;
  }
  constexpr E& value() & {
    return value_;
  }

private:
  E value_;
};

template<typename T, typename E>
class expected {
public:
  constexpr expected(const T& v) : data_(v) {
  }
  constexpr expected(T&& v) : data_(std::move(v)) {
  }
  constexpr expected(const unexpected<E>& e) : data_(std::in_place_index<1>, e.value()) {
  }
  constexpr expected(unexpected<E>&& e) : data_(std::in_place_index<1>, std::move(e.value())) {
  }

  constexpr bool has_value() const {
    return data_.index() == 0;
  }
  constexpr explicit operator bool() const {
    return has_value();
  }
  constexpr const T& value() const& {
    return std::get<0>(data_);
  }
  constexpr T& value() & {
    return std::get<0>(data_);
  }
  constexpr const E& error() const& {
    return std::get<1>(data_);
  }
  constexpr E& error() & {
    return std::get<1>(data_);
  }
  constexpr const T* operator->() const {
    return &std::get<0>(data_);
  }
  constexpr T* operator->() {
    return &std::get<0>(data_);
  }
  constexpr const T& operator*() const& {
    return value();
  }
  constexpr T& operator*() & {
    return value();
  }

private:
  std::variant<T, E> data_;
};

template<typename E>
class expected<void, E> {
public:
  constexpr expected() : has_val_(true) {
  }
  constexpr expected(const unexpected<E>& e) : err_(e.value()), has_val_(false) {
  }
  constexpr expected(unexpected<E>&& e) : err_(std::move(e.value())), has_val_(false) {
  }

  constexpr bool has_value() const {
    return has_val_;
  }
  constexpr explicit operator bool() const {
    return has_val_;
  }
  constexpr void value() const {
  }
  constexpr const E& error() const& {
    return err_;
  }
  constexpr E& error() & {
    return err_;
  }

private:
  E err_;
  bool has_val_;
};

} // namespace std

#endif // VOX_COMMON_EXPECTED_POLYFILL_HPP
