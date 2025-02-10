#pragma once

#include <stdexec/__detail/__execution_fwd.hpp>

#include <stdexec/__detail/__basic_sender.hpp>
#include <stdexec/__detail/__diagnostics.hpp>
#include <stdexec/__detail/__domain.hpp>
#include <stdexec/__detail/__meta.hpp>
#include <stdexec/__detail/__senders_core.hpp>
#include <stdexec/__detail/__sender_adaptor_closure.hpp>
#include <stdexec/__detail/__transform_completion_signatures.hpp>
#include <stdexec/__detail/__transform_sender.hpp>
#include <stdexec/__detail/__senders.hpp>
#include <stdexec/__detail/__stop_token.hpp>

#include "concepts.hpp"

namespace stdexec {
  namespace __spawn {
    struct __spawn_state_base {
      void __complete() noexcept {
        __complete_(this);
      }

      __spawn_state_base(void (*__complete)(__spawn_state_base*) noexcept) noexcept
        : __complete_(__complete) {
      }

      __spawn_state_base(__spawn_state_base&&) = delete;

     protected:
      ~__spawn_state_base() = default;

     private:
      void (*__complete_)(__spawn_state_base*) noexcept;
    };

    struct __spawn_receiver {
      using receiver_concept = receiver_t;

      __spawn_state_base* __state_;

      void set_value() noexcept {
        __state_->__complete();
      }

      void set_stopped() noexcept {
        __state_->__complete();
      }
    };

    template <class _Alloc, async_scope_token _Token, sender _Sender>
    struct __spawn_state : __spawn_state_base {
      using __op_t = decltype(connect(__declval<_Sender>(), __spawn_receiver{nullptr}));

      __spawn_state(_Alloc __alloc, _Sender&& __sndr, _Token __tkn)
        : __spawn_state_base{&__complete}
        , __alloc_(std::move(__alloc))
        , __op_(connect(std::move(__sndr), __spawn_receiver{this}))
        , __tkn_(std::move(__tkn)) {
      }

      void __run() {
        if (__tkn_.try_associate()) {
          __op_.start();
        } else {
          __complete(this);
        }
      }

     private:
      using __alloc_t =
        typename std::allocator_traits<_Alloc>::template rebind_alloc<__spawn_state>;

      __alloc_t __alloc_;
      __op_t __op_;
      _Token __tkn_;

      static void __complete(__spawn_state_base* __base) noexcept {
        auto* __self = static_cast<__spawn_state*>(__base);
        auto __tkn = std::move(__self->__tkn_);
        {
          auto __alloc = std::move(__self->__alloc_);

          std::allocator_traits<__alloc_t>::destroy(__alloc, __self);
          std::allocator_traits<__alloc_t>::deallocate(__alloc, __self, 1);
        }
        __tkn.disassociate();
      }
    };

    template <sender _Sender, class _Env>
    auto __choose_allocator_and_env(const _Sender& __sndr, const _Env& __env) {
      if constexpr (requires { ::stdexec::get_allocator(__env); }) {
        using __alloc_t = decltype(::stdexec::get_allocator(__env));
        return std::pair<__alloc_t, const _Env&>{::stdexec::get_allocator(__env), __env};
      } else if constexpr (requires { ::stdexec::get_allocator(::stdexec::get_env(__sndr)); }) {
        auto alloc = ::stdexec::get_allocator(::stdexec::get_env(__sndr));
        return std::pair{alloc, __env::__join(prop{get_allocator, std::move(alloc)}, __env)};
      } else {
        return std::pair<std::allocator<void>, const _Env&>{{}, __env};
      }
    }

    struct spawn_t {
      template <sender _Sender, async_scope_token _Token, class _Env = empty_env>
      void operator()(_Sender&& __sndr, _Token __tkn, _Env&& __env = {}) const {
        auto __newSndr = __tkn.wrap(static_cast<_Sender&&>(__sndr));
        auto&& [__alloc, __senv] = __choose_allocator_and_env(__newSndr, __env);

        auto __makeSender = [&]() {
          return __write_env(std::move(__newSndr), std::move(__senv));
        };

        using __sender_t = decltype(__makeSender());

        using __state_t = __spawn_state<__decay_t<decltype(__alloc)>, _Token, __sender_t>;
        using __alloc_t = typename std::allocator_traits<
          __decay_t<decltype(__alloc)>>::template rebind_alloc<__state_t>;
        using __traits_t = std::allocator_traits<__alloc_t>;

        __alloc_t __stateAlloc{__alloc};

        auto* __op = __traits_t::allocate(__stateAlloc, 1);

        try {
          __traits_t::construct(__stateAlloc, __op, __alloc, __makeSender(), std::move(__tkn));
        } catch (...) {
          __traits_t::deallocate(__stateAlloc, __op, 1);
          throw;
        }

        try {
          __op->__run();
        } catch (...) {
          __traits_t::destroy(__stateAlloc, __op);
          __traits_t::deallocate(__stateAlloc, __op, 1);
          throw;
        }
      }
    };
  } // namespace __spawn

  using __spawn::spawn_t;

  inline constexpr spawn_t spawn{};
} // namespace stdexec
