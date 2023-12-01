#include <__concepts/same_as.h>
#include <__expected/unexpect.h>
#include <__expected/unexpected.h>
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

template <typename MonadT>
struct MonadTrait;

template <typename MonadT>
using ValueOf =
    std::decay_t<decltype(MonadTrait<MonadT>::value(std::declval<MonadT&&>()))>;

template <typename MonadT>
concept CotryMonad = std::is_nothrow_move_assignable_v<MonadT> &&
                     std::is_nothrow_move_constructible_v<MonadT> &&
                     requires(const MonadT& monad) {
                       {
                         MonadTrait<MonadT>::has_value(monad)
                       } -> std::same_as<bool>;
                     } &&
                     requires(
                         MonadT&& monad,
                         ValueOf<MonadT>&& value,
                         const std::exception_ptr& ptr) {
                       { MonadTrait<MonadT>::value(std::move(monad)) };
                       {
                         MonadTrait<MonadT>::from_value(std::move(value))
                       } -> std::same_as<MonadT>;
                       {
                         MonadTrait<MonadT>::from_exception(ptr)
                       } -> std::same_as<MonadT>;
                     };

template <CotryMonad OutcomeT>
struct CotryTransportMonad : public std::exception {
  explicit CotryTransportMonad(OutcomeT&& outcome)
      : outcome{std::move(outcome)} {
  }

  OutcomeT outcome;
};

template <CotryMonad OutcomeT>
class CotryPromise;

template <CotryMonad OutcomeT>
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

template <CotryMonad OutcomeT>
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
    if (MonadTrait<OutcomeT>::has_value(outcome_)) {
      return MonadTrait<OutcomeT>::value(std::move(outcome_));
    }
    throw CotryTransportMonad{std::move(outcome_)};
  }

private:
  OutcomeT outcome_;
};

template <CotryMonad OutcomeT>
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
    outcome_ = MonadTrait<OutcomeT>::from_value(std::move(value));
  }

  void unhandled_exception() {
    std::cout << "unhandled_exception!" << std::endl;
    try {
      std::rethrow_exception(std::current_exception());
    } catch (CotryTransportMonad<OutcomeT>& ex) {
      std::cout << "Ok transport outcome." << std::endl;
      outcome_ = std::move(ex.outcome);
    } catch (...) {
      std::cout << "Unknown exception." << std::endl;
      outcome_ = MonadTrait<OutcomeT>::from_exception(std::current_exception());
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
    return outcome_;
  }

  const OutcomeT& outcome() const {
    return outcome_;
  }

private:
  OutcomeT outcome_;
};

template <typename E>
struct ExceptionConverter;

template <typename T>
struct MonadTrait<std::optional<T>> {
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
struct MonadTrait<std::expected<T, E>> {
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
template <cotry::CotryMonad OutcomeT, typename... Args>
struct coroutine_traits<OutcomeT, Args...> {
  using promise_type = cotry::CotryPromise<OutcomeT>;
};

template <cotry::CotryMonad OutcomeT, typename... Args>
struct coroutine_traits<cotry::CotryReturnObject<OutcomeT>, Args...> {
  using promise_type = cotry::CotryPromise<OutcomeT>;
};
}  // namespace std

#define co_try co_await
#define co_unwrap co_await
