#include <__expected/unexpected.h>
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <system_error>

namespace cotry {

template <typename T, typename E>
std::ostream &operator<<(std::ostream &out, std::expected<T, E> &expected) {
  if (expected.has_value()) {
    out << "Ok(" << expected.value() << ")";
  } else {
    out << "Err(" << expected.error() << ")";
  }
  return out;
}

template <typename E> struct ExceptionConverter;

template <typename T, typename E>
class CotryTransportUnexpected : public std::exception {
public:
  CotryTransportUnexpected(std::expected<T, E> &&outcome)
      : outcome_{std::move(outcome)} {}

  std::expected<T, E> &outcome() { return outcome_; }

private:
  std::expected<T, E> outcome_;
};

template <typename T, typename E> class CotryPromise;

template <typename T, typename E> class CotryReturnObject {
public:
  CotryReturnObject(std::coroutine_handle<CotryPromise<T, E>> handle)
      : handle_{handle} {}

  operator std::expected<T, E>() {
    std::cout << "Implicit conversion! " << handle_.promise().outcome()
              << std::endl;
    return handle_.promise().outcome();
  }

private:
  std::coroutine_handle<CotryPromise<T, E>> handle_;
};

template <typename T, typename E> class CotryAwaiter {
public:
  CotryAwaiter(std::expected<T, E> &outcome) : outcome_{std::move(outcome)} {}
  CotryAwaiter(std::expected<T, E> &&outcome) : outcome_{std::move(outcome)} {}

  bool await_ready() const noexcept { return true; }
  T await_resume() {
    if (outcome_.has_value()) {
      return outcome_.value();
    }
    throw CotryTransportUnexpected{std::move(outcome_)};
  }

  bool await_suspend(std::coroutine_handle<CotryPromise<T, E>> &&coroutine) {
    return false;
  }

private:
  std::expected<T, E> outcome_;
};

template <typename T, typename E> class CotryPromise {
public:
  CotryReturnObject<T, E> get_return_object() {
    std::cout << "Get return object" << std::endl;
    return CotryReturnObject<T, E>{
        std::coroutine_handle<CotryPromise<T, E>>::from_promise(*this)};
  }

  std::suspend_never initial_suspend() { return {}; }
  std::suspend_never final_suspend() noexcept { return {}; }

  void return_value(T value) {
    std::cout << "return_value: " << value << std::endl;
    outcome_ = value;
  }

  void unhandled_exception() {
    std::cout << "unhandled_exception!" << std::endl;
    try {
      std::rethrow_exception(std::current_exception());
    } catch (CotryTransportUnexpected<T, E> &ex) {
      std::cout << "Ok transport outcome." << std::endl;
      outcome_ = std::move(ex.outcome());
    } catch (...) {
      std::cout << "Unknown exception." << std::endl;
      outcome_ = std::unexpected{
          cotry::ExceptionConverter<E>::from(std::current_exception())};
    }
  }

  CotryAwaiter<T, E> await_transform(std::expected<T, E> &t) {
    return CotryAwaiter<T, E>{std::move(t)};
  }
  CotryAwaiter<T, E> await_transform(std::expected<T, E> &&t) {
    return CotryAwaiter<T, E>{std::move(t)};
  }

  std::expected<T, E> &outcome() { return outcome_; }

private:
  std::expected<T, E> outcome_;
};
} // namespace cotry

namespace std {
template <typename T, typename E, typename... Args>
struct coroutine_traits<std::expected<T, E>, Args...> {
  using promise_type = cotry::CotryPromise<T, E>;
};

template <typename T, typename E, typename... Args>
struct coroutine_traits<cotry::CotryReturnObject<T, E>, Args...> {
  using promise_type = cotry::CotryPromise<T, E>;
};
} // namespace std

#define co_try co_await
