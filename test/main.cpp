#include "cotry/cotry.hpp"
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <system_error>
#include <variant>

template <typename T> using outcome = std::expected<T, std::string>;

namespace cotry {
template <> struct ExceptionConverter<std::string> {
  static std::string from(const std::exception_ptr &ptr) {
    try {
      std::rethrow_exception(ptr);
    } catch (std::exception &ex) {
      return ex.what();
    } catch (...) {
      return "unknown exception";
    }
  }
};
} // namespace cotry

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
  double v = co_try ferror();
  std::cout << "From f1: " << v << std::endl;
  co_return v * 2;
}

int f3() {
  std::cout << "f3" << std::endl;
  const outcome<int> v = f2();
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
