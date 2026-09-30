#pragma once

namespace moho
{
  class Sim;

  /**
   * Engine addition (not in the original binary): the agent bridge.
   *
   * Lets an external process -- a reinforcement-learning trainer, a neural-network policy, a scripted
   * test agent -- observe the sim and command armies in lock-step with it. Everything here is inert
   * unless the game is launched with `/agentbridge <port>`.
   *
   * Command line:
   *   /agentbridge <port>    connect to 127.0.0.1:<port> (the agent listens; the game connects)
   *   /agentscript <file>    sim-side Lua that builds observations and applies actions (required)
   *   /agentspeed <rate>     game-speed rate the sim is paced to while the bridge is on; the default
   *                          of 50 is effectively unpaced (only the agent and the CPU limit it),
   *                          10 is the fastest normal game speed and 0 is real time
   *   /agenttimeout <secs>   how long to keep retrying the initial connection (default 60)
   *   /agentvfx              keep particle effects while unpaced (skipped by default then)
   *   /agenttraining         training mode: skip sim work only a watching human needs (see
   *                          AGENT_TrainingMode); for minimized training games
   *   /agentlean             skip loading work that only feeds the renderer (strategic-icon textures,
   *                          searches for implicit mesh material textures); for training games that are
   *                          never drawn
   *   /agentwatchdog <secs>  if the sim thread makes no progress for this long, log the Lua
   *                          traceback, send it to the agent and exit (default 60, 0 disables)
   *   /agentchecksum <n>     the sim checksums every n beats instead of every 50 (1: every beat), so
   *                          the "sums" of each message cover each beat (determinism checks)
   *   /agentpoison <byte>    diagnostics: fill every new heap block with this byte, to expose reads of
   *                          uninitialised memory (two runs with different bytes differ where one matters)
   *
   * Protocol: newline-delimited JSON over TCP. At the end of every sim tick that is due, the game
   * sends one message and blocks the sim thread until the agent answers with one message:
   *
   *   game  -> agent  {"type":"hello","protocol":1,"tick":T,"info":<AgentBridge_Hello()>}   (first)
   *   game  -> agent  {"type":"obs","tick":T,"gameOver":b,"obs":<AgentBridge_Observe()>,
   *                    "result":<AgentBridge_Act() result for the previous reply>,
   *                    "beat":B,"sums":[[beat,"md5"],...],"stream":[[beat,op,arg],...]}
   *                   `sums` are the sim's own per-beat checksums (the desync check's) of every beat
   *                   finished since the previous message; `stream` is only present when a client put
   *                   something other than beat bookkeeping into the command stream.
   *   agent -> game   any JSON object; it is handed to AgentBridge_Act(msg) as a Lua table. The engine
   *                   itself reads only `wait` (ticks until the next observation, default 1) and
   *                   `cmd` ("quit" ends the process).
   *
   * The script defines the three global functions named above; what an observation contains and which
   * actions exist is entirely up to it, so the engine side never has to change to add either. Lua
   * errors are caught and reported to the agent in the next message (`"error"`), never raised into
   * the sim. If the agent disconnects the game process exits, so a crashed trainer cannot leave
   * orphaned game instances behind.
   */

  /**
   * Records that the loading phase `phase` (a string literal) was reached, in milliseconds since the
   * process started; the next hello message reports the marks ("load"). Any thread; inert without
   * /agentbridge.
   */
  void AGENT_LoadMark(const char* phase);

  /** Called by `Sim::AdvanceBeat` at its start (sim thread); times the beat for the step report. */
  void AGENT_OnSimBeatStart();

  /** Called by `Sim::AdvanceBeat` at the end of every beat (sim thread). */
  void AGENT_OnSimBeat(Sim* sim);

  /**
   * Called by `CDecoder::DecodeMessage` for every command-stream message the sim consumes (`op` is its
   * ECmdStreamOp, `arg` the first payload byte). The next step message lists the ones that are not the
   * stream's own bookkeeping -- what a client (the local UI included) asked the sim to do, and in which
   * beat: anything there that the agent did not send is outside its control ("stream"). Inert without
   * /agentbridge.
   */
  void AGENT_NoteStreamOp(unsigned op, unsigned arg);

  /** Sim rate the issue thread paces beats to: `/agentspeed` while the bridge is on, else `requested`. */
  [[nodiscard]] int AGENT_ResolvePacingSimRate(int requestedSimRate);

  /**
   * True while the bridge paces the sim faster than real time. The user session then drains every
   * queued sync each frame instead of interpolating at the requested game speed, which would
   * otherwise throttle the sim back to that speed.
   */
  [[nodiscard]] bool AGENT_UserDrainsAllBeats();

  /**
   * Scales one of the run-ahead limits that tie the sim to the render loop (sync packets queued for
   * the user session, beats issued ahead of the last one it consumed, beats it drains per frame)
   * while the bridge runs unpaced. The stock limits allow about ten beats per rendered frame: right
   * for play, a bottleneck for training.
   */
  [[nodiscard]] int AGENT_ScaleRunAheadLimit(int limit);

  /**
   * Period, in ticks, of the full Lua collection `Sim::AdvanceBeat` forces. Stock is 70 (7 s of game
   * time, invisible at normal speed); at training speed that is a dozen full collections of FAF's
   * Lua heap per second and over a third of the sim's time, so an unpaced bridge session uses ten
   * times the period. Lua's own threshold (collect at twice the live size) still bounds memory.
   */
  [[nodiscard]] int AGENT_ForcedGcPeriod(int stockPeriodTicks);

  /**
   * True when particle emitters should skip emission: an unpaced bridge session without
   * `/agentvfx`. Particles only feed the renderer, and draw from a process-wide random stream that
   * is separate from the sim's, so skipping them cannot change the game.
   */
  [[nodiscard]] bool AGENT_SkipParticleEmission();

  /**
   * True with /agentlean (and the bridge on): blueprint loading skips work that only the renderer uses --
   * the strategic-icon textures (four per unit blueprint, created on the loader thread through the D3D
   * device the main thread renders the loading screen with: it waits on that device's lock) and the file
   * searches for mesh material textures a blueprint does not name. The sim reads neither, and the renderer
   * already copes with an icon or texture that is missing.
   */
  [[nodiscard]] bool AGENT_SkipRenderOnlyAssets();

  /**
   * True with /agenttraining (and the bridge on): the sim skips work whose only consumer is the user
   * session that draws the game - nobody watches a training game. What it skips must not change the
   * simulation; each use says why it cannot, and the per-beat sim checksums (/checksumlog) of all-AI games
   * with and without the switch are identical:
   *   - Sim::Sync runs every dirty entity's Sync with no packet: the entity bookkeeping (interface flags,
   *     visibility, command-queue snapshots, the dirty list) is done as always, but no create / update /
   *     delete record is copied into the beat's packet, so the user session builds no UserEntity or
   *     UserUnit and its per-beat work shrinks with it.
   */
  [[nodiscard]] bool AGENT_TrainingMode();
} // namespace moho
