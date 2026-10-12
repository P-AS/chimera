/* mcp.cpp - the MCP server (docs/mcp-server.md): the list of tools, the
 * protocol chimera-mcp speaks with an agent's program, the link from
 * chimera-mcp to a running Chimera, and the part that lives in the running
 * Chimera and runs the tools on its machine (the ce_mcp_* calls of engine.h).
 */

#include "mcp.hpp"

#include "chimera/engine.h"
#include "thread_string.hpp"

#include "../../extern/cjson/cJSON.h"
#include "../../extern/miniz/miniz.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace chimera
{

/* ---- the tools ---- */

namespace
{

const char kNoArguments[] = "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";

const McpTool kTools[] = {
	{ "status",
	  "What is open in Chimera right now: the core and the system, the frame the machine is on, the movie's length, "
	  "the size of the picture, the frame rate, whether the person has allowed changes, and whether the core has stopped.",
	  kNoArguments, false, false },
	{ "controls",
	  "The machine's buttons and axes by name, in the order of the columns of a movie's input lines, "
	  "with each axis's rest value. 'active' is false for a control this machine does not have plugged in.",
	  kNoArguments, false, false },
	{ "get_input",
	  "The movie's input for a range of frames, one line a frame, as the movie file holds it. "
	  "'key' names the columns: groups are separated by '|', a '.' is a button not pressed, a letter is a button pressed, "
	  "and an axis is a number. At most 1000 frames a call.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"from\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"The first frame. 0 when left out.\"},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000,\"description\":\"How many frames. 100 when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "screenshot",
	  "The picture of the frame the machine is on, as a PNG. A large picture is scaled down to max_width.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"max_width\":{\"type\":\"integer\",\"minimum\":32,\"maximum\":4096,\"description\":\"The widest picture wanted, in pixels. 800 when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "list_memory",
	  "The machine's memory: its domains (blocks of memory, addressed from 0) and its buses "
	  "(address spaces as the machine's processor sees them), each with its size and whether it can be written.",
	  kNoArguments, false, false },
	{ "read_memory",
	  "Reads values from a memory domain or a bus at the frame the machine is on. At most 4096 bytes a call.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A name from list_memory.\"},"
	  "\"address\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"How many values. 1 when left out.\"},"
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"],\"description\":\"u8 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"The byte order of values wider than a byte. false when left out.\"}"
	  "},\"required\":[\"domain\",\"address\"],\"additionalProperties\":false}",
	  false, false },
	{ "registers",
	  "The processor's registers, for a core that gives them.",
	  kNoArguments, false, false },
	{ "list_properties",
	  "A game core's named values (a player's position, a score), each with its type and what it reads as now. "
	  "Emulator cores have none. At most 'limit' are returned; 'total' says how many matched.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"filter\":{\"type\":\"string\",\"description\":\"Only names containing this text, in any case.\"},"
	  "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":500,\"description\":\"100 when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "get_property",
	  "One named value of a game core, as a person reads it. An element of an array is asked for as Name[3].",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\"}"
	  "},\"required\":[\"name\"],\"additionalProperties\":false}",
	  false, false },
	{ "list_branches",
	  "The project's branches: for each, its number, the frame it was taken on, when, and its note.",
	  kNoArguments, false, true },
	{ "list_markers",
	  "The project's markers: for each, its frame and its text.",
	  kNoArguments, false, true },
};

std::string printJson(cJSON *item)
{
	char *text = cJSON_PrintUnformatted(item);
	std::string out = text != nullptr ? text : "null";
	cJSON_free(text);
	return out;
}

/* A whole number argument, or `fallback` when it is not given. False when it
 * is given and is not a whole number in [low, high]. */
bool integerArgument(const cJSON *arguments, const char *name, int64_t low, int64_t high, int64_t fallback, int64_t &out)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, name);
	if (item == nullptr || cJSON_IsNull(item)) { out = fallback; return true; }
	if (!cJSON_IsNumber(item)) return false;
	const double value = item->valuedouble;
	if (value != static_cast<double>(static_cast<int64_t>(value))) return false;
	out = static_cast<int64_t>(value);
	return out >= low && out <= high;
}

std::string lowered(std::string text)
{
	for (char &c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	return text;
}

std::string base64(const uint8_t *data, size_t len)
{
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((len + 2) / 3 * 4);
	for (size_t i = 0; i < len; i += 3)
	{
		const uint32_t a = data[i];
		const uint32_t b = i + 1 < len ? data[i + 1] : 0;
		const uint32_t c = i + 2 < len ? data[i + 2] : 0;
		const uint32_t triple = (a << 16) | (b << 8) | c;
		out += table[(triple >> 18) & 63];
		out += table[(triple >> 12) & 63];
		out += i + 1 < len ? table[(triple >> 6) & 63] : '=';
		out += i + 2 < len ? table[triple & 63] : '=';
	}
	return out;
}

} // namespace

const McpTool *mcpTools(size_t &count)
{
	count = sizeof(kTools) / sizeof(kTools[0]);
	return kTools;
}

const McpTool *mcpFindTool(const std::string &name)
{
	for (const McpTool &tool : kTools) if (name == tool.name) return &tool;
	return nullptr;
}

std::string mcpTextContent(const std::string &text)
{
	cJSON *content = cJSON_CreateArray();
	cJSON *item = cJSON_CreateObject();
	cJSON_AddStringToObject(item, "type", "text");
	cJSON_AddStringToObject(item, "text", text.c_str());
	cJSON_AddItemToArray(content, item);
	std::string out = printJson(content);
	cJSON_Delete(content);
	return out;
}

McpResult mcpError(const std::string &sentence)
{
	McpResult result;
	result.content = mcpTextContent(sentence);
	result.isError = true;
	return result;
}

/* ---- the protocol ---- */

namespace
{

/* The versions of the protocol this was written against. An agent's program
 * that asks for one of them gets it; any other gets the newest. */
const char *const kProtocolVersions[] = { "2025-06-18", "2025-03-26", "2024-11-05" };

const char kInstructions[] =
	"Chimera is a frontend for tool-assisted speedruns. These tools work on the project a person has open in a running "
	"Chimera, after they started its server from Tools > MCP Server.... Call 'status' first. Tools that change the "
	"movie or the machine are refused until the person ticks 'Allow the agent to make changes' there.";

std::string rpcReply(const cJSON *id, cJSON *result)
{
	cJSON *reply = cJSON_CreateObject();
	cJSON_AddStringToObject(reply, "jsonrpc", "2.0");
	cJSON_AddItemToObject(reply, "id", cJSON_Duplicate(id, 1));
	cJSON_AddItemToObject(reply, "result", result);
	std::string out = printJson(reply);
	cJSON_Delete(reply);
	return out;
}

std::string rpcError(const cJSON *id, int code, const std::string &message)
{
	cJSON *reply = cJSON_CreateObject();
	cJSON_AddStringToObject(reply, "jsonrpc", "2.0");
	cJSON_AddItemToObject(reply, "id", id != nullptr ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
	cJSON *error = cJSON_CreateObject();
	cJSON_AddNumberToObject(error, "code", code);
	cJSON_AddStringToObject(error, "message", message.c_str());
	cJSON_AddItemToObject(reply, "error", error);
	std::string out = printJson(reply);
	cJSON_Delete(reply);
	return out;
}

} // namespace

std::string mcpHandleLine(const std::string &line, const McpExecutor &execute, std::string *client)
{
	cJSON *message = cJSON_ParseWithLength(line.data(), line.size());
	if (message == nullptr || !cJSON_IsObject(message))
	{
		cJSON_Delete(message);
		return rpcError(nullptr, -32700, "the line is not a JSON object");
	}
	const cJSON *id = cJSON_GetObjectItemCaseSensitive(message, "id");
	const cJSON *methodItem = cJSON_GetObjectItemCaseSensitive(message, "method");
	const cJSON *params = cJSON_GetObjectItemCaseSensitive(message, "params");
	std::string out;
	if (!cJSON_IsString(methodItem))
	{
		/* an answer to something we never asked, or a malformed request */
		if (id != nullptr && cJSON_GetObjectItemCaseSensitive(message, "result") == nullptr
			&& cJSON_GetObjectItemCaseSensitive(message, "error") == nullptr)
			out = rpcError(id, -32600, "the request names no method");
		cJSON_Delete(message);
		return out;
	}
	const std::string method = methodItem->valuestring;
	if (id == nullptr)
	{
		/* a notification: nothing is sent back, whatever it is */
		cJSON_Delete(message);
		return out;
	}

	if (method == "initialize")
	{
		const cJSON *asked = cJSON_GetObjectItemCaseSensitive(params, "protocolVersion");
		const char *version = kProtocolVersions[0];
		for (const char *known : kProtocolVersions)
		{
			if (cJSON_IsString(asked) && std::strcmp(asked->valuestring, known) == 0) version = known;
		}
		if (client != nullptr)
		{
			const cJSON *info = cJSON_GetObjectItemCaseSensitive(params, "clientInfo");
			const cJSON *name = cJSON_GetObjectItemCaseSensitive(info, "name");
			const cJSON *clientVersion = cJSON_GetObjectItemCaseSensitive(info, "version");
			*client = cJSON_IsString(name) ? name->valuestring : "an agent";
			if (cJSON_IsString(clientVersion)) *client += std::string(" ") + clientVersion->valuestring;
		}
		cJSON *result = cJSON_CreateObject();
		cJSON_AddStringToObject(result, "protocolVersion", version);
		cJSON *capabilities = cJSON_CreateObject();
		cJSON_AddItemToObject(capabilities, "tools", cJSON_CreateObject());
		cJSON_AddItemToObject(result, "capabilities", capabilities);
		cJSON *server = cJSON_CreateObject();
		cJSON_AddStringToObject(server, "name", "chimera");
		cJSON_AddStringToObject(server, "title", "Chimera");
		cJSON_AddStringToObject(server, "version", CE_BUILD_COMMIT);
		cJSON_AddItemToObject(result, "serverInfo", server);
		cJSON_AddStringToObject(result, "instructions", kInstructions);
		out = rpcReply(id, result);
	}
	else if (method == "ping")
	{
		out = rpcReply(id, cJSON_CreateObject());
	}
	else if (method == "tools/list")
	{
		cJSON *list = cJSON_CreateArray();
		for (const McpTool &tool : kTools)
		{
			cJSON *item = cJSON_CreateObject();
			cJSON_AddStringToObject(item, "name", tool.name);
			cJSON_AddStringToObject(item, "description", tool.description);
			cJSON_AddItemToObject(item, "inputSchema", cJSON_Parse(tool.schema));
			cJSON *annotations = cJSON_CreateObject();
			cJSON_AddBoolToObject(annotations, "readOnlyHint", !tool.changes);
			cJSON_AddItemToObject(item, "annotations", annotations);
			cJSON_AddItemToArray(list, item);
		}
		cJSON *result = cJSON_CreateObject();
		cJSON_AddItemToObject(result, "tools", list);
		out = rpcReply(id, result);
	}
	else if (method == "tools/call")
	{
		const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
		const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(params, "arguments");
		if (!cJSON_IsString(name))
		{
			out = rpcError(id, -32602, "tools/call needs the tool's name");
		}
		else if (mcpFindTool(name->valuestring) == nullptr)
		{
			out = rpcError(id, -32602, std::string("there is no tool called '") + name->valuestring + "'");
		}
		else
		{
			std::string argumentsText = "{}";
			if (cJSON_IsObject(arguments))
			{
				cJSON *copy = cJSON_Duplicate(arguments, 1);
				argumentsText = printJson(copy);
				cJSON_Delete(copy);
			}
			const McpResult ran = execute(name->valuestring, argumentsText);
			cJSON *result = cJSON_CreateObject();
			cJSON *content = cJSON_Parse(ran.content.c_str());
			cJSON_AddItemToObject(result, "content", content != nullptr ? content : cJSON_CreateArray());
			cJSON_AddBoolToObject(result, "isError", ran.isError);
			out = rpcReply(id, result);
		}
	}
	else
	{
		out = rpcError(id, -32601, "this server does not know the method '" + method + "'");
	}
	cJSON_Delete(message);
	return out;
}

/* ---- the link from chimera-mcp to a running Chimera ----
 *
 * Lines on the pipe, each one JSON object:
 *   chimera-mcp -> Chimera   {"hello":"<the agent's program>"}
 *                            {"id":N,"tool":"<name>","arguments":{...}}
 *   Chimera -> chimera-mcp   {"hello":"chimera"}
 *                            {"id":N,"started":true}      the call was taken up
 *                            {"id":N,"content":[...],"isError":false}
 */

McpResult McpLink::call(const std::string &tool, const std::string &argumentsJson)
{
	const char *const howToStart = " In Chimera, open Tools > MCP Server... and press Start, then call the tool again.";
	std::string line;
	const uint64_t id = nextId++;
	cJSON *request = cJSON_CreateObject();
	cJSON_AddNumberToObject(request, "id", static_cast<double>(id));
	cJSON_AddStringToObject(request, "tool", tool.c_str());
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	cJSON_AddItemToObject(request, "arguments", arguments != nullptr ? arguments : cJSON_CreateObject());
	const std::string requestLine = printJson(request);
	cJSON_Delete(request);

	/* A connection kept from an earlier call may lead to a server that has
	 * since been stopped, or to a Chimera that was closed and opened again.
	 * That shows when the request cannot be sent, and nothing was run, so the
	 * pipe is opened afresh and the request sent once more. */
	for (int attempt = 0;; attempt++)
	{
		const bool fresh = !pipe.connected();
		if (fresh)
		{
			std::string error;
			if (!pipe.connect(error)) return mcpError(error + "." + howToStart);
			cJSON *hello = cJSON_CreateObject();
			cJSON_AddStringToObject(hello, "hello", client.empty() ? "an agent" : client.c_str());
			const bool greeted = pipe.send(printJson(hello));
			cJSON_Delete(hello);
			/* Chimera answers the greeting from its own thread, at once */
			PipeEvent event = PipeEvent::Nothing;
			const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
			while (greeted && event != PipeEvent::Line && event != PipeEvent::Disconnected && std::chrono::steady_clock::now() < until)
			{
				event = pipe.wait(100, line);
			}
			if (event != PipeEvent::Line)
			{
				pipe.close();
				return mcpError(std::string("Chimera's MCP server did not answer.") + howToStart);
			}
		}
		if (pipe.send(requestLine)) break;
		pipe.close();
		if (fresh || attempt > 0)
			return mcpError(std::string("Chimera's MCP server was stopped, or Chimera was closed.") + howToStart);
	}

	bool started = false;
	const auto takeUpBy = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTakeUpMs);
	for (;;)
	{
		const PipeEvent event = pipe.wait(200, line);
		if (event == PipeEvent::Disconnected)
		{
			pipe.close();
			return mcpError(started
				? "Chimera's MCP server was stopped, or Chimera was closed, while the call was running. Whether it finished is not known."
				: std::string("Chimera's MCP server was stopped, or Chimera was closed, before the call was run.") + howToStart);
		}
		if (event == PipeEvent::Line)
		{
			cJSON *reply = cJSON_ParseWithLength(line.data(), line.size());
			const cJSON *replyId = cJSON_GetObjectItemCaseSensitive(reply, "id");
			if (cJSON_IsNumber(replyId) && static_cast<uint64_t>(replyId->valuedouble) == id)
			{
				cJSON *content = cJSON_GetObjectItemCaseSensitive(reply, "content");
				if (content != nullptr)
				{
					McpResult result;
					result.content = printJson(content);
					result.isError = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "isError"));
					cJSON_Delete(reply);
					return result;
				}
				if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "started"))) started = true;
			}
			cJSON_Delete(reply); /* a line for an older call is dropped */
			continue;
		}
		if (!started && std::chrono::steady_clock::now() >= takeUpBy)
		{
			return mcpError("Chimera is busy and did not take the call within 30 seconds, so it was not run. "
				"A dialog may be open in Chimera. Call the tool again when it is closed.");
		}
	}
}

/* ---- the part in the running Chimera ---- */

namespace
{

struct WaitingCall
{
	uint64_t id = 0;
	std::string tool;
	std::string arguments;
	std::chrono::steady_clock::time_point at;
};

/* The server in a running Chimera. One for the process, made on first use and
 * never destroyed: it owns a thread, and a thread's destructor at the moment
 * a library is unloaded is not something to rely on (thread_string.hpp has
 * the history of that kind of trouble). */
struct Service
{
	std::mutex lock;                   /* everything below, except where said */
	std::thread thread;
	std::atomic<bool> running{ false };
	PipeServer pipe;                   /* the thread's alone while it runs */
	std::string endpoint;
	int state = CE_MCP_STOPPED;
	std::string client;
	std::deque<WaitingCall> waiting;   /* read by the thread, run by the caller of ce_mcp_service */
	std::deque<std::string> outgoing;  /* lines for the thread to send */
	std::atomic<int> pending{ 0 };
	std::atomic<bool> allowChanges{ false };
	std::map<uint64_t, WaitingCall> withHost; /* calls the frontend is running */
	std::deque<std::string> log;
	uint64_t logTotal = 0;
	bool wroteMemory = false;
};

Service &service()
{
	static Service *const one = new Service();
	return *one;
}

const size_t kLogKept = 5000;

std::string shortened(const std::string &text, size_t limit)
{
	std::string out;
	for (char c : text) out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
	if (out.size() > limit) out = out.substr(0, limit) + "...";
	return out;
}

/* with the lock held */
void addLog(Service &sv, const std::string &text)
{
	char stamp[16] = "";
	const std::time_t now = std::time(nullptr);
	struct tm parts;
#ifdef _WIN32
	localtime_s(&parts, &now);
#else
	localtime_r(&now, &parts);
#endif
	std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &parts);
	sv.log.push_back(std::string(stamp) + "  " + text);
	sv.logTotal++;
	if (sv.log.size() > kLogKept) sv.log.pop_front();
}

/* The first text of a content array, for the log. */
std::string contentSummary(const std::string &content)
{
	cJSON *parsed = cJSON_Parse(content.c_str());
	std::string out;
	const cJSON *item = nullptr;
	cJSON_ArrayForEach(item, parsed)
	{
		const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
		const cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
		if (cJSON_IsString(text)) { out = text->valuestring; break; }
		if (cJSON_IsString(type)) out = std::string("(") + type->valuestring + ")";
	}
	cJSON_Delete(parsed);
	return out;
}

/* with the lock held: queues the answer and writes the log line */
void finish(Service &sv, const WaitingCall &call, const McpResult &result)
{
	cJSON *reply = cJSON_CreateObject();
	cJSON_AddNumberToObject(reply, "id", static_cast<double>(call.id));
	cJSON *content = cJSON_Parse(result.content.c_str());
	cJSON_AddItemToObject(reply, "content", content != nullptr ? content : cJSON_CreateArray());
	cJSON_AddBoolToObject(reply, "isError", result.isError);
	sv.outgoing.push_back(printJson(reply));
	cJSON_Delete(reply);
	const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - call.at).count();
	addLog(sv, call.tool + " " + shortened(call.arguments, 120) + "  ->  "
		+ (result.isError ? "REFUSED: " : "") + shortened(contentSummary(result.content), 160)
		+ "  (" + std::to_string(ms) + " ms)");
}

void pipeThread(Service *sv)
{
	std::string line;
	while (sv->running.load())
	{
		/* a short wait, so an answer queued meanwhile leaves within
		 * milliseconds; the cost is a few hundred wake-ups a second, and only
		 * while the server is started */
		const PipeEvent event = sv->pipe.wait(4, line);
		std::vector<std::string> toSend;
		{
			std::lock_guard<std::mutex> hold(sv->lock);
			if (event == PipeEvent::Connected)
			{
				sv->state = CE_MCP_CONNECTED;
				sv->client = "an agent";
				addLog(*sv, "an agent's program connected");
			}
			else if (event == PipeEvent::Disconnected)
			{
				sv->state = CE_MCP_WAITING;
				sv->pending -= static_cast<int>(sv->waiting.size());
				sv->waiting.clear();
				sv->outgoing.clear();
				/* a call the frontend is still running has nobody to answer to */
				sv->withHost.clear();
				addLog(*sv, sv->client + " disconnected");
				sv->client.clear();
			}
			else if (event == PipeEvent::Line)
			{
				cJSON *message = cJSON_ParseWithLength(line.data(), line.size());
				const cJSON *hello = cJSON_GetObjectItemCaseSensitive(message, "hello");
				const cJSON *id = cJSON_GetObjectItemCaseSensitive(message, "id");
				const cJSON *tool = cJSON_GetObjectItemCaseSensitive(message, "tool");
				if (cJSON_IsString(hello))
				{
					sv->client = hello->valuestring;
					addLog(*sv, "the agent's program is " + sv->client);
					sv->outgoing.push_back("{\"hello\":\"chimera\"}");
				}
				else if (cJSON_IsNumber(id) && cJSON_IsString(tool))
				{
					WaitingCall call;
					call.id = static_cast<uint64_t>(id->valuedouble);
					call.tool = tool->valuestring;
					cJSON *arguments = cJSON_GetObjectItemCaseSensitive(message, "arguments");
					call.arguments = cJSON_IsObject(arguments) ? printJson(arguments) : "{}";
					call.at = std::chrono::steady_clock::now();
					sv->waiting.push_back(std::move(call));
					sv->pending++;
				}
				cJSON_Delete(message);
			}
			toSend.assign(sv->outgoing.begin(), sv->outgoing.end());
			sv->outgoing.clear();
		}
		for (const std::string &out : toSend) sv->pipe.send(out);
	}
}

/* ---- the tools the engine runs itself ---- */

const char kNoMachine[] = "No project is open in Chimera, so there is no machine to ask.";

McpResult textResult(cJSON *object)
{
	McpResult result;
	result.content = mcpTextContent(printJson(object));
	cJSON_Delete(object);
	return result;
}

McpResult toolStatus(ce_session *s, const ce_movie_log *log, int64_t frame, const char *hostStatus, bool allowChanges, bool wroteMemory)
{
	/* what the frontend knows (the project's name, whether it is paused)
	 * comes first, and the engine's own facts are added to it */
	cJSON *out = hostStatus != nullptr ? cJSON_Parse(hostStatus) : nullptr;
	if (out == nullptr || !cJSON_IsObject(out))
	{
		cJSON_Delete(out);
		out = cJSON_CreateObject();
	}
	cJSON_AddBoolToObject(out, "machine_open", s != nullptr);
	cJSON_AddBoolToObject(out, "changes_allowed", allowChanges);
	if (s != nullptr)
	{
		cJSON_AddStringToObject(out, "core", ce_session_core_name(s));
		cJSON_AddStringToObject(out, "system", ce_session_system_name(s));
		cJSON_AddNumberToObject(out, "frame", static_cast<double>(frame >= 0 ? frame : ce_session_frame(s)));
		if (log != nullptr) cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(log)));
		cJSON *picture = cJSON_CreateObject();
		cJSON_AddNumberToObject(picture, "width", ce_session_video_width(s));
		cJSON_AddNumberToObject(picture, "height", ce_session_video_height(s));
		cJSON_AddItemToObject(out, "picture", picture);
		const int32_t numerator = ce_session_vsync_numerator(s);
		const int32_t denominator = ce_session_vsync_denominator(s);
		if (denominator > 0) cJSON_AddNumberToObject(out, "frames_per_second", static_cast<double>(numerator) / denominator);
		const char *death = ce_session_guest_death(s);
		if (death != nullptr) cJSON_AddStringToObject(out, "core_stopped", death);
		else cJSON_AddBoolToObject(out, "core_stopped", false);
		cJSON_AddBoolToObject(out, "memory_written_by_agent", wroteMemory);
	}
	return textResult(out);
}

McpResult toolControls(ce_session *s, const ce_movie_log *log)
{
	cJSON *out = cJSON_CreateObject();
	cJSON *buttons = cJSON_CreateArray();
	for (int64_t i = 0; i < ce_session_button_count(s); i++)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_button_name(s, i));
		cJSON_AddBoolToObject(item, "active", ce_session_button_active(s, i) != 0);
		cJSON_AddItemToArray(buttons, item);
	}
	cJSON *axes = cJSON_CreateArray();
	for (int64_t i = 0; i < ce_session_axis_count(s); i++)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_axis_name(s, i));
		cJSON_AddNumberToObject(item, "rest", ce_session_axis_neutral(s, i));
		cJSON_AddBoolToObject(item, "active", ce_session_axis_active(s, i) != 0);
		cJSON_AddItemToArray(axes, item);
	}
	cJSON_AddItemToObject(out, "buttons", buttons);
	cJSON_AddItemToObject(out, "axes", axes);
	const char *key = log != nullptr ? ce_movie_log_key(log) : nullptr;
	if (key != nullptr) cJSON_AddStringToObject(out, "key", key);
	return textResult(out);
}

McpResult toolGetInput(const ce_movie_log *log, const cJSON *arguments)
{
	if (log == nullptr) return mcpError("No movie is open in Chimera.");
	int64_t from = 0, count = 0;
	if (!integerArgument(arguments, "from", 0, INT64_MAX, 0, from)) return mcpError("'from' must be a frame number, 0 or more.");
	if (!integerArgument(arguments, "count", 1, 1000, 100, count)) return mcpError("'count' must be between 1 and 1000.");
	const int64_t length = ce_movie_log_count(log);
	cJSON *out = cJSON_CreateObject();
	cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(length));
	cJSON_AddNumberToObject(out, "from", static_cast<double>(from));
	const char *key = ce_movie_log_key(log);
	if (key != nullptr) cJSON_AddStringToObject(out, "key", key);
	cJSON *entries = cJSON_CreateArray();
	for (int64_t frame = from; frame < length && frame < from + count; frame++)
	{
		const char *entry = ce_movie_log_entry(log, frame);
		cJSON_AddItemToArray(entries, cJSON_CreateString(entry != nullptr ? entry : ""));
	}
	cJSON_AddNumberToObject(out, "count", cJSON_GetArraySize(entries));
	cJSON_AddItemToObject(out, "entries", entries);
	return textResult(out);
}

McpResult toolScreenshot(ce_session *s, int64_t frame, const cJSON *arguments)
{
	int64_t maxWidth = 0;
	if (!integerArgument(arguments, "max_width", 32, 4096, 800, maxWidth)) return mcpError("'max_width' must be between 32 and 4096.");
	const uint32_t *bgra = ce_session_video(s);
	const int32_t width = ce_session_video_width(s);
	const int32_t height = ce_session_video_height(s);
	if (bgra == nullptr || width <= 0 || height <= 0) return mcpError("The machine has not drawn a picture yet.");

	/* a whole-number reduction: every pixel of the result is the average of
	 * a square of the picture's */
	int32_t step = 1;
	while (width / step > maxWidth) step++;
	const int32_t outWidth = width / step;
	const int32_t outHeight = std::max(1, height / step);
	std::vector<uint8_t> rgb(static_cast<size_t>(outWidth) * outHeight * 3);
	for (int32_t y = 0; y < outHeight; y++)
	{
		for (int32_t x = 0; x < outWidth; x++)
		{
			uint32_t sum[3] = { 0, 0, 0 };
			uint32_t seen = 0;
			for (int32_t dy = 0; dy < step && y * step + dy < height; dy++)
			{
				const uint32_t *row = bgra + static_cast<size_t>(y * step + dy) * width + static_cast<size_t>(x) * step;
				for (int32_t dx = 0; dx < step; dx++)
				{
					sum[0] += (row[dx] >> 16) & 0xFF;
					sum[1] += (row[dx] >> 8) & 0xFF;
					sum[2] += row[dx] & 0xFF;
					seen++;
				}
			}
			uint8_t *pixel = &rgb[(static_cast<size_t>(y) * outWidth + x) * 3];
			for (int c = 0; c < 3; c++) pixel[c] = static_cast<uint8_t>(sum[c] / seen);
		}
	}
	size_t pngLen = 0;
	void *png = tdefl_write_image_to_png_file_in_memory_ex(rgb.data(), outWidth, outHeight, 3, &pngLen, 6, MZ_FALSE);
	if (png == nullptr) return mcpError("The picture could not be made into a PNG.");
	const std::string encoded = base64(static_cast<const uint8_t *>(png), pngLen);
	mz_free(png);

	cJSON *content = cJSON_CreateArray();
	cJSON *image = cJSON_CreateObject();
	cJSON_AddStringToObject(image, "type", "image");
	cJSON_AddStringToObject(image, "data", encoded.c_str());
	cJSON_AddStringToObject(image, "mimeType", "image/png");
	cJSON_AddItemToArray(content, image);
	cJSON *facts = cJSON_CreateObject();
	cJSON_AddNumberToObject(facts, "frame", static_cast<double>(frame >= 0 ? frame : ce_session_frame(s)));
	cJSON_AddNumberToObject(facts, "width", outWidth);
	cJSON_AddNumberToObject(facts, "height", outHeight);
	cJSON_AddNumberToObject(facts, "picture_width", width);
	cJSON_AddNumberToObject(facts, "picture_height", height);
	cJSON *text = cJSON_CreateObject();
	cJSON_AddStringToObject(text, "type", "text");
	const std::string factsText = printJson(facts);
	cJSON_Delete(facts);
	cJSON_AddStringToObject(text, "text", factsText.c_str());
	cJSON_AddItemToArray(content, text);
	McpResult result;
	result.content = printJson(content);
	cJSON_Delete(content);
	return result;
}

McpResult toolListMemory(ce_session *s)
{
	cJSON *out = cJSON_CreateObject();
	cJSON *domains = cJSON_CreateArray();
	for (int32_t i = 0; i < ce_session_domain_count(s); i++)
	{
		if (ce_session_domain_hidden(s, i) != 0) continue;
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_domain_name(s, i));
		cJSON_AddNumberToObject(item, "size", static_cast<double>(ce_session_domain_size(s, i)));
		cJSON_AddBoolToObject(item, "writable", ce_session_domain_writable(s, i) != 0);
		cJSON_AddItemToArray(domains, item);
	}
	cJSON *buses = cJSON_CreateArray();
	for (int32_t i = 0; i < ce_session_bus_count(s); i++)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_bus_name(s, i));
		cJSON_AddNumberToObject(item, "size", static_cast<double>(ce_session_bus_size(s, i)));
		cJSON_AddBoolToObject(item, "writable", ce_session_bus_writable(s, i) != 0);
		cJSON_AddItemToArray(buses, item);
	}
	cJSON_AddItemToObject(out, "domains", domains);
	cJSON_AddItemToObject(out, "buses", buses);
	return textResult(out);
}

McpResult toolReadMemory(ce_session *s, const cJSON *arguments)
{
	const cJSON *domainItem = cJSON_GetObjectItemCaseSensitive(arguments, "domain");
	if (!cJSON_IsString(domainItem)) return mcpError("'domain' must be a name from list_memory.");
	const std::string domain = domainItem->valuestring;
	const cJSON *typeItem = cJSON_GetObjectItemCaseSensitive(arguments, "type");
	const std::string type = cJSON_IsString(typeItem) ? typeItem->valuestring : "u8";
	if (type.size() < 2 || (type[0] != 'u' && type[0] != 's')
		|| (type.substr(1) != "8" && type.substr(1) != "16" && type.substr(1) != "32"))
		return mcpError("'type' must be one of u8, s8, u16, s16, u32, s32.");
	const bool isSigned = type[0] == 's';
	const int64_t width = type.substr(1) == "8" ? 1 : type.substr(1) == "16" ? 2 : 4;
	const bool bigEndian = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "big_endian"));
	int64_t address = 0, count = 0;
	if (!integerArgument(arguments, "address", 0, INT64_MAX / 8, 0, address)) return mcpError("'address' must be 0 or more.");
	if (!integerArgument(arguments, "count", 1, 4096 / width, 1, count))
		return mcpError("'count' must be at least 1, and the values read must fit in 4096 bytes.");

	/* a domain by that name, else a bus by that name */
	int32_t domainIndex = -1, busIndex = -1;
	for (int32_t i = 0; i < ce_session_domain_count(s) && domainIndex < 0; i++)
	{
		if (ce_session_domain_hidden(s, i) == 0 && domain == ce_session_domain_name(s, i)) domainIndex = i;
	}
	for (int32_t i = 0; domainIndex < 0 && i < ce_session_bus_count(s) && busIndex < 0; i++)
	{
		if (domain == ce_session_bus_name(s, i)) busIndex = i;
	}
	if (domainIndex < 0 && busIndex < 0) return mcpError("There is no domain or bus called '" + domain + "'. list_memory names them.");
	const int64_t size = domainIndex >= 0 ? ce_session_domain_size(s, domainIndex) : ce_session_bus_size(s, busIndex);
	if (address + count * width > size)
		return mcpError("'" + domain + "' is " + std::to_string(size) + " bytes, and the read goes past its end.");

	std::vector<uint8_t> bytes(static_cast<size_t>(count * width));
	const int64_t got = domainIndex >= 0
		? ce_session_domain_read(s, domainIndex, address, bytes.data(), static_cast<int64_t>(bytes.size()))
		: ce_session_bus_read(s, busIndex, address, bytes.data(), static_cast<int64_t>(bytes.size()));
	if (got != static_cast<int64_t>(bytes.size())) return mcpError("The machine did not give those bytes.");

	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", domain.c_str());
	cJSON_AddNumberToObject(out, "address", static_cast<double>(address));
	cJSON_AddStringToObject(out, "type", type.c_str());
	cJSON *values = cJSON_CreateArray();
	for (int64_t i = 0; i < count; i++)
	{
		uint32_t value = 0;
		for (int64_t b = 0; b < width; b++)
		{
			const uint8_t byte = bytes[static_cast<size_t>(i * width + b)];
			value |= static_cast<uint32_t>(byte) << (8 * (bigEndian ? width - 1 - b : b));
		}
		double number = value;
		if (isSigned)
		{
			number = width == 1 ? static_cast<double>(static_cast<int8_t>(value))
				: width == 2 ? static_cast<double>(static_cast<int16_t>(value))
				: static_cast<double>(static_cast<int32_t>(value));
		}
		cJSON_AddItemToArray(values, cJSON_CreateNumber(number));
	}
	cJSON_AddItemToObject(out, "values", values);
	std::string hex;
	static const char digits[] = "0123456789ABCDEF";
	for (uint8_t byte : bytes) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
	cJSON_AddStringToObject(out, "bytes_hex", hex.c_str());
	return textResult(out);
}

McpResult toolRegisters(ce_session *s)
{
	cJSON *out = cJSON_CreateObject();
	cJSON *registers = cJSON_CreateArray();
	for (int32_t i = 0; i < ce_session_register_count(s); i++)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_register_name(s, i));
		const int32_t bits = ce_session_register_bits(s, i);
		const uint64_t value = static_cast<uint64_t>(ce_session_register_value(s, i));
		cJSON_AddNumberToObject(item, "bits", bits);
		char hex[32];
		std::snprintf(hex, sizeof(hex), "0x%0*llX", std::max(1, (bits + 3) / 4), static_cast<unsigned long long>(value));
		cJSON_AddStringToObject(item, "value", hex);
		cJSON_AddItemToArray(registers, item);
	}
	cJSON_AddItemToObject(out, "registers", registers);
	if (ce_session_has_executed_cycles(s) != 0)
		cJSON_AddNumberToObject(out, "executed_cycles", static_cast<double>(ce_session_executed_cycles(s)));
	return textResult(out);
}

std::string propertyText(ce_session *s, int32_t index, uint32_t element)
{
	char buffer[1024];
	const int32_t length = ce_session_property_text(s, index, element, 1, buffer, sizeof(buffer));
	return length >= 0 ? std::string(buffer) : std::string();
}

McpResult toolListProperties(ce_session *s, const cJSON *arguments)
{
	int64_t limit = 0;
	if (!integerArgument(arguments, "limit", 1, 500, 100, limit)) return mcpError("'limit' must be between 1 and 500.");
	const cJSON *filterItem = cJSON_GetObjectItemCaseSensitive(arguments, "filter");
	const std::string filter = cJSON_IsString(filterItem) ? lowered(filterItem->valuestring) : std::string();
	/* a table the game makes as it runs is listed afresh before it is read */
	if (ce_session_property_dynamic(s) != 0) ce_session_property_refresh(s);
	cJSON *table = cJSON_Parse(ce_session_property_table(s));
	const cJSON *properties = cJSON_GetObjectItemCaseSensitive(table, "properties");
	cJSON *out = cJSON_CreateObject();
	cJSON *list = cJSON_CreateArray();
	int32_t index = 0;
	int64_t total = 0;
	const cJSON *property = nullptr;
	cJSON_ArrayForEach(property, properties)
	{
		const cJSON *name = cJSON_GetObjectItemCaseSensitive(property, "name");
		if (cJSON_IsString(name) && (filter.empty() || lowered(name->valuestring).find(filter) != std::string::npos))
		{
			total++;
			if (cJSON_GetArraySize(list) < limit)
			{
				cJSON *item = cJSON_CreateObject();
				cJSON_AddStringToObject(item, "name", name->valuestring);
				const cJSON *type = cJSON_GetObjectItemCaseSensitive(property, "type");
				if (cJSON_IsString(type)) cJSON_AddStringToObject(item, "type", type->valuestring);
				const cJSON *count = cJSON_GetObjectItemCaseSensitive(property, "count");
				if (cJSON_IsNumber(count) && count->valuedouble > 1) cJSON_AddNumberToObject(item, "elements", count->valuedouble);
				cJSON_AddStringToObject(item, "value", propertyText(s, index, 0).c_str());
				cJSON_AddItemToArray(list, item);
			}
		}
		index++;
	}
	cJSON_Delete(table);
	cJSON_AddNumberToObject(out, "total", static_cast<double>(total));
	cJSON_AddItemToObject(out, "properties", list);
	return textResult(out);
}

McpResult toolGetProperty(ce_session *s, const cJSON *arguments)
{
	const cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	if (!cJSON_IsString(nameItem)) return mcpError("'name' must be a property's name. list_properties names them.");
	if (ce_session_property_dynamic(s) != 0) ce_session_property_refresh(s);
	uint32_t element = 0;
	const int32_t index = ce_session_property_find(s, nameItem->valuestring, &element);
	if (index < 0) return mcpError(std::string("There is no property called '") + nameItem->valuestring + "'. list_properties names them.");
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "name", nameItem->valuestring);
	cJSON_AddStringToObject(out, "value", propertyText(s, index, element).c_str());
	return textResult(out);
}

McpResult runEngineTool(Service &sv, const std::string &tool, ce_session *s, const ce_movie_log *log,
	int64_t frame, const char *hostStatus, const cJSON *arguments)
{
	if (tool == "status") return toolStatus(s, log, frame, hostStatus, sv.allowChanges.load(), sv.wroteMemory);
	if (tool == "get_input") return toolGetInput(log, arguments);
	if (s == nullptr) return mcpError(kNoMachine);
	if (tool == "controls") return toolControls(s, log);
	if (tool == "screenshot") return toolScreenshot(s, frame, arguments);
	if (tool == "list_memory") return toolListMemory(s);
	if (tool == "read_memory") return toolReadMemory(s, arguments);
	if (tool == "registers") return toolRegisters(s);
	if (tool == "list_properties") return toolListProperties(s, arguments);
	if (tool == "get_property") return toolGetProperty(s, arguments);
	return mcpError("Chimera does not run the tool '" + tool + "' yet.");
}

} // namespace

} // namespace chimera

/* ---- the ABI ---- */

using chimera::Service;
using chimera::service;

extern "C" {

int32_t ce_mcp_start(const char **error_out)
{
	static thread_local chimera::ThreadString error;   /* never destroyed: see thread_string.hpp */
	Service &sv = service();
	if (sv.running.load()) return 1;
	if (!sv.pipe.open(*error))
	{
		if (error_out != nullptr) *error_out = error->c_str();
		return 0;
	}
	{
		std::lock_guard<std::mutex> hold(sv.lock);
		sv.endpoint = sv.pipe.name();
		sv.state = CE_MCP_WAITING;
		sv.client.clear();
		sv.waiting.clear();
		sv.outgoing.clear();
		sv.withHost.clear();
		sv.pending = 0;
		/* every start begins with changes not allowed */
		sv.allowChanges = false;
		sv.wroteMemory = false;
		chimera::addLog(sv, "the server was started");
	}
	sv.running = true;
	sv.thread = std::thread(chimera::pipeThread, &sv);
	return 1;
}

void ce_mcp_stop(void)
{
	Service &sv = service();
	if (!sv.running.exchange(false)) return;
	if (sv.thread.joinable()) sv.thread.join();
	sv.pipe.close();
	std::lock_guard<std::mutex> hold(sv.lock);
	sv.state = CE_MCP_STOPPED;
	sv.client.clear();
	sv.waiting.clear();
	sv.outgoing.clear();
	sv.withHost.clear();
	sv.pending = 0;
	sv.allowChanges = false;
	chimera::addLog(sv, "the server was stopped");
}

void ce_mcp_name(const char *name)
{
	chimera::pipeName(name != nullptr ? name : "");
}

int32_t ce_mcp_state(void)
{
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	return sv.state;
}

const char *ce_mcp_client(void)
{
	static thread_local chimera::ThreadString held;
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	*held = sv.client;
	return held->c_str();
}

const char *ce_mcp_endpoint(void)
{
	static thread_local chimera::ThreadString held;
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	*held = sv.endpoint;
	return held->c_str();
}

void ce_mcp_allow_changes(int32_t on)
{
	Service &sv = service();
	const bool allow = on != 0;
	if (sv.allowChanges.exchange(allow) == allow) return;
	std::lock_guard<std::mutex> hold(sv.lock);
	chimera::addLog(sv, allow ? "changes by the agent were allowed" : "changes by the agent were disallowed");
}

int32_t ce_mcp_changes_allowed(void) { return service().allowChanges.load() ? 1 : 0; }

int32_t ce_mcp_pending(void) { return service().pending.load() > 0 ? 1 : 0; }

int32_t ce_mcp_service(ce_session *s, const ce_movie_log *log, int64_t frame, const char *host_status_json,
	uint64_t *id_out, const char **tool_out, const char **arguments_out)
{
	static thread_local chimera::ThreadString heldTool, heldArguments;
	Service &sv = service();
	for (;;)
	{
		chimera::WaitingCall call;
		const chimera::McpTool *tool = nullptr;
		{
			std::lock_guard<std::mutex> hold(sv.lock);
			if (!sv.running.load() || sv.waiting.empty()) return 0;
			call = std::move(sv.waiting.front());
			sv.waiting.pop_front();
			sv.pending--;
			const long long waited = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - call.at).count();
			if (waited > chimera::McpLink::kStaleMs)
			{
				/* the agent has been told by now that this call was not run,
				 * so it must not be */
				chimera::addLog(sv, call.tool + "  ->  NOT RUN: it waited " + std::to_string(waited / 1000) + " s for Chimera to be free");
				continue;
			}
			sv.outgoing.push_back("{\"id\":" + std::to_string(call.id) + ",\"started\":true}");
			tool = chimera::mcpFindTool(call.tool);
			if (tool == nullptr)
			{
				chimera::finish(sv, call, chimera::mcpError("There is no tool called '" + call.tool + "'."));
				continue;
			}
			if (tool->changes && !sv.allowChanges.load())
			{
				chimera::finish(sv, call, chimera::mcpError(
					"Changes are not allowed. The person using Chimera can allow them: Tools > MCP Server... > "
					"Allow the agent to make changes."));
				continue;
			}
			if (tool->host)
			{
				*heldTool = call.tool;
				*heldArguments = call.arguments;
				if (id_out != nullptr) *id_out = call.id;
				if (tool_out != nullptr) *tool_out = heldTool->c_str();
				if (arguments_out != nullptr) *arguments_out = heldArguments->c_str();
				sv.withHost[call.id] = std::move(call);
				return 1;
			}
		}
		/* the engine's own tools run without the lock: they only touch the
		 * machine, which is this thread's */
		cJSON *arguments = cJSON_Parse(call.arguments.c_str());
		const chimera::McpResult result = chimera::runEngineTool(sv, call.tool, s, log, frame, host_status_json, arguments);
		cJSON_Delete(arguments);
		std::lock_guard<std::mutex> hold(sv.lock);
		chimera::finish(sv, call, result);
	}
}

void ce_mcp_answer(uint64_t id, const char *text, int32_t is_error)
{
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	const auto found = sv.withHost.find(id);
	if (found == sv.withHost.end()) return;
	chimera::McpResult result;
	result.content = chimera::mcpTextContent(text != nullptr ? text : "");
	result.isError = is_error != 0;
	chimera::finish(sv, found->second, result);
	sv.withHost.erase(found);
}

uint64_t ce_mcp_log_total(void)
{
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	return sv.logTotal;
}

const char *ce_mcp_log_line(uint64_t index)
{
	static thread_local chimera::ThreadString held;
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	held->clear();
	const uint64_t first = sv.logTotal - sv.log.size();
	if (index >= first && index < sv.logTotal) *held = sv.log[static_cast<size_t>(index - first)];
	return held->c_str();
}

} // extern "C"
