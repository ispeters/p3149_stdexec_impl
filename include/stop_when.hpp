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

namespace stdexec {
  namespace __stop_when {
    struct __on_stop_request {
      inplace_stop_source& __stop_source_;

      void operator()() noexcept {
        __stop_source_.request_stop();
      }
    };

    template <class _Env>
    auto __mkenv(_Env&& __env, const inplace_stop_source& __stop_source) noexcept {
      return __env::__join(
        prop{get_stop_token, __stop_source.get_token()}, static_cast<_Env&&>(__env));
    }

    template <class _Env, class...>
    using __env_t = //
      decltype(__stop_when::__mkenv(__declval<_Env>(), __declval<inplace_stop_source&>()));

    struct stop_when_t {
      template <sender _Sender, stoppable_token _Token>
      auto operator()(_Sender&& __sndr, _Token __tok) const -> __well_formed_sender auto {
        auto __domain = __get_early_domain(__sndr);
        return stdexec::transform_sender(
          __domain,
          __make_sexpr<stop_when_t>(static_cast<_Token&&>(__tok), static_cast<_Sender&&>(__sndr)));
      }

      template <stoppable_token _Token>
      STDEXEC_ATTRIBUTE((always_inline)) auto operator()(_Token __tok) const -> __binder_back<stop_when_t, _Token> {
        return {{static_cast<_Token&&>(__tok)}, {}, {}};
      }
    };

    struct __stop_when_impl : __sexpr_defaults {
      static constexpr auto get_completion_signatures = //
        []<class _Sender, class... _Env>(_Sender&&, _Env&&...) noexcept
        -> __completion_signatures_of_t<__child_of<_Sender>, __env_t<_Env, empty_env>...> {
        static_assert(sender_expr_for<_Sender, stop_when_t>);
        return {};
      };

      template <stoppable_token _GivenToken, stoppable_token _RcvrToken>
      struct __state {
        /* implicit */ __state(_GivenToken __given, _RcvrToken __rcvr)
          : __givenToken_(__given)
          , __rcvrToken_(__rcvr) {
        }

        ~__state() {
          assert(!__givenCallback_);
          assert(!__rcvrCallback_);
        }

        void __init_callbacks() {
          assert(!__givenCallback_);
          assert(!__rcvrCallback_);
          __givenCallback_.emplace(__givenToken_, __on_stop_request{__stopSource_});
          __rcvrCallback_.emplace(__rcvrToken_, __on_stop_request{__stopSource_});
        }

        void __deinit_callbacks() {
          assert(__rcvrCallback_);
          assert(__givenCallback_);
          __rcvrCallback_.reset();
          __givenCallback_.reset();
        }

        using _GivenCallback = stop_callback_for_t<_GivenToken, __on_stop_request>;
        using _RcvrCallback = stop_callback_for_t<_RcvrToken, __on_stop_request>;

        _GivenToken __givenToken_;
        _RcvrToken __rcvrToken_;
        inplace_stop_source __stopSource_;
        std::optional<_GivenCallback> __givenCallback_;
        std::optional<_RcvrCallback> __rcvrCallback_;
      };

      template <class _Self>
      using __given_token_t = std::remove_reference_t<__data_of<_Self>>;

      template <receiver _Receiver>
      using __rcvr_token_t = ::stdexec::stop_token_of_t<::stdexec::env_of_t<_Receiver>>;

      static constexpr auto get_state = //
        []<class _Self, class _Receiver>(_Self&& __self, _Receiver& __rcvr)
        -> __state<__given_token_t<_Self>, __rcvr_token_t<_Receiver>> {
        __given_token_t<_Self> __givenToken =
          __sexpr_apply(__self, [](__ignore, auto __token, __ignore) { return __token; });
        __rcvr_token_t<_Receiver> __rcvrToken =
          ::stdexec::get_stop_token(::stdexec::get_env(__rcvr));

        return {__givenToken, __rcvrToken};
      };

      static constexpr auto start = //
        []<class _State, class _Receiver, class... _Operations>(
          _State& __state,
          _Receiver& __rcvr,
          _Operations&... __child_ops) noexcept -> void {
        static_assert(sizeof...(_Operations) == 1);

        __state.__init_callbacks();
        (::stdexec::start(__child_ops), ...);
      };

      static constexpr auto complete = //
        []<class _State, class _Receiver, class _SetTag, class... _Args>(
          __ignore,
          _State& __state,
          _Receiver& __rcvr,
          _SetTag,
          _Args&&... __args) noexcept -> void {
        __state.__deinit_callbacks();
        _SetTag()(std::move(__rcvr), static_cast<_Args&&>(__args)...);
      };
    };
  } // namespace __stop_when

  using __stop_when::stop_when_t;

  inline constexpr stop_when_t stop_when{};

  template <>
  struct __sexpr_impl<stop_when_t> : __stop_when::__stop_when_impl { };
} // namespace stdexec
