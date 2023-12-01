# `co_try` - Rust `try!` macro in C++
`co_try` lets you use `std::expected` similar to how you'd use `Result` in Rust. Simply include the header and write:
```c++
#include <cotry/cotry.hpp>

std::expected<image, fail_reason> get_cute_cat(const image& img) {
    auto cropped = co_try crop_to_cat(img);
    auto with_tie = co_try add_bow_tie(cropped);
    auto with_sparkles = co_try make_eyes_sparkle(with_tie);
    co_return add_rainbox(make_smaller(with_sparkles));
}
```
instead of
```c++
std::expected<image,fail_reason> get_cute_cat (const image& img) {
    auto cropped = crop_to_cat(img);
    if (!cropped) {
      return cropped;
    }

    auto with_tie = add_bow_tie(*cropped);
    if (!with_tie) {
      return with_tie;
    }

    auto with_sparkles = make_eyes_sparkle(*with_tie);
    if (!with_sparkles) {
       return with_sparkles;
    }

    return add_rainbow(make_smaller(*with_sparkles));
}
```

## Motivation
Exceptions have the drawback of reporting errors *implicitly*. By looking at the signature of a function, it is not possible to know what exceptions it may throw. 
C++23 introduces `std::expected`, which is considered by many to be a better alternative to exceptions. 

With `std::expected`, whether a function returns an error or not is declared explicitly:

```c++
std::expected<image,fail_reason> get_cute_cat (const image& img);
```

Unfortunately, code written using `std::expected` can be clunky:

```c++
std::expected<image,fail_reason> get_cute_cat (const image& img) {
    auto cropped = crop_to_cat(img);
    if (!cropped) {
      return cropped;
    }

    auto with_tie = add_bow_tie(*cropped);
    if (!with_tie) {
      return with_tie;
    }

    auto with_sparkles = make_eyes_sparkle(*with_tie);
    if (!with_sparkles) {
       return with_sparkles;
    }

    return add_rainbow(make_smaller(*with_sparkles));
}
```

Using monad-like functions is better, but still not great:

```c++
tl::expected<image,fail_reason> get_cute_cat (const image& img) {
    return crop_to_cat(img)
           .and_then(add_bow_tie)
           .and_then(make_eyes_sparkle)
           .map(make_smaller)
           .map(add_rainbow);
}
```

Rust doesn't have exceptions, but has a `Result` type similar to `std::expected`. Rust code using `Result` is much cleaner, thanks to the `try!()` macro (or equivalently the `?` operator):

```rust
fn get_cute_cat(img: &Image) -> Result<Image, FailReason> {
    let cropped = crop_to_cat(img)?;
    let with_tie = add_bow_tie(cropped)?;
    let with_sparkles = make_eyes_sparkle(with_tie)?;
    Ok(add_rainbow(make_smaller(with_sparkles)))
}
```
`crop_to_cat` returns a `Result` and the `?` operator tells that execution of the function should continue only if `crop_to_cat` returns `Ok`. Otherwise, execution jumps directly to the end of the function.

`co_try` allows the same behavior in C++:

```c++
auto get_cute_cat(const Image& img) -> std::expected<Image, FailReason> {
    auto cropped = co_try crop_to_cat(img);
    auto with_tie = co_try add_bow_tie(cropped);
    auto with_sparkles = co_try make_eyes_sparkle(with_tie);
    co_return add_rainbow(make_smaller(with_sparkles));
}
```

## Implementation
`co_try` is implemented using coroutines. In fact, `co_try` is nothing but `co_await` in disguise:
```c++
#define co_try co_await
```
`co_try` is a proof of concept of how coroutines provide enough customization points on the control flow of functions to allow the implementation of feature beyond the original use case they were intended for. 

The idea is not new. See in particular the [coroutine_monad](https://github.com/toby-allsopp/coroutine_monad) repo from Toby Allsopp and the excellent [Cppcon talk](https://www.youtube.com/watch?v=mlP1MKP8d_Q).

This project differs from the original `coroutine_monad` in a couple of ways:
- it aims to be practical. A couple of decisions have been taken, sacrificing generality in favor of convenience.
- it exploits the improvements to the language (especially concepts) to provide a clearer implementation.
- it doesn't use any experimental feature, because all the required features are now part of the standard (in turn, it requires an aggressively modern compiler with C++23 support).

## How to use
`co_try` supports `std::optional` out of the box and `std::expected` (almost) out of the box, but can be used with any type `T`, provided that the `cotry::MaybeTrait<T>` trait is implemented. The `cotry::MaybeTrait<T>` must define the following functions:
- `has_value(const T& maybe) -> bool` returns true if `maybe` contains a value
- `value(const T& maybe)` returns a reference to the contained object when `has_value()` is `true`
- `from_value(U&& value) -> T` constructs a new `T` from a value
- `from_exception(const std::exception_ptr& ptr) -> T` constructs a new `T` from an exception (more on this later).

### Handling exceptions
When you propagate errors explicitly, e.g. using `std::expected<T,E>`, you should return all errors only using `std::expected<T,E>`. A function that returns `std::expected<T,E>` should not also propagate exceptions.
In fact, functions that return `std::expected<T,E>` should be declared `noexcept`.
In practice, it is virtually impossible to ensure that a function never throws, unless, you wrap all its logic in a `try/catch(...)` block.
With `co_try` however, this is really easy. In fact, `co_try` won't allow you to propagate exceptions. If an exception propagates within the fucntion (and is not explicitly caught), it will be passed to `MaybeTrait::from_exception`, which must package it into a return type. 

Since the library can't know how you want to transform `std::exception_ptr` to `E`, you must define that yourself even if you use `std::expected<T,E>`:
```c++
namespace cotry {
template <typename T>
struct MaybeTrait<std::expected<T, std::string>>
    : public MaybeTraitExpected<T, std::string> {

  static std::expected<T, std::string> from_exception(
      const std::exception_ptr& ptr) {
        return std::unexpected{"Unhandled exception!"};
      }
};
}  // namespace cotry

```

## Issues and limitations
`co_try` uses cutting-edge C++ features. It requires C++23 support and at the moment is not recommended for use in production.
Be aware especially of [this bug](https://github.com/llvm/llvm-project/issues/56532) in clang, that causes the library to behave incorrectly.
