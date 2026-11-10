// Engine addition (faf-neural-ai): what the native Linux client has in place of the wx tool windows it
// does not build (src/platform/sdl/sources.cmake, FAF_LINUX_CLIENT_EXCLUDE): the log window's target and
// the entity dialog command. The game's own log (gpg logging: the log file, stderr) is unchanged.

#include "moho/app/WxPrivate.h"

#include <atomic>

#include "gpg/core/utils/Logging.h"

// --- the log window's target (moho/app/WWinLogWindow.cpp on Windows) --------------------------------
// Without a log window nothing would ever merge the lines it queues, so the Linux client's target stays
// out of the logging dispatch (LogTarget(false)) and keeps nothing.
moho::CWinLogTarget::CWinLogTarget()
  : gpg::LogTarget(false)
{}

moho::CWinLogTarget::~CWinLogTarget() = default;

void moho::CWinLogTarget::AppendPendingLine(const CWinLogLine&) {}

std::size_t moho::CWinLogTarget::CommittedLineCount() const
{
  return mCommittedLines.size();
}

const fa::vector<moho::CWinLogLine>& moho::CWinLogTarget::CommittedLines() const
{
  return mCommittedLines;
}

void moho::CWinLogTarget::ReplaceCommittedLines(const fa::vector<CWinLogLine>& nextCommittedLines)
{
  mCommittedLines = nextCommittedLines;
}

void moho::CWinLogTarget::SnapshotCommittedLines(fa::vector<CWinLogLine>* const outLines)
{
  if (outLines != nullptr) {
    boost::mutex::scoped_lock scopedLock(lock);
    *outLines = mCommittedLines;
  }
}

void moho::CWinLogTarget::ResetCommittedLinesFromReplayBuffer(const fa::vector<fa::string>&) {}

void moho::CWinLogTarget::MergePendingLines() {}

void moho::CWinLogTarget::OnMessage(const gpg::LogSeverity, const fa::string&, const fa::vector<fa::string>&, int) {}

// --- the entity dialog (moho/app/WRefEditDialog.cpp on Windows) --------------------------------------
namespace moho
{
  void funcl_SC_CreateEntityDialog(); // moho/sim/Sim.h
}

void moho::funcl_SC_CreateEntityDialog()
{
  gpg::Warnf("SC_CreateEntityDialog: the entity dialog is a wx tool window the Linux client does not have");
}
