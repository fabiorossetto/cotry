#include <__concepts/same_as.h>
#include <__expected/unexpect.h>
#include <__expected/unexpected.h>
#include <cassert>
#include <coroutine>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>

namespace cotry {

template <typename T, typename E>
std::ostream& operator<<(std::ostream& out, std::expected<T, E>& expected) {
  if (expected.has_value()) {
    out << "Ok(" << expected.value() << ")";
  } else {
    out << "Err(" << expected.error() << ")";
  }
  return out;
}

template <typename MaybeT>
struct MaybeTrait;

template <typename MaybeT>
using ValueOf =
    std::decay_t<decltype(MaybeTrait<MaybeT>::value(std::declval<MaybeT&&>()))>;

template <typename MaybeT>
concept CotryMaybe = std::is_nothrow_move_assignable_v<MaybeT> &&
                     std::is_nothrow_move_constructible_v<MaybeT> &&
                     requires(const MaybeT& maybe) {
                       {
                         MaybeTrait<MaybeT>::has_value(maybe)
                       } -> std::same_as<bool>;
                     } &&
                     requires(
                         MaybeT&& maybe,
                         ValueOf<MaybeT>&& value,
                         const std::exception_ptr& ptr) {
                       { MaybeTrait<MaybeT>::value(std::move(maybe)) };
                       {
                         MaybeTrait<MaybeT>::from_value(std::move(value))
                       } -> std::same_as<MaybeT>;
                       {
                         MaybeTrait<MaybeT>::from_exception(ptr)
                       } -> std::same_as<MaybeT>;
                     };

template <CotryMaybe OutcomeT>
struct CotryTransportMaybe : public std::exception {
  explicit CotryTransportMaybe(OutcomeT&& outcome)
      : outcome{std::move(outcome)} {
  }

  OutcomeT outcome;
};

template <CotryMaybe OutcomeT>
class CotryPromise;

template <CotryMaybe OutcomeT>
class CotryReturnObject {
public:
  explicit CotryReturnObject(
      std::coroutine_handle<CotryPromise<OutcomeT>> handle)
      : handle_{handle} {
  }

  // Allow implicit conversion to OutcomeT
  // NOLINTNEXTLINE (google-explicit-constructor)
  operator OutcomeT() {
    std::cout << "Implicit conversion! " << handle_.promise().outcome()
              << std::endl;
    return handle_.promise().outcome();
  }

private:
  std::coroutine_handle<CotryPromise<OutcomeT>> handle_;
};

template <CotryMaybe OutcomeT>
class CotryAwaiter {
public:
  explicit CotryAwaiter(OutcomeT&& outcome)
      : outcome_{std::move(outcome)} {
  }

  bool await_ready() const noexcept {
    return true;
  }

  bool await_suspend(
      const std::coroutine_handle<CotryPromise<OutcomeT>>& /*coroutine*/) {
    return false;
  }

  ValueOf<OutcomeT> await_resume() {
    if (MaybeTrait<OutcomeT>::has_value(outcome_)) {
      return MaybeTrait<OutcomeT>::value(std::move(outcome_));
    }
    throw CotryTransportMaybe{std::move(outcome_)};
  }

private:
  OutcomeT outcome_;
};

template <CotryMaybe OutcomeT>
class CotryPromise {
public:
  CotryReturnObject<OutcomeT> get_return_object() {
    std::cout << "Get return object" << std::endl;
    return CotryReturnObject<OutcomeT>{
        std::coroutine_handle<CotryPromise<OutcomeT>>::from_promise(*this)};
  }

  std::suspend_never initial_suspend() {
    return {};
  }
  std::suspend_never final_suspend() noexcept {
    return {};
  }

  void return_value(ValueOf<OutcomeT>&& value) {
    std::cout << "return_value: " << value << std::endl;
    outcome_ = MaybeTrait<OutcomeT>::from_value(std::move(value));
  }

  void unhandled_exception() {
    std::cout << "unhandled_exception!" << std::endl;
    try {
      std::rethrow_exception(std::current_exception());
    } catch (CotryTransportMaybe<OutcomeT>& ex) {
      std::cout << "Ok transport outcome." << std::endl;
      outcome_ = std::move(ex.outcome);
    } catch (...) {
      std::cout << "Unknown exception." << std::endl;
      outcome_ = MaybeTrait<OutcomeT>::from_exception(std::current_exception());
    }
  }

  CotryAwaiter<OutcomeT> await_transform(OutcomeT& t) {
    std::cout << "Awaiting& " << t << std::endl;
    return CotryAwaiter<OutcomeT>{std::move(t)};
  }

  CotryAwaiter<OutcomeT> await_transform(OutcomeT&& t) {
    std::cout << "Awaiting&& " << t << std::endl;
    return CotryAwaiter<OutcomeT>{std::move(t)};
  }

  OutcomeT& outcome() {
    assert(outcome_.has_value());
    return outcome_.value();
  }

  const OutcomeT& outcome() const {
    assert(outcome_.has_value());
    return outcome_.value();
  }

private:
  std::optional<OutcomeT> outcome_;
};

template <typename E>
struct ExceptionConverter;

template <typename T>
struct MaybeTrait<std::optional<T>> {
  static T value(std::optional<T>&& optional) {
    return optional.value();
  }
  static bool has_value(std::optional<T>& optional) {
    return optional.has_value();
  }
  static std::optional<T> from_value(T&& value) {
    return std::move(value);
  }
  static std::optional<T> from_exception(
      const std::exception_ptr& /*exception*/) {
    return std::nullopt;
  }
};

template <typename T, typename E>
struct MaybeTrait<std::expected<T, E>> {
  static T value(std::expected<T, E>&& expected) {
    return expected.value();
  }
  static bool has_value(const std::expected<T, E>& expected) {
    return expected.has_value();
  }
  static std::expected<T, E> from_value(T&& value) {
    return std::move(value);
  }
  static std::expected<T, E> from_exception(const std::exception_ptr& ptr) {
    return std::unexpected(ExceptionConverter<E>::from_exception(ptr));
  }
};
}  // namespace cotry

namespace std {
template <cotry::CotryMaybe OutcomeT, typename... Args>
struct coroutine_traits<OutcomeT, Args...> {
  using promise_type = cotry::CotryPromise<OutcomeT>;
};

template <cotry::CotryMaybe OutcomeT, typename... Args>
struct coroutine_traits<cotry::CotryReturnObject<OutcomeT>, Args...> {
  using promise_type = cotry::CotryPromise<OutcomeT>;
};
}  // namespace std

#define co_try co_await
#define co_unwrap co_await
