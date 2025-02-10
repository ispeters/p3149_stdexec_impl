#pragma once

#include <stdexec/__detail/__execution_fwd.hpp>

#include <stdexec/__detail/__concepts.hpp>
#include <stdexec/__detail/__type_traits.hpp>

namespace stdexec {
  // [exec.scope.concepts]
  template <class Token>
  concept async_scope_token = copyable<Token> && requires(Token token) {
    { token.try_associate() } -> same_as<bool>;
    { token.disassociate() } -> same_as<void>;
  };

  // [exec.scope.expos]
  template <async_scope_token _Token, sender _Sender>
  using __wrapped_sender_from = __decay_t<decltype(__declval<_Token&>().wrap(__declval<_Sender>()))>;
}; // namespace stdexec
