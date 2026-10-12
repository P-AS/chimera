/* chimera-mcp - the program an AI agent's program starts to use Chimera
 * (docs/mcp-server.md).
 *
 * It speaks MCP, the Model Context Protocol, over its standard input and
 * output: one line of JSON in, one line out. It runs no machine itself. Every
 * tool call is passed to the Chimera the same user has running, over a pipe
 * that is local to the machine (source/engine/source/local_pipe.hpp), and the
 * answer is passed back. No socket is opened and nothing leaves the machine.
 *
 * When no Chimera is running, or its server is not started (Tools > MCP
 * Server...), the tools are still listed and a call answers with a sentence
 * saying how to start it. So the agent's program does not need restarting
 * when Chimera is.
 *
 *   chimera-mcp            serve the agent's program until its input ends
 *   chimera-mcp --tools    print the tools, one "name<TAB>description" a line
 *
 * Without a window. Given a cores folder, chimera-mcp reaches no Chimera at
 * all: it opens a machine of its own for the agent, inside this process
 * (source/engine/source/mcp_local.hpp), with tools to open a game or a
 * project and to save what was made:
 *
 *   chimera-mcp --cores <dir> --work <dir> [--files <dir>]... [--gpu] [--history <MiB>]
 *
 *   --cores    the folder of core packages; nothing else is read as a core
 *   --work     the one folder anything is written in; it may be read from too
 *   --files    a folder games, projects, movies and firmware may be read from;
 *              repeatable
 *   --gpu      ask for the GPU bridge, for a core that draws on the card
 *   --history  the memory, in MiB, for the saved states that make going back
 *              cheap (256 when not given)
 *
 * Standard output carries the protocol and nothing else; anything for a
 * person goes to standard error.
 */

#include "../source/mcp.hpp"
#include "../source/mcp_local.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv)
{
	if (argc > 1 && std::strcmp(argv[1], "--tools") == 0)
	{
		size_t count = 0;
		const chimera::McpTool *tools = chimera::mcpTools(count);
		for (size_t i = 0; i < count; i++)
		{
			std::printf("%s\t%s%s%s\n", tools[i].name, tools[i].changes ? "[changes] " : "",
				tools[i].local ? "[without a window] " : "", tools[i].description);
		}
		return 0;
	}
	/* with a cores folder: no window, a machine of this program's own */
	chimera::McpLocalOptions local;
	bool usage = false;
	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];
		if (arg == "--cores" && i + 1 < argc) local.coresDir = argv[++i];
		else if (arg == "--work" && i + 1 < argc) local.workDir = argv[++i];
		else if (arg == "--files" && i + 1 < argc) local.fileDirs.emplace_back(argv[++i]);
		else if (arg == "--history" && i + 1 < argc) local.historyMiB = std::atoll(argv[++i]);
		else if (arg == "--gpu") local.gpu = true;
		else usage = true;
	}
	const bool windowless = !local.coresDir.empty();
	if (usage || (argc > 1 && (!windowless || local.workDir.empty() || local.historyMiB < 1)))
	{
		std::fprintf(stderr, "usage: chimera-mcp            (started by an agent's program; talks MCP on standard input and output,\n"
			"                              and works on the project open in a running Chimera)\n"
			"       chimera-mcp --cores <dir> --work <dir> [--files <dir>]... [--gpu] [--history <MiB>]\n"
			"                              (no window: opens a machine of its own; writes only inside --work)\n"
			"       chimera-mcp --tools    (print the tools)\n");
		return 2;
	}

#ifdef _WIN32
	/* a line ends with '\n' alone: no translation in either direction */
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	chimera::McpLink link;
	std::unique_ptr<chimera::McpLocal> machine;
	if (windowless)
	{
		chimera::mcpSetLocal(true);
		machine = std::make_unique<chimera::McpLocal>(local);
	}
	std::string client;
	const chimera::McpExecutor execute = [&](const std::string &tool, const std::string &arguments)
	{
		if (machine) return machine->call(tool, arguments);
		link.setClient(client);
		return link.call(tool, arguments);
	};

	std::string line;
	while (std::getline(std::cin, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;
		const std::string reply = chimera::mcpHandleLine(line, execute, &client);
		if (reply.empty()) continue;
		std::fwrite(reply.data(), 1, reply.size(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	}
	return 0;
}
