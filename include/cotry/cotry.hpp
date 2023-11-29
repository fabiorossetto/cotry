#include <__concepts/same_as.h>
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

template <typename E> struct ExceptionConverter;

template <typename MonadT> struct MonadTrait;

template <typename MonadT>
using MonadInnerValue =
    decltype(MonadTrait<MonadT>::value(std::declval<MonadT &>()));

template <typename MonadT>
concept Monad = std::is_nothrow_move_assignable_v<MonadT> &&
                std::is_nothrow_move_constructible_v<MonadT> &&
                requires(MonadT &monad) {
                  { MonadTrait<MonadT>::value(monad) };
                } && [] {
                  using T = MonadInnerValue<MonadT>;
                  return requires(MonadT & monad, T && value,
                                  std::exception_ptr && ptr) {
                           {
                             MonadTrait<MonadT>::value(monad)
                           } -> std::same_as<T &>;
                           {
                             MonadTrait<MonadT>::has_value(monad)
                           } -> std::same_as<bool>;
                           {
                             MonadTrait<MonadT>::from_value(std::move(value))
                           } -> std::same_as<MonadT>;
                           {
                             MonadTrait<MonadT>::from_exception(std::move(ptr))
                           } -> std::same_as<MonadT>;
                         };
                }();

template <typename T> struct MonadTrait<std::optional<T>> {
  static T &value(std::optional<T> &monad) { return monad.value(); }
  static bool has_value(std::optional<T> &monad) { return monad.has_value(); }
  static std::optional<T> from_value(T &&value) { return std::move(value); }
  static std::optional<T> from_exception(std::exception_ptr) {
    return std::nullopt;
  }
};

template <typename T, typename E> struct MonadTrait<std::expected<T, E>> {
  static T &value(std::expected<T, E> &monad) { return monad.value(); }
  static bool has_value(std::expected<T, E> &monad) {
    return monad.has_value();
  }
  static std::expected<T, E> from_value(T &&value) { return std::move(value); }
  static std::expected<T, E> from_exception(const std::exception_ptr &ptr) {
    return ExceptionConverter<E>::from_exception(ptr);
  }
};

// static_assert(Monad<std::optional<int>>);

// template <typename MonadT>
// concept SimpleMonad = requires(MonadT &monad) {
//                         { monad.value() };
//                         { monad.has_value() } -> std::same_as<bool>;
//                       } && [] {
//                         using T = decltype(std::declval<MonadT &>().value());
//                         return requires(MonadT & monad, T && value,
//                                         std::exception_ptr && ptr) {
//                                  { monad.value() } -> std::same_as<T &>;
//                                  //  {
//                                  //    MonadT{std::move(value)}
//                                  //  } -> std::same_as<MonadT>;
//                                  //  {
//                                  //    MonadTrait<MonadT>::from_exception(
//                                  //        std::move(ptr))
//                                  //  } -> std::same_as<MonadT>;
//                                };
//                       }();

// static_assert(SimpleMonad<std::optional<int>>);
// static_assert(SimpleMonad<std::unique_ptr<int>>);

// template <typename MonadT> struct MonadTrait<MonadT> {};

template <typename T, typename E>
std::ostream &operator<<(std::ostream &out, std::expected<T, E> &expected) {
  if (expected.has_value()) {
    out << "Ok(" << expected.value() << ")";
  } else {
    out << "Err(" << expected.error() << ")";
  }
  return out;
}

template <Monad MonadT> class CotryTransportUnexpected : public std::exception {
public:
  CotryTransportUnexpected(MonadT &&outcome) : outcome_{std::move(outcome)} {}

  MonadT &outcome() { return outcome_; }

private:
  MonadT outcome_;
};

template <Monad MonadT> class CotryPromise;

template <Monad MonadT> class CotryReturnObject {
public:
  CotryReturnObject(std::coroutine_handle<CotryPromise<MonadT>> handle)
      : handle_{handle} {}

  operator MonadT() {
    std::cout << "Implicit conversion! " << handle_.promise().outcome()
              << std::endl;
    return handle_.promise().outcome();
  }

private:
  std::coroutine_handle<CotryPromise<MonadT>> handle_;
};

template <Monad MonadT> class CotryAwaiter {
public:
  CotryAwaiter(MonadT &outcome) : outcome_{std::move(outcome)} {}
  CotryAwaiter(MonadT &&outcome) : outcome_{std::move(outcome)} {}

  bool await_ready() const noexcept { return true; }
  MonadT await_resume() {
    if (MonadTrait<MonadT>::has_value(outcome_)) {
      return MonadTrait<MonadT>::value(outcome_);
    }
    throw CotryTransportUnexpected{std::move(outcome_)};
  }

  bool await_suspend(std::coroutine_handle<CotryPromise<MonadT>> &&coroutine) {
    return false;
  }

private:
  MonadT outcome_;
};

template <Monad MonadT> class CotryPromise {
public:
  CotryReturnObject<MonadT> get_return_object() {
    std::cout << "Get return object" << std::endl;
    return CotryReturnObject<MonadT>{
        std::coroutine_handle<CotryPromise<MonadT>>::from_promise(*this)};
  }

  std::suspend_never initial_suspend() { return {}; }
  std::suspend_never final_suspend() noexcept { return {}; }

  void return_value(MonadInnerValue<MonadT> value) {
    std::cout << "return_value: " << value << std::endl;
    outcome_ = MonadTrait<MonadT>::from_value(std::move(value));
  }

  void unhandled_exception() {
    std::cout << "unhandled_exception!" << std::endl;
    try {
      std::rethrow_exception(std::current_exception());
    } catch (CotryTransportUnexpected<MonadT> &ex) {
      std::cout << "Ok transport outcome." << std::endl;
      outcome_ = std::move(ex.outcome());
    } catch (...) {
      std::cout << "Unknown exception." << std::endl;
      outcome_ = MonadTrait<MonadT>::from_exception(std::current_exception());
    }
  }

  CotryAwaiter<MonadT> await_transform(MonadT &t) {
    return CotryAwaiter<MonadT>{std::move(t)};
  }
  CotryAwaiter<MonadT> await_transform(MonadT &&t) {
    return CotryAwaiter<MonadT>{std::move(t)};
  }

  MonadT &outcome() { return outcome_; }

private:
  MonadT outcome_;
};
} // namespace cotry

namespace std {
template <cotry::Monad MonadT, typename... Args>
struct coroutine_traits<MonadT, Args...> {
  using promise_type = cotry::CotryPromise<MonadT>;
};

template <cotry::Monad MonadT, typename... Args>
struct coroutine_traits<cotry::CotryReturnObject<MonadT>, Args...> {
  using promise_type = cotry::CotryPromise<MonadT>;
};
} // namespace std

#define co_try co_await
#define co_unwrap co_await
