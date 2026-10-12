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
	{ "watch",
	  "Names values to be told with every answer that lands the machine on a frame: status, seek, advance_until and "
	  "load_branch then carry a 'watches' list, which saves a read_memory call after each. Each watch is a memory value "
	  "(domain, address, type) or a game core's property. The list given replaces the one before; an empty list ends "
	  "watching. Answers with the values now. At most 32.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"watches\":{\"type\":\"array\",\"maxItems\":32,\"items\":{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\",\"description\":\"What to call it in the answers. Made up from the rest when left out.\"},"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A domain or bus from list_memory. Give this with 'address', or give 'property'.\"},"
	  "\"address\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"],\"description\":\"u8 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"},"
	  "\"property\":{\"type\":\"string\",\"description\":\"A game core's named value, in place of domain and address.\"}"
	  "},\"additionalProperties\":false}}"
	  "},\"required\":[\"watches\"],\"additionalProperties\":false}",
	  false, false },
	{ "search_start",
	  "Starts a RAM search over a memory domain: every address becomes a candidate, and its value now is remembered as its "
	  "'previous' value. Then change something in the game (seek, or let the person play) and narrow the candidates with "
	  "search_filter. One search at a time; starting a new one drops the old.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A domain from list_memory (not a bus).\"},"
	  "\"size\":{\"type\":\"integer\",\"enum\":[1,2,4],\"description\":\"Bytes a value. 1 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"}"
	  "},\"required\":[\"domain\"],\"additionalProperties\":false}",
	  false, false },
	{ "search_filter",
	  "Keeps the candidates whose value now compares as asked, to 'value' when it is given and to their previous value when "
	  "it is not. Afterwards each candidate's previous value is its value now. Answers how many are left.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"op\":{\"type\":\"string\",\"enum\":[\"equal\",\"not_equal\",\"greater\",\"greater_or_equal\",\"less\",\"less_or_equal\"]},"
	  "\"value\":{\"type\":\"integer\",\"description\":\"Compare to this number instead of to the previous value.\"},"
	  "\"signed\":{\"type\":\"boolean\",\"description\":\"Compare the values as signed numbers. false when left out.\"}"
	  "},\"required\":[\"op\"],\"additionalProperties\":false}",
	  false, false },
	{ "search_results",
	  "The candidates left, lowest address first: each with its value now and its previous value. "
	  "At most 'limit' are returned; 'candidates' says how many there are.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":500,\"description\":\"50 when left out.\"},"
	  "\"signed\":{\"type\":\"boolean\",\"description\":\"Show the values as signed numbers. false when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "search_undo",
	  "Takes the last search_filter back.",
	  kNoArguments, false, false },
	{ "list_branches",
	  "The project's branches: for each, its number, the frame it was taken on, when, and its note.",
	  kNoArguments, false, true },
	{ "list_markers",
	  "The project's markers: for each, its frame, its text, and whether a person placed it "
	  "(every movie has three of its own: the run's start, its last input and its end).",
	  kNoArguments, false, true },

	/* The tools that change something. They are refused until the person
	 * using Chimera allows changes, and before the first of them a branch of
	 * the movie as it was is saved. */
	{ "seek",
	  "Moves the playback position to a frame, back or forward, and answers when the machine is there. "
	  "Going back uses the saved states; going forward plays the movie's input.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "advance_until",
	  "Plays the movie forward from the frame the machine is on until a value meets a condition, and stops there. "
	  "The value is a memory value (domain, address, type) or a game core's property. 'op' compares it with 'value'; "
	  "the op 'changed' needs no value and stops when it differs from what it was at the start. It stops at the movie's "
	  "end or after max_frames if the condition is never met, and says which.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A domain or bus from list_memory. Give this with 'address', or give 'property'.\"},"
	  "\"address\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"],\"description\":\"u8 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"},"
	  "\"property\":{\"type\":\"string\",\"description\":\"A game core's named value, in place of domain and address.\"},"
	  "\"op\":{\"type\":\"string\",\"enum\":[\"equal\",\"not_equal\",\"greater\",\"greater_or_equal\",\"less\",\"less_or_equal\",\"changed\"]},"
	  "\"value\":{\"type\":\"number\",\"description\":\"What to compare with. Not needed for 'changed'.\"},"
	  "\"max_frames\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000,\"description\":\"The most frames to play. 600 when left out.\"}"
	  "},\"required\":[\"op\"],\"additionalProperties\":false}",
	  true, true },
	{ "set_input",
	  "Replaces the movie's input from a frame on, one line a frame in the form get_input returns. "
	  "Frames past the movie's end are added. It is one step in the undo history. At most 1000 frames a call.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"The first frame to replace.\"},"
	  "\"entries\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"minItems\":1,\"maxItems\":1000}"
	  "},\"required\":[\"frame\",\"entries\"],\"additionalProperties\":false}",
	  true, true },
	{ "set_buttons",
	  "Presses or releases buttons, and sets axes, over a range of frames, by the names 'controls' gives. "
	  "Controls not named keep what they had. It is one step in the undo history.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"The first frame.\"},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10000,\"description\":\"How many frames. 1 when left out.\"},"
	  "\"buttons\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"boolean\"},\"description\":\"Button name to pressed (true) or released (false).\"},"
	  "\"axes\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"integer\"},\"description\":\"Axis name to value.\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "insert_frames",
	  "Inserts empty frames before a frame; the input from there on moves later.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000,\"description\":\"1 when left out.\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "delete_frames",
	  "Deletes frames from a frame on; the input after them moves earlier.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000,\"description\":\"1 when left out.\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "undo",
	  "Undoes the last step in the movie's undo history, whoever made it: the same as Edit > Undo in TAStudio.",
	  kNoArguments, true, true },
	{ "save_branch",
	  "Saves a branch: the movie's input and the machine as they are now, to come back to later.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"note\":{\"type\":\"string\",\"description\":\"A note shown beside the branch.\"}"
	  "},\"additionalProperties\":false}",
	  true, true },
	{ "load_branch",
	  "Loads a branch by its number in list_branches: the movie's input and the machine become what the branch saved.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"number\":{\"type\":\"integer\",\"minimum\":1}"
	  "},\"required\":[\"number\"],\"additionalProperties\":false}",
	  true, true },
	{ "add_marker",
	  "Places a marker with a text on a frame.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"text\":{\"type\":\"string\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "write_memory",
	  "Writes values to a memory domain or a bus at the frame the machine is on. A movie cannot reproduce a memory write: "
	  "it is for testing a guess, and 'status' says from then on that memory was written. At most 4096 bytes a call.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A name from list_memory.\"},"
	  "\"address\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"values\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"},\"minItems\":1},"
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"],\"description\":\"u8 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"}"
	  "},\"required\":[\"domain\",\"address\",\"values\"],\"additionalProperties\":false}",
	  true, false },
	{ "set_property",
	  "Sets one named value of a game core from text: a number, a name the value can take, true or false. "
	  "Like a memory write, a movie cannot reproduce it.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\"},"
	  "\"value\":{\"type\":\"string\"}"
	  "},\"required\":[\"name\",\"value\"],\"additionalProperties\":false}",
	  true, false },
	{ "set_register",
	  "Sets a processor register, for a core that allows it. Like a memory write, a movie cannot reproduce it.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\",\"description\":\"A name from registers.\"},"
	  "\"value\":{\"type\":\"integer\"}"
	  "},\"required\":[\"name\",\"value\"],\"additionalProperties\":false}",
	  true, false },
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

/* A value of the machine an agent names: a place in memory, or a game core's
 * property. What `watch` lists and what advance_until's condition reads. */
struct Source
{
	std::string name;
	bool isProperty = false;
	std::string property;
	std::string domain;
	int64_t address = 0;
	int width = 1;
	bool isSigned = false;
	bool bigEndian = false;
};

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
	int64_t wroteMemoryAt = -1;        /* the frame of the agent's first write to the machine, -1 for none */
	/* Before the agent's first change the frontend is asked to keep the
	 * person's work as it was (a branch). 0 not asked yet, 1 asked and
	 * waiting, 2 kept, 3 could not be kept - and then nothing is changed. */
	int kept = 0;
	uint64_t keepId = 0;
	std::string keepError;
	/* The agent's RAM search. It reads the machine's memory through a pointer,
	 * so what it was started on is remembered and checked before every use:
	 * the caller of ce_mcp_service's alone, like the machine. */
	std::vector<Source> watches;
	/* advance_until's condition, set by the frontend when it begins to play
	 * and asked once per frame until it is met */
	bool conditionSet = false;
	Source conditionSource;
	int conditionOp = 0;               /* 0 ==, 1 >, 2 >=, 3 <, 4 <=, 5 !=, 6 changed */
	double conditionValue = 0;
	std::string conditionStart;        /* the value at the start, as text, for "changed" */
	ce_ramsearch *search = nullptr;
	std::string searchDomain;
	const uint8_t *searchBase = nullptr;
	int64_t searchBytes = 0;
	int searchSize = 1;
	bool searchBigEndian = false;
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

McpResult toolStatus(ce_session *s, const ce_movie_log *log, int64_t frame, const char *hostStatus, bool allowChanges, int64_t wroteMemoryAt)
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
		/* a movie cannot reproduce a write to the machine, so it is said for as
		 * long as the server stays started */
		cJSON_AddBoolToObject(out, "memory_written_by_agent", wroteMemoryAt >= 0);
		if (wroteMemoryAt >= 0) cJSON_AddNumberToObject(out, "first_memory_write_frame", static_cast<double>(wroteMemoryAt));
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

/* The domain or the bus of that name: one index is set, the other is -1. */
bool findMemory(ce_session *s, const std::string &name, int32_t &domainIndex, int32_t &busIndex)
{
	domainIndex = busIndex = -1;
	for (int32_t i = 0; i < ce_session_domain_count(s) && domainIndex < 0; i++)
	{
		if (ce_session_domain_hidden(s, i) == 0 && name == ce_session_domain_name(s, i)) domainIndex = i;
	}
	for (int32_t i = 0; domainIndex < 0 && i < ce_session_bus_count(s) && busIndex < 0; i++)
	{
		if (name == ce_session_bus_name(s, i)) busIndex = i;
	}
	return domainIndex >= 0 || busIndex >= 0;
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

	int32_t domainIndex = -1, busIndex = -1;
	findMemory(s, domain, domainIndex, busIndex);
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

McpResult toolWriteMemory(ce_session *s, const cJSON *arguments)
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
	int64_t address = 0;
	if (!integerArgument(arguments, "address", 0, INT64_MAX / 8, 0, address)) return mcpError("'address' must be 0 or more.");
	const cJSON *values = cJSON_GetObjectItemCaseSensitive(arguments, "values");
	const int64_t count = cJSON_IsArray(values) ? cJSON_GetArraySize(values) : 0;
	if (count < 1 || count * width > 4096) return mcpError("'values' must hold at least one value, and they must fit in 4096 bytes.");

	/* every value is checked before a byte is written */
	const double low = isSigned ? -static_cast<double>(1ULL << (8 * width - 1)) : 0;
	const double high = isSigned ? static_cast<double>((1ULL << (8 * width - 1)) - 1) : static_cast<double>((1ULL << (8 * width)) - 1);
	std::vector<uint8_t> bytes(static_cast<size_t>(count * width));
	int64_t at = 0;
	const cJSON *item = nullptr;
	cJSON_ArrayForEach(item, values)
	{
		if (!cJSON_IsNumber(item) || item->valuedouble != static_cast<double>(static_cast<int64_t>(item->valuedouble))
			|| item->valuedouble < low || item->valuedouble > high)
			return mcpError("Every value must be a whole number that fits in " + type + ".");
		const uint32_t value = static_cast<uint32_t>(static_cast<int64_t>(item->valuedouble));
		for (int64_t b = 0; b < width; b++)
		{
			bytes[static_cast<size_t>(at * width + b)] = static_cast<uint8_t>(value >> (8 * (bigEndian ? width - 1 - b : b)));
		}
		at++;
	}

	int32_t domainIndex = -1, busIndex = -1;
	if (!findMemory(s, domain, domainIndex, busIndex))
		return mcpError("There is no domain or bus called '" + domain + "'. list_memory names them.");
	const int64_t size = domainIndex >= 0 ? ce_session_domain_size(s, domainIndex) : ce_session_bus_size(s, busIndex);
	if (address + static_cast<int64_t>(bytes.size()) > size)
		return mcpError("'" + domain + "' is " + std::to_string(size) + " bytes, and the write goes past its end.");
	if (domainIndex >= 0)
	{
		/* the same way the frontend's own memory tools write: through the
		 * domain's memory itself */
		uint8_t *memory = reinterpret_cast<uint8_t *>(ce_session_domain_ptr(s, domainIndex));
		if (ce_session_domain_writable(s, domainIndex) == 0 || memory == nullptr)
			return mcpError("'" + domain + "' cannot be written.");
		std::memcpy(memory + address, bytes.data(), bytes.size());
	}
	else
	{
		if (ce_session_bus_writable(s, busIndex) == 0) return mcpError("'" + domain + "' cannot be written.");
		for (size_t i = 0; i < bytes.size(); i++)
		{
			ce_session_bus_poke(s, busIndex, static_cast<int32_t>(address + static_cast<int64_t>(i)), bytes[i]);
		}
	}
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", domain.c_str());
	cJSON_AddNumberToObject(out, "address", static_cast<double>(address));
	cJSON_AddNumberToObject(out, "bytes_written", static_cast<double>(bytes.size()));
	return textResult(out);
}

McpResult toolSetProperty(ce_session *s, const cJSON *arguments)
{
	const cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	const cJSON *valueItem = cJSON_GetObjectItemCaseSensitive(arguments, "value");
	if (!cJSON_IsString(nameItem) || !cJSON_IsString(valueItem)) return mcpError("'name' and 'value' must both be text.");
	if (ce_session_property_dynamic(s) != 0) ce_session_property_refresh(s);
	uint32_t element = 0;
	const int32_t index = ce_session_property_find(s, nameItem->valuestring, &element);
	if (index < 0) return mcpError(std::string("There is no property called '") + nameItem->valuestring + "'. list_properties names them.");
	if (ce_session_property_set_text(s, index, element, valueItem->valuestring) != 0)
	{
		const char *why = ce_session_last_error(s);
		return mcpError(std::string("'") + nameItem->valuestring + "' was not set: " + (why != nullptr && why[0] != '\0' ? why : "the value does not fit it") + ".");
	}
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "name", nameItem->valuestring);
	cJSON_AddStringToObject(out, "value", propertyText(s, index, element).c_str());
	return textResult(out);
}

McpResult toolSetRegister(ce_session *s, const cJSON *arguments)
{
	const cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	const cJSON *valueItem = cJSON_GetObjectItemCaseSensitive(arguments, "value");
	if (!cJSON_IsString(nameItem) || !cJSON_IsNumber(valueItem)) return mcpError("'name' must be a register from registers, and 'value' a whole number.");
	for (int32_t i = 0; i < ce_session_register_count(s); i++)
	{
		if (std::strcmp(nameItem->valuestring, ce_session_register_name(s, i)) != 0) continue;
		if (ce_session_register_set(s, i, static_cast<int64_t>(valueItem->valuedouble)) != 0)
			return mcpError("This core does not allow its registers to be written.");
		cJSON *out = cJSON_CreateObject();
		cJSON_AddStringToObject(out, "name", nameItem->valuestring);
		cJSON_AddNumberToObject(out, "value", static_cast<double>(ce_session_register_value(s, i)));
		return textResult(out);
	}
	return mcpError(std::string("There is no register called '") + nameItem->valuestring + "'. registers names them.");
}

double searchNumber(uint32_t raw, int size, bool isSigned)
{
	if (!isSigned) return raw;
	return size == 1 ? static_cast<double>(static_cast<int8_t>(raw))
		: size == 2 ? static_cast<double>(static_cast<int16_t>(raw))
		: static_cast<double>(static_cast<int32_t>(raw));
}

/* ---- named values: watch, and advance_until's condition ---- */

/* Reads what an agent named out of a tool's arguments. False with `why`. */
bool parseSource(const cJSON *arguments, Source &source, std::string &why)
{
	const cJSON *property = cJSON_GetObjectItemCaseSensitive(arguments, "property");
	const cJSON *domain = cJSON_GetObjectItemCaseSensitive(arguments, "domain");
	const cJSON *name = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	if (cJSON_IsString(property) && domain == nullptr)
	{
		source.isProperty = true;
		source.property = property->valuestring;
		source.name = cJSON_IsString(name) ? name->valuestring : source.property;
		return true;
	}
	if (!cJSON_IsString(domain) || property != nullptr)
	{
		why = "Give either 'domain' and 'address', or 'property'.";
		return false;
	}
	const cJSON *typeItem = cJSON_GetObjectItemCaseSensitive(arguments, "type");
	const std::string type = cJSON_IsString(typeItem) ? typeItem->valuestring : "u8";
	if (type.size() < 2 || (type[0] != 'u' && type[0] != 's')
		|| (type.substr(1) != "8" && type.substr(1) != "16" && type.substr(1) != "32"))
	{
		why = "'type' must be one of u8, s8, u16, s16, u32, s32.";
		return false;
	}
	if (!integerArgument(arguments, "address", 0, INT64_MAX / 8, -1, source.address) || source.address < 0)
	{
		why = "'address' must be given with 'domain', and be 0 or more.";
		return false;
	}
	source.domain = domain->valuestring;
	source.isSigned = type[0] == 's';
	source.width = type.substr(1) == "8" ? 1 : type.substr(1) == "16" ? 2 : 4;
	source.bigEndian = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "big_endian"));
	char place[64];
	std::snprintf(place, sizeof(place), " 0x%llX ", static_cast<unsigned long long>(source.address));
	source.name = cJSON_IsString(name) ? name->valuestring : source.domain + place + type;
	return true;
}

/* The value now: as text always, and as a number when it is one. False with
 * `why` when it cannot be read (no such domain, past its end, no such
 * property). */
bool readSource(ce_session *s, const Source &source, std::string &text, bool &isNumber, double &number, std::string &why)
{
	isNumber = false;
	if (source.isProperty)
	{
		if (ce_session_property_dynamic(s) != 0) ce_session_property_refresh(s);
		uint32_t element = 0;
		const int32_t index = ce_session_property_find(s, source.property.c_str(), &element);
		if (index < 0)
		{
			why = "there is no property called '" + source.property + "'";
			return false;
		}
		text = propertyText(s, index, element);
		ce_property_value value;
		std::memset(&value, 0, sizeof(value));
		if (ce_session_property_get(s, index, element, &value) == 0)
		{
			isNumber = value.kind == CE_PROPERTY_INT || value.kind == CE_PROPERTY_UINT
				|| value.kind == CE_PROPERTY_FLOAT || value.kind == CE_PROPERTY_BOOL;
			number = value.kind == CE_PROPERTY_UINT ? static_cast<double>(value.u)
				: value.kind == CE_PROPERTY_FLOAT ? value.f : static_cast<double>(value.i);
		}
		return true;
	}
	int32_t domainIndex = -1, busIndex = -1;
	if (!findMemory(s, source.domain, domainIndex, busIndex))
	{
		why = "there is no domain or bus called '" + source.domain + "'";
		return false;
	}
	uint8_t bytes[4] = { 0, 0, 0, 0 };
	const int64_t got = domainIndex >= 0
		? ce_session_domain_read(s, domainIndex, source.address, bytes, source.width)
		: ce_session_bus_read(s, busIndex, source.address, bytes, source.width);
	if (got != source.width)
	{
		why = "'" + source.domain + "' has no such address";
		return false;
	}
	uint32_t raw = 0;
	for (int b = 0; b < source.width; b++)
	{
		raw |= static_cast<uint32_t>(bytes[b]) << (8 * (source.bigEndian ? source.width - 1 - b : b));
	}
	number = searchNumber(raw, source.width, source.isSigned);
	isNumber = true;
	char digits[32];
	std::snprintf(digits, sizeof(digits), "%.0f", number);
	text = digits;
	return true;
}

/* [{"name":..,"value":..}] for the watches, a value that cannot be read
 * carrying "error" in place of "value". The caller owns the array. */
cJSON *watchValues(Service &sv, ce_session *s)
{
	cJSON *list = cJSON_CreateArray();
	for (const Source &watch : sv.watches)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", watch.name.c_str());
		std::string text, why;
		bool isNumber = false;
		double number = 0;
		if (!readSource(s, watch, text, isNumber, number, why)) cJSON_AddStringToObject(item, "error", why.c_str());
		else if (isNumber && !watch.isProperty) cJSON_AddNumberToObject(item, "value", number);
		else cJSON_AddStringToObject(item, "value", text.c_str());
		cJSON_AddItemToArray(list, item);
	}
	return list;
}

/* The tools whose answer lands the machine on a frame carry the watches. */
bool carriesWatches(const std::string &tool)
{
	return tool == "status" || tool == "seek" || tool == "advance_until" || tool == "load_branch";
}

/* Adds "watches" to an answer that is a JSON object. Anything else is left
 * as it is. */
std::string withWatches(Service &sv, ce_session *s, const std::string &text)
{
	if (s == nullptr || sv.watches.empty()) return text;
	cJSON *object = cJSON_Parse(text.c_str());
	if (!cJSON_IsObject(object))
	{
		cJSON_Delete(object);
		return text;
	}
	cJSON_AddItemToObject(object, "watches", watchValues(sv, s));
	std::string out = printJson(object);
	cJSON_Delete(object);
	return out;
}

McpResult toolWatch(Service &sv, ce_session *s, const cJSON *arguments)
{
	const cJSON *list = cJSON_GetObjectItemCaseSensitive(arguments, "watches");
	if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > 32) return mcpError("'watches' must be a list of at most 32 values to watch.");
	std::vector<Source> watches;
	const cJSON *item = nullptr;
	cJSON_ArrayForEach(item, list)
	{
		Source source;
		std::string why;
		if (!cJSON_IsObject(item) || !parseSource(item, source, why))
			return mcpError("Watch " + std::to_string(watches.size() + 1) + ": " + (why.empty() ? "it must be an object." : why));
		watches.push_back(source);
	}
	sv.watches = watches;
	cJSON *out = cJSON_CreateObject();
	cJSON_AddItemToObject(out, "watches", watchValues(sv, s));
	return textResult(out);
}

/* ---- the editing tools: what they mean ----
 *
 * set_input, set_buttons, insert_frames and delete_frames are run by whoever
 * holds the movie: TAStudio in the window, the engine itself where there is
 * no window. What they MEAN is decided here, once, before either is asked:
 * the arguments are checked against the movie and the machine's controls,
 * set_buttons is turned into the lines it amounts to, and what is handed on
 * is one of three plain steps - set these lines from this frame, insert this
 * many frames here, delete this many there - with the name the step has in
 * the undo history. The holder of the movie only applies it. */

const char kNoMovie[] = "No project is open in Chimera.";

std::string labelled(const char *what, int64_t frame, int64_t count)
{
	return std::string("Agent: ") + what + (count == 1
		? ", frame " + std::to_string(frame)
		: ", frames " + std::to_string(frame) + " to " + std::to_string(frame + count - 1));
}

/* Whether a line has the columns of this machine's lines: as many characters
 * between its bars as the empty line has, and readable as one. The bars are
 * not counted, because the engine and the interface do not write them alike:
 * for a machine with no console buttons the engine writes an empty first
 * group ("||....|") and the interface leaves it out ("|....|"). Both read
 * either. */
bool lineFits(ce_session *s, const std::string &line, const std::string &blank)
{
	const auto columns = [](const std::string &text)
	{
		return std::count_if(text.begin(), text.end(), [](char c) { return c != '|'; });
	};
	return !line.empty() && line.front() == '|' && line.back() == '|' && columns(line) == columns(blank)
		&& ce_session_movie_entry_decode_wide(s, line.c_str(), nullptr, nullptr) == 0;
}

/* Hands back the step for the movie's holder as (tool, arguments), or an
 * error. `tool` comes in as the agent's and goes out as the holder's. */
bool prepareEdit(ce_session *s, const ce_movie_log *log, std::string &tool, std::string &argumentsJson, McpResult &refusal)
{
	auto refuse = [&](const std::string &why)
	{
		refusal = mcpError(why);
		return false;
	};
	if (s == nullptr || log == nullptr) return refuse(kNoMovie);
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	const int64_t length = ce_movie_log_count(log);
	const std::string blank = sessionMovieEntry(s, nullptr, nullptr);
	cJSON *step = cJSON_CreateObject();
	std::string why;
	int64_t frame = 0, count = 0;

	if (tool == "insert_frames" || tool == "delete_frames")
	{
		const bool inserting = tool == "insert_frames";
		if (!integerArgument(arguments, "frame", 0, inserting ? length : length - 1, -1, frame) || frame < 0)
			why = inserting ? "'frame' must be between 0 and the movie's length, " + std::to_string(length) + "."
				: length == 0 ? "The movie has no frames."
				: "'frame' must be a frame of the movie, which has " + std::to_string(length) + ".";
		else if (!integerArgument(arguments, "count", 1, 100000, 1, count)) why = "'count' must be between 1 and 100000.";
		else
		{
			/* deleting past the end stops at the end */
			if (!inserting && count > length - frame) count = length - frame;
			cJSON_AddNumberToObject(step, "frame", static_cast<double>(frame));
			cJSON_AddNumberToObject(step, "count", static_cast<double>(count));
			cJSON_AddStringToObject(step, "label", labelled(inserting ? "insert frames" : "delete frames", frame, count).c_str());
		}
	}
	else if (tool == "set_input")
	{
		const cJSON *entries = cJSON_GetObjectItemCaseSensitive(arguments, "entries");
		count = cJSON_IsArray(entries) ? cJSON_GetArraySize(entries) : 0;
		if (!integerArgument(arguments, "frame", 0, INT32_MAX - 2000, -1, frame) || frame < 0) why = "'frame' must be a frame number, 0 or more.";
		else if (count < 1 || count > 1000) why = "'entries' must be between 1 and 1000 lines of input, as get_input returns them.";
		else
		{
			/* every line is checked before one is written */
			int64_t at = 0;
			const cJSON *entry = nullptr;
			cJSON_ArrayForEach(entry, entries)
			{
				at++;
				if (cJSON_IsString(entry) && lineFits(s, entry->valuestring, blank)) continue;
				why = "Line " + std::to_string(at) + " of 'entries' does not have the shape of this movie's input lines. get_input shows them.";
				break;
			}
			if (why.empty())
			{
				cJSON_AddNumberToObject(step, "frame", static_cast<double>(frame));
				cJSON_AddItemToObject(step, "entries", cJSON_Duplicate(entries, 1));
				cJSON_AddStringToObject(step, "label", labelled("set input", frame, count).c_str());
			}
		}
	}
	else /* set_buttons: the lines it amounts to */
	{
		const size_t buttonCount = static_cast<size_t>(ce_session_button_count(s));
		const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s));
		std::vector<int> buttonAsked(buttonCount, -1); /* -1 keep, 0 release, 1 press */
		std::vector<bool> axisAsked(axisCount, false);
		std::vector<int32_t> axisValue(axisCount, 0);
		bool any = false;
		const cJSON *asked = nullptr;
		cJSON_ArrayForEach(asked, cJSON_GetObjectItemCaseSensitive(arguments, "buttons"))
		{
			size_t index = buttonCount;
			for (size_t i = 0; i < buttonCount && index == buttonCount; i++)
			{
				if (std::strcmp(asked->string, ce_session_button_name(s, static_cast<int64_t>(i))) == 0) index = i;
			}
			if (index == buttonCount || ce_session_button_active(s, static_cast<int64_t>(index)) == 0)
				why = std::string("There is no button called '") + asked->string + "'. 'controls' names them.";
			else if (!cJSON_IsBool(asked)) why = std::string("The button '") + asked->string + "' must be true (pressed) or false (released).";
			else buttonAsked[index] = cJSON_IsTrue(asked) ? 1 : 0;
			any = true;
			if (!why.empty()) break;
		}
		const cJSON *askedAxes = why.empty() ? cJSON_GetObjectItemCaseSensitive(arguments, "axes") : nullptr;
		cJSON_ArrayForEach(asked, askedAxes)
		{
			size_t index = axisCount;
			for (size_t i = 0; i < axisCount && index == axisCount; i++)
			{
				if (std::strcmp(asked->string, ce_session_axis_name(s, static_cast<int64_t>(i))) == 0) index = i;
			}
			if (index == axisCount || ce_session_axis_active(s, static_cast<int64_t>(index)) == 0)
				why = std::string("There is no axis called '") + asked->string + "'. 'controls' names them.";
			else if (!cJSON_IsNumber(asked) || asked->valuedouble != static_cast<double>(static_cast<int32_t>(asked->valuedouble)))
				why = std::string("The axis '") + asked->string + "' must be a whole number.";
			else
			{
				axisAsked[index] = true;
				axisValue[index] = static_cast<int32_t>(asked->valuedouble);
			}
			any = true;
			if (!why.empty()) break;
		}
		if (!why.empty()) {}
		else if (!integerArgument(arguments, "frame", 0, INT32_MAX - 20000, -1, frame) || frame < 0) why = "'frame' must be a frame number, 0 or more.";
		else if (!integerArgument(arguments, "count", 1, 10000, 1, count)) why = "'count' must be between 1 and 10000.";
		else if (!any) why = "Name at least one button in 'buttons' or one axis in 'axes'.";
		else
		{
			cJSON *entries = cJSON_CreateArray();
			std::vector<uint8_t> states(buttonCount);
			std::vector<int32_t> values(axisCount);
			for (int64_t i = 0; i < count && why.empty(); i++)
			{
				/* the line the frame has now, or the empty one past the movie's end */
				const char *now = frame + i < length ? ce_movie_log_entry(log, frame + i) : nullptr;
				if (ce_session_movie_entry_decode_wide(s, now != nullptr ? now : blank.c_str(), states.data(), values.data()) != 0)
				{
					why = "The movie's input on frame " + std::to_string(frame + i) + " cannot be read.";
					break;
				}
				for (size_t b = 0; b < buttonCount; b++) if (buttonAsked[b] >= 0) states[b] = static_cast<uint8_t>(buttonAsked[b]);
				for (size_t a = 0; a < axisCount; a++) if (axisAsked[a]) values[a] = axisValue[a];
				cJSON_AddItemToArray(entries, cJSON_CreateString(sessionMovieEntry(s, states.data(), values.data()).c_str()));
			}
			cJSON_AddNumberToObject(step, "frame", static_cast<double>(frame));
			cJSON_AddItemToObject(step, "entries", entries);
			cJSON_AddStringToObject(step, "label", labelled("set controls", frame, count).c_str());
			tool = "set_input";
		}
	}
	cJSON_Delete(arguments);
	if (!why.empty())
	{
		cJSON_Delete(step);
		return refuse(why);
	}
	argumentsJson = printJson(step);
	cJSON_Delete(step);
	return true;
}

bool isEdit(const std::string &tool)
{
	return tool == "set_input" || tool == "set_buttons" || tool == "insert_frames" || tool == "delete_frames";
}

/* ---- RAM search ---- */

void dropSearch(Service &sv)
{
	if (sv.search != nullptr) ce_ramsearch_destroy(sv.search);
	sv.search = nullptr;
	sv.searchBase = nullptr;
}

/* Whether the search still stands on the memory it was started on. A project
 * closed, or another one opened, takes that memory away; the search is then
 * dropped before anything reads through its pointer. */
bool searchStands(Service &sv, ce_session *s)
{
	if (sv.search == nullptr) return false;
	bool stands = false;
	for (int32_t i = 0; s != nullptr && i < ce_session_domain_count(s) && !stands; i++)
	{
		stands = sv.searchDomain == ce_session_domain_name(s, i)
			&& reinterpret_cast<const uint8_t *>(ce_session_domain_ptr(s, i)) == sv.searchBase
			&& ce_session_domain_size(s, i) == sv.searchBytes;
	}
	if (!stands) dropSearch(sv);
	return stands;
}

const char kNoSearch[] = "No search is running. search_start begins one (a search ends when the project is closed).";

McpResult toolSearchStart(Service &sv, ce_session *s, const cJSON *arguments)
{
	const cJSON *domainItem = cJSON_GetObjectItemCaseSensitive(arguments, "domain");
	if (!cJSON_IsString(domainItem)) return mcpError("'domain' must be a domain from list_memory.");
	int64_t size = 1;
	if (!integerArgument(arguments, "size", 1, 4, 1, size) || size == 3) return mcpError("'size' must be 1, 2 or 4.");
	int32_t domainIndex = -1, busIndex = -1;
	findMemory(s, domainItem->valuestring, domainIndex, busIndex);
	if (domainIndex < 0)
		return mcpError(std::string("'") + domainItem->valuestring + (busIndex >= 0
			? "' is a bus. A search runs over a domain; list_memory names them."
			: "' is not a domain. list_memory names them."));
	const uint8_t *base = reinterpret_cast<const uint8_t *>(ce_session_domain_ptr(s, domainIndex));
	if (base == nullptr) return mcpError(std::string("'") + domainItem->valuestring + "' cannot be searched: the core gives it no memory of its own.");
	dropSearch(sv);
	sv.searchBytes = ce_session_domain_size(s, domainIndex);
	sv.search = ce_ramsearch_create(base, nullptr, nullptr, sv.searchBytes);
	sv.searchDomain = domainItem->valuestring;
	sv.searchBase = base;
	sv.searchSize = static_cast<int>(size);
	sv.searchBigEndian = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "big_endian"));
	if (sv.search != nullptr) ce_ramsearch_set_undo_enabled(sv.search, 1);
	if (sv.search == nullptr || ce_ramsearch_start(sv.search, sv.searchSize, 0, sv.searchBigEndian ? 1 : 0, 0) != 0)
	{
		dropSearch(sv);
		return mcpError("There is not enough memory to search that domain.");
	}
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", sv.searchDomain.c_str());
	cJSON_AddNumberToObject(out, "size", sv.searchSize);
	cJSON_AddNumberToObject(out, "candidates", static_cast<double>(ce_ramsearch_count(sv.search)));
	return textResult(out);
}

McpResult toolSearchFilter(Service &sv, ce_session *s, const cJSON *arguments)
{
	if (!searchStands(sv, s)) return mcpError(kNoSearch);
	const cJSON *opItem = cJSON_GetObjectItemCaseSensitive(arguments, "op");
	const std::string opName = cJSON_IsString(opItem) ? opItem->valuestring : "";
	/* the engine's numbering (engine.h, RAM Search): 0 ==, 1 >, 2 >=, 3 <, 4 <=, 5 != */
	const int op = opName == "equal" ? 0 : opName == "greater" ? 1 : opName == "greater_or_equal" ? 2
		: opName == "less" ? 3 : opName == "less_or_equal" ? 4 : opName == "not_equal" ? 5 : -1;
	if (op < 0) return mcpError("'op' must be one of equal, not_equal, greater, greater_or_equal, less, less_or_equal.");
	const bool isSigned = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "signed"));
	const cJSON *valueItem = cJSON_GetObjectItemCaseSensitive(arguments, "value");
	const bool toValue = valueItem != nullptr && !cJSON_IsNull(valueItem);
	int64_t value = 0;
	if (toValue && !integerArgument(arguments, "value", INT32_MIN, UINT32_MAX, 0, value)) return mcpError("'value' must be a whole number.");
	/* compare: 0 to the previous value, 1 to a given value; previous type 1:
	 * after the search, a candidate's previous value is its value now */
	const int64_t removed = ce_ramsearch_search(sv.search, toValue ? 1 : 0, op, isSigned ? 1 : 0,
		static_cast<uint32_t>(value), 0, 1);
	if (removed < 0) return mcpError("There is not enough memory to run that search.");
	cJSON *out = cJSON_CreateObject();
	cJSON_AddNumberToObject(out, "removed", static_cast<double>(removed));
	cJSON_AddNumberToObject(out, "candidates", static_cast<double>(ce_ramsearch_count(sv.search)));
	return textResult(out);
}

McpResult toolSearchResults(Service &sv, ce_session *s, const cJSON *arguments)
{
	if (!searchStands(sv, s)) return mcpError(kNoSearch);
	int64_t limit = 0;
	if (!integerArgument(arguments, "limit", 1, 500, 50, limit)) return mcpError("'limit' must be between 1 and 500.");
	const bool isSigned = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "signed"));
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", sv.searchDomain.c_str());
	cJSON_AddNumberToObject(out, "size", sv.searchSize);
	const int64_t count = ce_ramsearch_count(sv.search);
	cJSON_AddNumberToObject(out, "candidates", static_cast<double>(count));
	cJSON *results = cJSON_CreateArray();
	for (int64_t i = 0; i < count && i < limit; i++)
	{
		uint64_t address = 0;
		uint32_t current = 0, previous = 0, changes = 0;
		if (ce_ramsearch_row(sv.search, i, &address, &current, &previous, &changes) == 0) break;
		/* the value now is read from the machine itself: the search keeps
		 * only what it needs to compare with */
		uint32_t now = 0;
		for (int b = 0; b < sv.searchSize; b++)
		{
			now |= static_cast<uint32_t>(sv.searchBase[address + static_cast<uint64_t>(b)])
				<< (8 * (sv.searchBigEndian ? sv.searchSize - 1 - b : b));
		}
		cJSON *row = cJSON_CreateObject();
		cJSON_AddNumberToObject(row, "address", static_cast<double>(address));
		cJSON_AddNumberToObject(row, "value", searchNumber(now, sv.searchSize, isSigned));
		cJSON_AddNumberToObject(row, "previous", searchNumber(previous, sv.searchSize, isSigned));
		cJSON_AddItemToArray(results, row);
	}
	cJSON_AddItemToObject(out, "results", results);
	return textResult(out);
}

McpResult toolSearchUndo(Service &sv, ce_session *s)
{
	if (!searchStands(sv, s)) return mcpError(kNoSearch);
	if (ce_ramsearch_can_undo(sv.search) == 0) return mcpError("There is no search_filter to take back.");
	ce_ramsearch_undo(sv.search);
	cJSON *out = cJSON_CreateObject();
	cJSON_AddNumberToObject(out, "candidates", static_cast<double>(ce_ramsearch_count(sv.search)));
	return textResult(out);
}

/* A tool that writes to the machine, and the note that it did. */
McpResult runWriteTool(Service &sv, const std::string &tool, ce_session *s, int64_t frame, const cJSON *arguments)
{
	const McpResult result = tool == "write_memory" ? toolWriteMemory(s, arguments)
		: tool == "set_property" ? toolSetProperty(s, arguments)
		: toolSetRegister(s, arguments);
	if (!result.isError)
	{
		std::lock_guard<std::mutex> hold(sv.lock);
		if (sv.wroteMemoryAt < 0) sv.wroteMemoryAt = frame >= 0 ? frame : ce_session_frame(s);
	}
	return result;
}

McpResult runEngineTool(Service &sv, const std::string &tool, ce_session *s, const ce_movie_log *log,
	int64_t frame, const char *hostStatus, const cJSON *arguments)
{
	if (tool == "status")
	{
		McpResult result = toolStatus(s, log, frame, hostStatus, sv.allowChanges.load(), sv.wroteMemoryAt);
		if (s != nullptr && !sv.watches.empty())
		{
			cJSON *content = cJSON_Parse(result.content.c_str());
			const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
			if (cJSON_IsString(text)) result.content = mcpTextContent(withWatches(sv, s, text->valuestring));
			cJSON_Delete(content);
		}
		return result;
	}
	if (tool == "get_input") return toolGetInput(log, arguments);
	/* a search whose memory is gone is dropped before anything else runs */
	searchStands(sv, s);
	if (s == nullptr) return mcpError(kNoMachine);
	if (tool == "controls") return toolControls(s, log);
	if (tool == "screenshot") return toolScreenshot(s, frame, arguments);
	if (tool == "list_memory") return toolListMemory(s);
	if (tool == "read_memory") return toolReadMemory(s, arguments);
	if (tool == "registers") return toolRegisters(s);
	if (tool == "list_properties") return toolListProperties(s, arguments);
	if (tool == "get_property") return toolGetProperty(s, arguments);
	if (tool == "write_memory" || tool == "set_property" || tool == "set_register") return runWriteTool(sv, tool, s, frame, arguments);
	if (tool == "watch") return toolWatch(sv, s, arguments);
	if (tool == "search_start") return toolSearchStart(sv, s, arguments);
	if (tool == "search_filter") return toolSearchFilter(sv, s, arguments);
	if (tool == "search_results") return toolSearchResults(sv, s, arguments);
	if (tool == "search_undo") return toolSearchUndo(sv, s);
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
		sv.wroteMemoryAt = -1;
		sv.kept = 0;
		sv.keepError.clear();
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
	/* the agent's search ends with the server (this is the thread that runs
	 * the machine, as every call of ce_mcp_service is) */
	chimera::dropSearch(sv);
	sv.watches.clear();
	sv.conditionSet = false;
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
			if (tool->changes && sv.kept == 0)
			{
				/* The first change since the server was started. The call goes
				 * back to the head of the line, and the frontend is asked first
				 * to keep the person's work as it is now. Its answer decides
				 * whether the call is then run. */
				sv.outgoing.pop_back(); /* not started after all */
				sv.waiting.push_front(std::move(call));
				sv.pending++;
				sv.kept = 1;
				sv.keepId = UINT64_MAX - sv.logTotal;
				*heldTool = "keep_work";
				*heldArguments = "{}";
				if (id_out != nullptr) *id_out = sv.keepId;
				if (tool_out != nullptr) *tool_out = heldTool->c_str();
				if (arguments_out != nullptr) *arguments_out = heldArguments->c_str();
				return 1;
			}
			if (tool->changes && sv.kept == 1)
			{
				/* the frontend has not answered yet: the call waits its turn */
				sv.outgoing.pop_back();
				sv.waiting.push_front(std::move(call));
				sv.pending++;
				return 0;
			}
			if (tool->changes && sv.kept == 3)
			{
				chimera::finish(sv, call, chimera::mcpError(
					"Nothing was changed: Chimera could not keep a copy of the work as it was before the agent's first change ("
					+ sv.keepError + "). Stop and start the server to try again."));
				continue;
			}
			if (tool->host)
			{
				*heldTool = call.tool;
				*heldArguments = call.arguments;
				/* an edit is checked, and worked out into the step it amounts
				 * to, before the movie's holder is asked to make it */
				chimera::McpResult refusal;
				if (chimera::isEdit(call.tool) && !chimera::prepareEdit(s, log, *heldTool, *heldArguments, refusal))
				{
					chimera::finish(sv, call, refusal);
					continue;
				}
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

void ce_mcp_answer(ce_session *s, uint64_t id, const char *text, int32_t is_error)
{
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	if (sv.kept == 1 && id == sv.keepId)
	{
		/* the frontend's answer to "keep the person's work": not a call of the
		 * agent's, so nothing is sent; the call that waited is run next */
		sv.kept = is_error != 0 ? 3 : 2;
		sv.keepError = is_error != 0 && text != nullptr ? text : "";
		chimera::addLog(sv, is_error != 0
			? std::string("the work could not be kept before the agent's first change: ") + sv.keepError
			: std::string("before the agent's first change, the work as it was has been kept: ") + (text != nullptr ? text : ""));
		return;
	}
	const auto found = sv.withHost.find(id);
	if (found == sv.withHost.end()) return;
	chimera::McpResult result;
	std::string answer = text != nullptr ? text : "";
	if (is_error == 0 && chimera::carriesWatches(found->second.tool)) answer = chimera::withWatches(sv, s, answer);
	result.content = chimera::mcpTextContent(answer);
	result.isError = is_error != 0;
	chimera::finish(sv, found->second, result);
	sv.withHost.erase(found);
}

int32_t ce_mcp_condition_set(ce_session *s, const char *arguments_json, const char **error_out)
{
	static thread_local chimera::ThreadString error;   /* never destroyed: see thread_string.hpp */
	Service &sv = service();
	sv.conditionSet = false;
	auto refuse = [&](const std::string &why)
	{
		*error = why;
		if (error_out != nullptr) *error_out = error->c_str();
		return 0;
	};
	if (s == nullptr) return refuse(chimera::kNoMachine);
	cJSON *arguments = cJSON_Parse(arguments_json != nullptr ? arguments_json : "{}");
	chimera::Source source;
	std::string why;
	const cJSON *opItem = cJSON_GetObjectItemCaseSensitive(arguments, "op");
	const std::string opName = cJSON_IsString(opItem) ? opItem->valuestring : "";
	const int op = opName == "equal" ? 0 : opName == "greater" ? 1 : opName == "greater_or_equal" ? 2
		: opName == "less" ? 3 : opName == "less_or_equal" ? 4 : opName == "not_equal" ? 5 : opName == "changed" ? 6 : -1;
	const cJSON *valueItem = cJSON_GetObjectItemCaseSensitive(arguments, "value");
	std::string text;
	bool isNumber = false;
	double number = 0;
	if (!cJSON_IsObject(arguments) || !chimera::parseSource(arguments, source, why)) why = why.empty() ? "The arguments are not a JSON object." : why;
	else if (op < 0) why = "'op' must be one of equal, not_equal, greater, greater_or_equal, less, less_or_equal, changed.";
	else if (op != 6 && !cJSON_IsNumber(valueItem)) why = "'value' must be a number, for every op but 'changed'.";
	else if (!chimera::readSource(s, source, text, isNumber, number, why)) why = "The value cannot be read: " + why + ".";
	else if (op != 6 && !isNumber) why = "'" + source.name + "' is not a number, so it can only be watched for 'changed'.";
	const double value = cJSON_IsNumber(valueItem) ? valueItem->valuedouble : 0;
	cJSON_Delete(arguments);
	if (!why.empty()) return refuse(why);
	sv.conditionSource = source;
	sv.conditionOp = op;
	sv.conditionValue = value;
	sv.conditionStart = text;
	sv.conditionSet = true;
	return 1;
}

int32_t ce_mcp_condition_met(ce_session *s)
{
	Service &sv = service();
	if (!sv.conditionSet || s == nullptr) return -1;
	std::string text, why;
	bool isNumber = false;
	double number = 0;
	if (!chimera::readSource(s, sv.conditionSource, text, isNumber, number, why)) return -1;
	switch (sv.conditionOp)
	{
		case 0: return number == sv.conditionValue ? 1 : 0;
		case 1: return number > sv.conditionValue ? 1 : 0;
		case 2: return number >= sv.conditionValue ? 1 : 0;
		case 3: return number < sv.conditionValue ? 1 : 0;
		case 4: return number <= sv.conditionValue ? 1 : 0;
		case 5: return number != sv.conditionValue ? 1 : 0;
		default: return text != sv.conditionStart ? 1 : 0;
	}
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
