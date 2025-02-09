#pragma once

// #include <stdexec/execution.hpp>
#include <atomic>
#include <cassert>
#include <stdexec/__detail/__run_loop.hpp>
#include <stdexec/__detail/__schedulers.hpp>
#include <stdexec/__detail/__senders.hpp>
#include <stdexec/concepts.hpp>

namespace stdexec {

  // copied from unifex::async_manual_reset_event
  struct amre {
    struct base_op {
      void (*complete_)(base_op*) noexcept;
    };

    template <receiver Receiver>
    struct wait_op;

    struct wait_sender;

    void set() noexcept {
      void* expected = nullptr;
      if (!state_.compare_exchange_strong(expected, this, std::memory_order_relaxed)) {
        // state_ must have been pointing to a base_op
        auto* op = static_cast<base_op*>(expected);
        op->complete_(op);
      }
    }

    [[nodiscard]]
    wait_sender async_wait() noexcept;

   private:
    // points to:
    //  - nullptr => unset, no waiter
    //  - this => set, no waiter
    //  - anything else => unset, state_ points at the active waiter
    std::atomic<void*> state_{nullptr};
  };

  template <receiver Receiver>
  struct amre::wait_op : base_op {
    struct rcvr {
      using receiver_concept = receiver_t;

      void set_value() noexcept {
        stdexec::set_value(std::move(*rcvr_));
      }

      template <typename E>
      void set_error(E&& e) noexcept {
        stdexec::set_error(std::move(*rcvr_), std::forward<E>(e));
      }

      void set_stopped() noexcept {
        stdexec::set_stopped(std::move(*rcvr_));
      }

      decltype(auto) get_env() const noexcept {
        return stdexec::get_env(*rcvr_);
      }

      Receiver* rcvr_;
    };

    using env_t = env_of_t<Receiver>;
    using sched_t = decltype(get_scheduler(std::declval<env_t>()));
    using sched_sender_t = decltype(schedule(std::declval<sched_t>()));
    using sched_op_t = connect_result_t<sched_sender_t, rcvr>;

    amre* event_;
    Receiver receiver_;
    sched_op_t op_;

    sched_op_t make_op() noexcept(
      noexcept(connect(schedule(get_scheduler(get_env(receiver_))), rcvr{&receiver_}))) {
      return connect(schedule(get_scheduler(get_env(receiver_))), rcvr{&receiver_});
    }

    template <typename R>
      requires std::constructible_from<Receiver, R>
    explicit wait_op(amre* event, R&& rcvr) noexcept(
      std::is_nothrow_constructible_v<Receiver, R> && noexcept(make_op()))
      : base_op{complete}
      , event_(event)
      , receiver_(std::forward<R>(rcvr))
      , op_(make_op()) {
    }

    wait_op(wait_op&&) = delete;

    ~wait_op() = default;

    using operation_state_concept = operation_state_t;

    void start() & noexcept {
      void* expected = nullptr;
      if (event_->state_.compare_exchange_strong(expected, this, std::memory_order_relaxed)) {
        // successfully updated state_ to point to this, so we're now
        // waiting
      } else {
        // either state_ was already set (so we should complete inline) or
        // we're the second waiter, which is UB
        assert(expected == event_);
        set_value(std::move(receiver_));
      }
    }

   private:
    static void complete(base_op* op) noexcept {
      auto& self = *static_cast<wait_op*>(op);

      stdexec::start(self.op_);
    }
  };

  struct amre::wait_sender {
    using sender_concept = sender_t;

    wait_sender(wait_sender&&) = default;
    ~wait_sender() = default;

    template <class Env>
      requires __scheduler_provider<Env>
    static constexpr auto get_completion_signatures(auto&&, Env&& e) {
      return stdexec::get_completion_signatures(schedule(get_scheduler(std::forward<Env>(e))));
    }

    template <receiver Receiver>
    wait_op<std::remove_cvref_t<Receiver>> connect(Receiver&& rcvr) && noexcept(
      std::is_nothrow_constructible_v<wait_op<std::remove_cvref_t<Receiver>>, amre*, Receiver>) {
      return wait_op<std::remove_cvref_t<Receiver>>{event_, std::forward<Receiver>(rcvr)};
    }

   private:
    friend struct amre;

    amre* event_;

    explicit wait_sender(amre* event) noexcept
      : event_(event) {
    }
  };

  inline auto amre::async_wait() noexcept -> wait_sender {
    return wait_sender{this};
  }

} // namespace stdexec
