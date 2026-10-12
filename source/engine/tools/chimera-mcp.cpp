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
 * Standard output carries the protocol and nothing else; anything for a
 * person goes to standard error.
 */

#include "../source/mcp.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
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
			std::printf("%s\t%s%s\n", tools[i].name, tools[i].changes ? "[changes] " : "", tools[i].description);
		}
		return 0;
	}
	if (argc > 1)
	{
		std::fprintf(stderr, "usage: chimera-mcp            (started by an agent's program; talks MCP on standard input and output)\n"
			"       chimera-mcp --tools    (print the tools)\n");
		return 2;
	}

#ifdef _WIN32
	/* a line ends with '\n' alone: no translation in either direction */
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	chimera::McpLink link;
	std::string client;
	const chimera::McpExecutor execute = [&](const std::string &tool, const std::string &arguments)
	{
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
