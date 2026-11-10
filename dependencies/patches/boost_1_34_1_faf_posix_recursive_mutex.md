# boost_1_34_1_faf_posix_recursive_mutex.patch

Applied after `boost_1_34_1_faf_required.patch` (cmake/FafDependencies.cmake). Only the Linux build compiles the code it changes.

On Windows, boost 1.34's `boost::mutex` and `boost::try_mutex` are critical sections (`libs/thread/src/mutex.inl`, `new_critical_section`), which the thread holding one may enter again. The engine relies on that: `STAT_GetLuaTable` holds a `StatItem`'s `mLock` while `StatItem::ToLua` → `GetString` → `SetValueCopy` takes it again, every frame the UI shows the frame-rate stats. On POSIX the two were plain `pthread_mutex_t`s, so the Linux client deadlocked there on its first frame.

| File | Change |
|---|---|
| `libs/thread/src/mutex.cpp` | The POSIX `mutex` and `try_mutex` constructors initialise a `PTHREAD_MUTEX_RECURSIVE` mutex, as Windows' are re-entrant. `timed_mutex` (a Win32 mutex object on Windows, also re-entrant) is unchanged: the engine does not use it. |

`boost::condition` waits stay correct: they release the mutex once, which frees a recursive mutex held once, as the critical section's wait does on Windows.
