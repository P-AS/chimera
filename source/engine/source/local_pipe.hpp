/* local_pipe.hpp - internal: one line-based pipe between two programs of the
 * same user on the same machine. Not part of the ABI.
 *
 * It exists for the MCP server (docs/mcp-server.md): a running Chimera is one
 * end, and chimera-mcp, the program an agent starts, is the other. Lines of
 * text go both ways, each ended by '\n'.
 *
 * What it is on each system:
 *   - Windows: a named pipe, \\.\pipe\chimera-mcp-<user>, that only the user
 *     who made it may open and that refuses clients from other machines.
 *   - Linux: two FIFO files in a folder only the user can enter
 *     ($XDG_RUNTIME_DIR/chimera-mcp, else /tmp/chimera-mcp-<uid>).
 *
 * Neither is a socket and no network API is used: Chimera has no network code
 * (docs/core-manager.md, "Nothing reaches the network").
 *
 * One server at a time for a user, and one client at a time for a server. A
 * second of either is refused with a sentence that says so. A second Chimera
 * of the same user can serve under another name: set the environment variable
 * CHIMERA_MCP_NAME to the same word for it and for its chimera-mcp.
 *
 * Threads: an object of either class is used by one thread at a time.
 */
#pragma once

#include <string>

namespace chimera
{

/* What a wait ended with. */
enum class PipeEvent
{
	Nothing,      /* the time ran out */
	Line,         /* a whole line was read (without its '\n') */
	Connected,    /* server: a client has opened the pipe */
	Disconnected, /* the other end went away */
};

/* The end a running Chimera holds. */
class PipeServer
{
public:
	PipeServer();
	~PipeServer();
	PipeServer(const PipeServer &) = delete;
	PipeServer &operator=(const PipeServer &) = delete;

	/* Makes the pipe. False with `error` when it cannot be made, which
	 * includes another program of this user already serving on it. */
	bool open(std::string &error);
	void close();

	/* Waits up to timeout_ms for something to happen. */
	PipeEvent wait(int timeout_ms, std::string &line);
	/* Sends one line (a '\n' is added). False when the client is gone. */
	bool send(const std::string &line);

	/* The pipe's name or folder, for a person to read. */
	std::string name() const;

private:
	struct Impl;
	Impl *impl;
};

/* The end chimera-mcp holds. */
class PipeClient
{
public:
	PipeClient();
	~PipeClient();
	PipeClient(const PipeClient &) = delete;
	PipeClient &operator=(const PipeClient &) = delete;

	/* Opens the pipe a server made. False with `error` when there is no
	 * server, or when another client has it. */
	bool connect(std::string &error);
	void close();
	bool connected() const;

	bool send(const std::string &line);
	/* Line, Nothing (time ran out) or Disconnected. */
	PipeEvent wait(int timeout_ms, std::string &line);

private:
	struct Impl;
	Impl *impl;
};

/* The name of CHIMERA_MCP_NAME, given by the program instead of by the
 * environment; it wins over the variable. Empty for none. Set before any open. */
void pipeName(const std::string &name);

/* For tests only: a name added to the pipe's own, so that a test does not meet
 * the pipe of a Chimera the same user is running. Set before any open. */
void pipeNameSuffixForTests(const std::string &suffix);

} // namespace chimera
