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
  namespace __nest {
    template <async_scope_token _Token, sender _Sender>
    struct __nest_data {
      using __wrap_sender = __wrapped_sender_from<_Token, _Sender>;

      std::optional<__wrap_sender> __sndr_;
      _Token __tkn_;

      explicit __nest_data(_Token __tkn, _Sender __sndr)
        : __sndr_(__tkn.wrap(std::forward<_Sender>(__sndr)))
        , __tkn_(__tkn) {
        if (!__tkn_.try_associate()) {
          __sndr_.reset();
        }
      }

      __nest_data(const __nest_data& __other) noexcept(__nothrow_copy_constructible<__wrap_sender>)
        requires copy_constructible<__wrap_sender>
        : __tkn_(__other.__tkn_) {
        if (__tkn_.try_associate()) {
          __sndr_ = __other.__sndr_;
        }
      }

      __nest_data(__nest_data&& __other) noexcept(__nothrow_move_constructible<__wrap_sender>)
        : __sndr_(std::move(__other).__sndr_)
        , __tkn_(__other.__tkn_) {
        __other.__sndr_.reset();
      }

      ~__nest_data() {
        if (__sndr_.has_value()) {
          __sndr_.reset();
          __tkn_.disassociate();
        }
      }
    };

    template <async_scope_token Token, sender Sender>
    __nest_data(Token, Sender&&) -> __nest_data<Token, Sender>;

    template <class _Data>
    using __nested_sender = typename std::remove_reference_t<_Data>::__wrap_sender;

    struct nest_t {
      template <sender _Sender, async_scope_token _Token>
      auto operator()(_Sender&& __sndr, _Token __tkn) const {
        auto __domain = __get_early_domain(__sndr);
        return stdexec::transform_sender(
          __domain,
          __make_sexpr<nest_t>(
            __nest_data{static_cast<_Token&&>(__tkn), static_cast<_Sender&&>(__sndr)}));
      }

      template <async_scope_token _Token>
      STDEXEC_ATTRIBUTE((always_inline)) auto operator()(_Token __tok) const -> __binder_back<nest_t, _Token> {
        return {{static_cast<_Token&&>(__tok)}, {}, {}};
      }
    };

    struct __nest_impl : __sexpr_defaults {
      static constexpr auto get_completion_signatures = //
        []<class _Sender, class... _Env>(_Sender&&, _Env&&...) noexcept {
          using __wrapped_sender = __nested_sender<std::remove_reference_t<__data_of<_Sender>>>;
          using __csigs = transform_completion_signatures<
            completion_signatures_of_t<__wrapped_sender, _Env...>,
            completion_signatures<set_stopped_t()>>;

          return __csigs{};
        };

      static constexpr auto get_state = //
        []<class _Sender, class _Receiver>(
          _Sender&& __sndr,
          _Receiver& __rcvr) /* TODO: noexcept(see-below) */ {
          auto& __data =
            __sexpr_apply(__sndr, [](__ignore, auto& __data) -> auto& { return __data; });

          using __wrap_sender = __nested_sender<__data_of<_Sender>>;
          using __scope_token = decltype(__data.__tkn_);
          using __op_t = decltype(connect(__forward_like<_Sender>(__data.__sndr_.value()), __rcvr));

          struct __op_state {
            bool __associated_{false};
            __scope_token __tkn_;
            union {
              _Receiver* __rcvr_;
              __op_t __op_;
            };

            __op_state(__scope_token __tkn, _Receiver& __rcvr) noexcept
              : __tkn_(std::move(__tkn))
              , __rcvr_(&__rcvr) {
            }

            __op_state(__scope_token __tkn, std::optional<__wrap_sender>&& __sndr, _Receiver& __rcvr)
              : __associated_(true)
              , __tkn_(std::move(__tkn))
              , __op_(connect(std::move(__sndr).value(), std::move(__rcvr))) {
              // we've stolen the association so tag the __nest_data as not having one
              __sndr.reset();
            }

            __op_state(__scope_token __tkn, const __wrap_sender& __sndr, _Receiver& __rcvr)
              requires __same_as<
                         __op_t,
                         std::invoke_result_t<connect_t, const __wrap_sender&, _Receiver>>
              : __associated_(__tkn.try_associate())
              , __tkn_(std::move(__tkn))
              , __rcvr_(&__rcvr) {
              if (__associated_) {
                ::new (&__op_) __op_t{connect(__sndr.value(), std::move(__rcvr))};
              }
            }

            __op_state(__op_state&&) = delete;

            ~__op_state() {
              if (__associated_) {
                __op_.~__op_t();
                __tkn_.disassociate();
              }
            }

            void __start() & noexcept {
              if (__associated_) {
                ::stdexec::start(__op_);
              } else {
                ::stdexec::set_stopped(std::move(*__rcvr_));
              }
            }
          };

          if (__data.__sndr_.has_value()) {
            return __op_state{
              __forward_like<_Sender>(__data.__tkn_),
              __forward_like<_Sender>(__data.__sndr_),
              __rcvr};
          } else {
            return __op_state{__forward_like<_Sender>(__data.__tkn_), __rcvr};
          }
        };

      static constexpr auto start = //
        []<class _State>(_State& __state, __ignore) noexcept -> void {
        __state.__start();
      };
    };
  } // namespace __nest

  using __nest::nest_t;

  inline constexpr nest_t nest{};

  template <>
  struct __sexpr_impl<nest_t> : __nest::__nest_impl { };
} // namespace stdexec
