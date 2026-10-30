// Engine addition, not recovered from the binary: the /netdiag switch.
//
// Client-only (sim_sources.cmake lists it with the user world): it reads the user session's
// driver through WLD_GetDriver, which programs without the client (headless, the server) lack.
#include <Windows.h>

#include <thread>

#include "moho/misc/StartupHelpers.h"
#include "moho/net/CClientManagerImpl.h"
#include "moho/sim/CWldSession.h"
#include "moho/sim/ISTIDriver.h"

namespace moho
{
  namespace
  {
    // Diagnostics (engine addition): with /netdiag, dump the client manager's beat bookkeeping (queued,
    // dispatched and available beats per client, readiness) to the log every five seconds -- for
    // finding out why a session (e.g. a replay) stops dispatching beats.
    struct NetDiagnosticsStarter
    {
      NetDiagnosticsStarter()
      {
        if (!CFG_GetArgOption("/netdiag", 0, nullptr)) {
          return;
        }
        std::thread([] {
          for (;;) {
            ::Sleep(5000);
            if (ISTIDriver* const driver = WLD_GetDriver(); driver != nullptr) {
              if (CClientManagerImpl* const clientManager = driver->GetClientManager(); clientManager != nullptr) {
                clientManager->Debug();
              }
            }
          }
        }).detach();
      }
    };
    const NetDiagnosticsStarter gNetDiagnosticsStarter{};
  } // namespace
} // namespace moho
