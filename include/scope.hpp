#pragma once

#include <atomic>
#include <cstddef>
#include <stdexec/stop_token.hpp>
#include <stdexec/__detail/__just.hpp>
#include <stdexec/__detail/__let.hpp>
#include <stdexec/__detail/__then.hpp>
#include <exec/finally.hpp>

#include "amre.hpp"
#include "stop_when.hpp"

namespace stdexec {

  struct simple_counting_scope {
    struct token {
      template <sender Sender>
      Sender&& wrap(Sender&& snd) const noexcept {
        return std::forward<Sender>(snd);
      }

      bool try_associate() const {
        auto oldState = scope_->state_.load(std::memory_order_relaxed);
        std::size_t newState;

        do {
          if ((oldState & closed_) != 0u) {
            // the scope is closed to new work
            return false;
          }

          // increment the refcount with (oldState + 8u), and assert the
          // need to join with | needsJoin_
          newState = (oldState + 8u) | needsJoin_;

          // TODO: verify that relaxed is correct here; my logic is that
          //       this is a refcount increment, so there's no need to
          //       synchronize anything other than the new count, but
          //       maybe the "| needsJoin_" affects the logic somehow
        } while (
          !scope_->state_.compare_exchange_weak(oldState, newState, std::memory_order_relaxed));

        return true;
      }

      void disassociate() const {
        // decrement the refcount
        //
        // we need to ensure that this completion synchronizes with the eventual joiner, even if we're not the one
        // to wake it up so we need release semantics here
        auto prevState = scope_->state_.fetch_sub(8u, std::memory_order_release);

        // given some work just finished, the scope should require joining
        assert((prevState & needsJoin_) != 0u);

        if ((prevState >> 3) != 1u) {
          // we didn't just drop the refcount from 1 to 0 so there's
          // nothing special to do here
          return;
        }

        // we did just drop the refcount from 1 to 0 so we need to check
        // for whether we need to event_.set()

        if ((prevState & (joining_ | closed_)) == 0u) {
          // the scope is still open to new work and there's no
          // join-sender waiting on us
          return;
        }

        // Either there's a join-sender waiting, the scoped is closed to new
        // work, or both.
        //  - If the current state is (joining_ && !closed_) then we need to
        //    *try* to close the scope and, on success, set the event to
        //    release the join-sender
        //  - Otherwise, if the current state is (closed_) then we need to
        //    just set the event so that a join-sender can complete
        //    (possibly inline).

        if ((prevState & closed_) == 0u) {
          // not closed; try to close before setting the event
          //
          // in this state, there's a join-sender waiting but the scope
          // hasn't been closed to new work so we may be racing with work
          // that's about to start, which will increment the refcount. If
          // we succeed in closing the scope with the refcount still at 0
          // then we can set the event to complete the join-sender; but if
          // more work starts and increments the refcount then we have to
          // wait for *it* (or some other work) to be the final operation.

          assert((prevState & joining_) != 0u);
          assert((prevState & needsJoin_) != 0u);

          std::size_t expected = joining_ | needsJoin_;
          if (!scope_->state_.compare_exchange_strong(
                expected, expected | closed_, std::memory_order_relaxed)) {
            // we failed to close the scope because the refcount was
            // incremented
            assert((expected >> 3) > 0u);
            return;
          }
        }

        // the refcount should be zero and the needsJoin_ and closed_ bits
        // should be true; the joining_ bit could be true or false
        assert((scope_->state_.load(std::memory_order_relaxed) | joining_) == 7u);

        scope_->event_.set();
      }

     private:
      friend struct simple_counting_scope;

      explicit token(simple_counting_scope* scope) noexcept
        : scope_(scope) {
      }

      simple_counting_scope* scope_;
    };

    simple_counting_scope() noexcept = default;

    // simple_counting_scope is immovable and uncopyable
    simple_counting_scope(simple_counting_scope&&) = delete;

    ~simple_counting_scope() {
      auto state = state_.load(std::memory_order_relaxed);

      // if we're being destroyed without ever having been used then state should be 0; otherwise, the join sender
      // has to have run to completion, which leaves us with all bits off except the closed_ bit
      if (state == 0u || state == closed_) {
        return;
      }

      std::terminate();
    };

    token get_token() noexcept {
      return token{this};
    }

    void close() noexcept {
      // TODO: not sure whether relaxed is right here
      state_.fetch_or(closed_, std::memory_order_relaxed);
    }

    sender auto join() noexcept {
      return just(this) | let_value([](auto* self) {
               // we need to synchronize with any work that ran in this scope; we'll do that by turning off the
               // joining_ bit with acquire semantics after waiting for the event to be set

               // relaxed is ok because we don't need to synchronize with anything yet.
               auto state = self->state_.fetch_or(joining_, std::memory_order_relaxed);
               assert((state & joining_) == 0u);

               if ((state >> 3) == 0u) {
                 // no outstanding work so we may be able to complete inline
                 if ((state & closed_) != 0u) {
                   // we're joining, we're closed, and there's no outstanding work
                   self->event_.set();
                 } else {
                   // we expect the state to have the joining_ bit set because we just set it
                   state |= joining_;

                   // try to move to the terminal state
                   if (self->state_.compare_exchange_strong(
                         state, closed_ | joining_, std::memory_order_relaxed)) {
                     self->event_.set();
                   }
                 }
               }

               return self->event_.async_wait()
                    | ::exec::finally(just(self) | then([](auto* self) noexcept {
                                        // this load-acquire is what ensures the joiner sees all the store-releases
                                        // made by scoped work as it finishes
                                        [[maybe_unused]]
                                        auto state = self->state_.fetch_and(
                                          ~joining_, std::memory_order_acquire);
                                        assert(state == (closed_ | joining_));
                                      }));
             });
    }

   private:
    // flags stored in the low bits of state_
    static constexpr std::size_t needsJoin_{1u};
    static constexpr std::size_t joining_{2u};
    static constexpr std::size_t closed_{4u};

    std::atomic<std::size_t> state_{0u};
    amre event_;
  };

  struct counting_scope {
    struct token {
      template <sender Sender>
      sender auto wrap(Sender&& snd) {
        return stop_when(static_cast<Sender&&>(snd), scope_->stopSource_.get_token());
      }

      bool try_associate() const {
        return scope_->scope_.get_token().try_associate();
      }

      void disassociate() const {
        scope_->scope_.get_token().disassociate();
      }

     private:
      friend struct counting_scope;

      explicit token(counting_scope* scope) noexcept
        : scope_(scope) {
      }

      counting_scope* scope_;
    };

    token get_token() noexcept {
      return token{this};
    }

    void close() noexcept {
      scope_.close();
    }

    void request_stop() noexcept {
      stopSource_.request_stop();
    }

    sender auto join() noexcept {
      return scope_.join();
    }

   private:
    simple_counting_scope scope_;
    inplace_stop_source stopSource_;
  };

} // namespace stdexec
