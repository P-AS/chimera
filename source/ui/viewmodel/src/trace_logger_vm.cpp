#include "chimera/vm/trace_logger_vm.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace chimera::vm {

TraceLoggerViewModel::TraceLoggerViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("traceLogger", dispatcher), session_(session)
{
	expose("logging", "the core traces, and its lines come here (only while this is on)", logging, true);
	expose("capacity", "how many of the latest lines are kept here, 100 to 1,000,000", capacity, true);
	expose("file", "the file the lines go to as they come, \"\" for none", file);
	expose("linesToFile", "how many lines went to the file", linesToFile);
	expose("lineCount", "how many lines are kept here", lineCount);
	expose("status", "what the trace logger says", status);
	rows("lines", "the latest lines: disassembly (what ran), registers (the machine around it)", rows_);
	command("clear", "the lines kept here let go (the file keeps its own)", {}, [this](const Args &, std::string &) -> Value {
		lines_.clear();
		rows_.reset.emit();
		lineCount.set(0);
		sayStatus();
		return {};
	});
	command("logToFile", "the lines from now on go to this file too, as they come", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string path;
		if (!arg(a, 0, path, error)) return {};
		closeFile();
		out_ = std::fopen(path.c_str(), "wb");
		if (out_ == nullptr)
		{
			error = "cannot write " + path;
			return {};
		}
		file.set(path);
		linesToFile.set(0);
		sayStatus();
		return {};
	});
	command("stopFile", "no more lines go to the file", {}, [this](const Args &, std::string &) -> Value {
		closeFile();
		sayStatus();
		return {};
	});
	command("saveVisible", "the lines kept here, written to a file", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string path;
		if (!arg(a, 0, path, error)) return {};
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		for (const Line &l : lines_) out << l.disassembly << (l.registers.empty() ? "" : "  ") << l.registers << "\n";
		if (!out)
		{
			error = "cannot write " + path;
			return {};
		}
		status.set("Saved " + std::to_string(lines_.size()) + " lines to " + path);
		return static_cast<int64_t>(lines_.size());
	});
	loggingWatch_ = logging.changed.connect([this] {
		/* the sink is what turns tracing on in the core */
		if (logging.get()) session_.setTracing(true, [this](std::vector<std::string> lines) { take(std::move(lines)); });
		else session_.setTracing(false, {});
		sayStatus();
	});
	capacityWatch_ = capacity.changed.connect([this] {
		if (capacity.get() < 100 || capacity.get() > 1000000) capacity.set(std::clamp<int64_t>(capacity.get(), 100, 1000000));
		trim();
		rows_.reset.emit();
		lineCount.set(static_cast<int64_t>(lines_.size()));
	});
	sayStatus();
}

TraceLoggerViewModel::~TraceLoggerViewModel()
{
	if (logging.get()) session_.setTracing(false, {});
	closeFile();
}

void TraceLoggerViewModel::take(std::vector<std::string> lines)
{
	for (std::string &line : lines)
	{
		Line l;
		/* what ran, and the machine around it after the first tab */
		const size_t tab = line.find('\t');
		if (tab == std::string::npos)
			l.disassembly = std::move(line);
		else
		{
			l.disassembly = line.substr(0, tab);
			l.registers = line.substr(tab + 1);
		}
		if (out_ != nullptr)
		{
			std::fputs(l.disassembly.c_str(), out_);
			if (!l.registers.empty())
			{
				std::fputs("  ", out_);
				std::fputs(l.registers.c_str(), out_);
			}
			std::fputc('\n', out_);
			linesToFile.set(linesToFile.get() + 1);
		}
		lines_.push_back(std::move(l));
	}
	trim();
	rows_.reset.emit();
	lineCount.set(static_cast<int64_t>(lines_.size()));
	sayStatus();
}

void TraceLoggerViewModel::trim()
{
	while (static_cast<int64_t>(lines_.size()) > capacity.get()) lines_.pop_front();
}

void TraceLoggerViewModel::closeFile()
{
	if (out_ != nullptr) std::fclose(out_);
	out_ = nullptr;
	file.set("");
}

void TraceLoggerViewModel::sayStatus()
{
	std::string s = logging.get() ? "Logging" : "Not logging";
	s += "; " + std::to_string(lines_.size()) + " lines kept";
	if (out_ != nullptr) s += "; " + std::to_string(linesToFile.get()) + " lines to " + file.get();
	status.set(s);
}

Value TraceLoggerViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Line &l = vm_.lines_[static_cast<size_t>(row)];
	return column == 0 ? Value(l.disassembly) : column == 1 ? Value(l.registers) : Value();
}

DebuggerViewModel::DebuggerViewModel(Dispatcher &dispatcher, SessionViewModel &session) : ViewModel("debugger", dispatcher), session_(session)
{
	expose("cycles", "the machine's cycles run, when the core counts them", cycles);
	expose("hasCycles", "the core counts the cycles it runs", hasCycles);
	expose("status", "what the debugger last did", status);
	rows("registers", "the core's registers: name, value, hex (as many digits as the core says), bits", rows_);
	command("refresh", "the registers read again", {}, [this](const Args &, std::string &) -> Value {
		refresh();
		return static_cast<int64_t>(registers_.size());
	});
	command("registerList", "the registers now, a line each: name=value", {}, [this](const Args &, std::string &) -> Value {
		refresh();
		std::string out;
		for (const SessionViewModel::Register &r : registers_) out += r.name + "=" + std::to_string(r.value) + "\n";
		return out;
	});
	command("setRegisterNamed", "a register set by its name", {"name", "value"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		int64_t value = 0;
		if (!arg(a, 0, name, error) || !arg(a, 1, value, error)) return {};
		refresh();
		for (size_t i = 0; i < registers_.size(); i++)
			if (registers_[i].name == name)
			{
				if (!session_.writeRegister(static_cast<int32_t>(i), value))
				{
					error = "this core does not take " + name;
					return {};
				}
				refresh();
				return {};
			}
		error = "no register " + name;
		return {};
	});
	command("setRegister", "a register set, by its place in the list: a number, or hex with 0x", {"index", "value"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t index = 0;
			if (!arg(a, 0, index, error)) return {};
			/* a number, or its text ("0x1F", "31") */
			int64_t value = 0;
			std::string text;
			if (a.size() < 2)
			{
				error = "no value given";
				return {};
			}
			if (!fromValue(a[1], value))
			{
				if (!fromValue(a[1], text))
				{
					error = "a register's value is a number";
					return {};
				}
				try
				{
					size_t used = 0;
					value = static_cast<int64_t>(std::stoull(text, &used, 0));
					if (used != text.size()) throw std::invalid_argument(text);
				}
				catch (...)
				{
					error = text + " is not a number";
					return {};
				}
			}
			if (!session_.writeRegister(static_cast<int32_t>(index), value))
			{
				error = "this core does not take that register";
				return {};
			}
			refresh();
			return {};
		}, &session_.hasRegisters);
}

void DebuggerViewModel::refresh()
{
	int64_t c = 0;
	bool counted = false;
	registers_ = session_.readRegisters(c, counted);
	cycles.set(c);
	hasCycles.set(counted);
	rows_.reset.emit();
}

std::string DebuggerViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "value", "hex", "bits"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value DebuggerViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const SessionViewModel::Register &r = vm_.registers_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return r.name;
	case 1: return r.value;
	case 2:
	{
		const int digits = std::clamp((r.bits + 3) / 4, 1, 16);
		char text[24];
		std::snprintf(text, sizeof text, "%0*llX", digits, static_cast<unsigned long long>(r.value) & (digits >= 16 ? ~0ull : ((1ull << (digits * 4)) - 1)));
		return std::string(text);
	}
	case 3: return static_cast<int64_t>(r.bits);
	default: return {};
	}
}

} // namespace chimera::vm
