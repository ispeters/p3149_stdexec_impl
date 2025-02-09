#include <stdexec/stop_token.hpp>
#include <stdexec/__detail/__sync_wait.hpp>

#include "scope.hpp"

void unused_scope_destructible() {
  [[maybe_unused]]
  stdexec::simple_counting_scope scope;
}

void closed_unused_scope_destructible() {
  stdexec::simple_counting_scope scope;

  scope.close();
}

void empty_scope_joinable() {
  stdexec::simple_counting_scope scope;
  stdexec::sync_wait(scope.join());
}

void used_scope_joinable() {
  stdexec::simple_counting_scope scope;

  auto success = scope.get_token().try_associate();

  assert(success);

  if (success) {
    scope.get_token().disassociate();
  }

  stdexec::sync_wait(scope.join());
}

void closed_empty_scope_joinable() {
  stdexec::simple_counting_scope scope;

  scope.close();

  stdexec::sync_wait(scope.join());
}

int main() {
  unused_scope_destructible();
  closed_unused_scope_destructible();
  empty_scope_joinable();
  used_scope_joinable();
  closed_empty_scope_joinable();
}
