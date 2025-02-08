#include <stdexec/stop_token.hpp>
#include <stdexec/__detail/__sync_wait.hpp>

//#include <stdexec/execution.hpp>

#include "amre.hpp"
#include "scope.hpp"

int main() {
  stdexec::amre event;

  event.set();

  stdexec::sync_wait(event.async_wait());
}
