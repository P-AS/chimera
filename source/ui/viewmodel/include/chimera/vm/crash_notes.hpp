#pragma once
/* What a crash that no handler sees leaves behind (docs/project.md, "Crash
 * notes"). A graphics driver that fast-fails ends the process before any of
 * Chimera's code can run, so the note is written from outside it: Windows
 * Error Reporting loads chimera_crash.dll into WerFault.exe, and that module
 * reads the block kept here in Chimera's memory - where to write, the frame,
 * the GPU bridge's flight recorder, a few lines about the session - and
 * writes a note and a minidump into the notes folder. Nothing here is needed
 * for the work to survive (the recovery journal is); it is what says why it
 * had to. On other systems arming does nothing and says why. */

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace chimera::vm::crash {

/* the module's file name, looked for in the folder arm is given */
inline constexpr const char *kModuleFileName = "chimera_crash.dll";
/* notes (with their dumps) older than the newest this many go when arming */
inline constexpr size_t kNotesKept = 20;

/* Asks Windows to run the crash module if this process dies: the module in
 * `moduleFolder` registered for this user (no administrator), the notes to
 * go to `notesFolder`. "" when armed, else why not; never throws: a crash
 * note is worth having, not worth failing a start over. */
std::string arm(const std::string &moduleFolder, const std::string &notesFolder);
bool armed();
/* where notes go from now on (the data folder decided after arming); "" or why not */
std::string repoint(const std::string &notesFolder);
/* the frame a note names; cheap enough for every frame */
void noteFrame(int64_t frame);
/* where the engine's GPU flight recorder is (ce_gl_flight_recorder), so a
 * note can carry the last calls that crossed the bridge */
void noteGlRecorder(const void *address, uint32_t bytes);
/* the lines a note carries under "session:", replacing what was there */
void describeSession(const std::string &text);

/* one note, as the crash module wrote it */
struct Note
{
	std::string path;
	/* the minidump beside it, "" when there is none */
	std::string dump;
	std::map<std::string, std::string> fields;
	std::string session, stack;
	/* local time, "yyyy-MM-dd HH:mm:ss" */
	std::string time;
	int64_t processId = 0;

	std::string field(const std::string &name) const;
	/* one line a person can read: what failed and where */
	std::string summary() const;
};

/* a note's text read; false when it is not one */
bool parse(const std::string &text, const std::string &path, Note &out);
/* every note in a folder, newest first */
std::vector<Note> notesIn(const std::string &folder);
/* the note the process `processId` left, written no earlier than `notBefore`
 * (local time, "yyyy-MM-dd HH:mm:ss"; a process id alone is reused) */
std::optional<Note> noteFor(int64_t processId, const std::string &notBefore, const std::string &folder);
/* keeps the newest `keep` notes; older notes go with their dumps */
void prune(const std::string &folder, size_t keep);
/* a module a person may recognise ("the NVIDIA graphics driver"), else "" */
std::string whoseModule(const std::string &module);
/* a process's start (processStarted's units) as local time two seconds
 * before it, "yyyy-MM-dd HH:mm:ss" (a note's time has whole seconds); ""
 * where it cannot be said (only Windows writes notes) */
std::string notBeforeStart(int64_t started);

} // namespace chimera::vm::crash
