#include <stdexec/stop_token.hpp>
#include <stdexec/__detail/__sync_wait.hpp>
#include <stdexec/__detail/__just.hpp>
#include <type_traits>

#include "scope.hpp"
#include "nest.hpp"

namespace {
  template <bool Simple>
  using scope_t =
    std::conditional_t<Simple, stdexec::simple_counting_scope, stdexec::counting_scope>;

  template <bool Simple>
  void unused_scope_destructible() {
    [[maybe_unused]]
    scope_t<Simple> scope;
  }

  template <bool Simple>
  void closed_unused_scope_destructible() {
    scope_t<Simple> scope;

    scope.close();
  }

  template <bool Simple>
  void empty_scope_joinable() {
    scope_t<Simple> scope;
    stdexec::sync_wait(scope.join());
  }

  template <bool Simple>
  void used_scope_joinable() {
    scope_t<Simple> scope;

    auto success = scope.get_token().try_associate();

    assert(success);

    if (success) {
      scope.get_token().disassociate();
    }

    stdexec::sync_wait(scope.join());
  }

  template <bool Simple>
  void closed_empty_scope_joinable() {
    scope_t<Simple> scope;

    scope.close();

    stdexec::sync_wait(scope.join());
  }

  template <bool Simple>
  void wrapped_just_runs() {
    scope_t<Simple> scope;

    auto token = scope.get_token();

    auto snd = token.wrap(stdexec::just());

    static_assert(stdexec::sender<decltype(snd)>);
    static_assert(stdexec::sender_in<decltype(snd), stdexec::empty_env>);

    stdexec::sync_wait(std::move(snd));
  }

  template <bool Simple>
  void can_nest_just() {
    scope_t<Simple> scope;

    auto s = stdexec::nest(stdexec::just(), scope.get_token());

    using sndr_t = decltype(s);
    using csigs = decltype(stdexec::get_completion_signatures(s, stdexec::empty_env{}));

    static_assert(stdexec::sender<sndr_t>);
    static_assert(stdexec::sender_in<sndr_t, stdexec::empty_env>);

    stdexec::sync_wait(std::move(s));
    stdexec::sync_wait(scope.join());
  }

  template <bool Simple>
  void run_examples() {
    unused_scope_destructible<Simple>();
    closed_unused_scope_destructible<Simple>();
    empty_scope_joinable<Simple>();
    used_scope_joinable<Simple>();
    closed_empty_scope_joinable<Simple>();
    wrapped_just_runs<Simple>();
    can_nest_just<Simple>();
  }
} // namespace

static_assert(stdexec::async_scope_token<stdexec::simple_counting_scope::token>);
static_assert(stdexec::async_scope_token<stdexec::counting_scope::token>);

int main() {
  run_examples<true>();
  run_examples<false>();
}
