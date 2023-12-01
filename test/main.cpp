#include "cotry/cotry.hpp"
#include <__expected/expected.h>
#include <cassert>
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <system_error>
#include <variant>

template <typename T>
using outcome = std::expected<T, std::string>;

namespace cotry {
template <typename T>
struct MaybeTrait<std::expected<T, std::string>>
    : public MaybeTraitExpected<T, std::string> {

  static std::expected<T, std::string> from_exception(
      const std::exception_ptr& ptr);
};
}  // namespace cotry

void f() {
  cotry::MaybeTrait<std::expected<int, std::string>>::from_value(4);
}

static_assert(cotry::CotryMaybe<outcome<int>>);

// static_assert(cotry::CotryMonad<outcome<int>>);

// namespace cotry {
// template <>
// struct ExceptionConverter<std::string> {
//   static std::string from_exception(const std::exception_ptr& ptr) {
//     try {
//       std::rethrow_exception(ptr);
//     } catch (std::exception& ex) {
//       return ex.what();
//     } catch (...) {
//       return "unknown exception";
//     }
//   }
// };
// }  // namespace cotry

outcome<int> f1() {
  std::cout << "f1" << std::endl;
  return 8;
}

outcome<int> ferror() {
  std::cout << "ferror" << std::endl;
  return std::unexpected{"ERROR!"};
}

outcome<int> f2() {
  std::cout << "f2" << std::endl;
  double v = co_try f1();
  std::cout << "From f1: " << v << std::endl;
  co_return v * 2;
}

int f3() {
  std::cout << "f3" << std::endl;
  const outcome<int> v = f2();
  std::cout << "Got value in f3: " << v.value() << std::endl;
  if (!v.has_value()) {
    std::cout << "Error: " << v.error() << std::endl;
  }
  return v.value_or(-1);
}

int main() {
  int v = f3();

  std::cout << "Value: " << v << std::endl;

  return 0;
}
