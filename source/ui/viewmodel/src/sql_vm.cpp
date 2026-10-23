#include "chimera/vm/sql_vm.hpp"

#include <cstdio>
#include <filesystem>

#include "sqlite3.h"

namespace chimera::vm {

SqlViewModel::SqlViewModel(Dispatcher &dispatcher) : ViewModel("sql", dispatcher)
{
	command("createDatabase", "an empty file for a database; what happened", {"name"}, [](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		std::FILE *f = std::fopen(name.c_str(), "wb");
		if (f == nullptr) return "Could not create " + name;
		std::fclose(f);
		return std::string("Database Created Successfully");
	});
	command("openDatabase", "the database the commands go to; what happened", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		if (db_ != nullptr) sqlite3_close(db_);
		db_ = nullptr;
		std::error_code ec;
		const std::string path = std::filesystem::absolute(name, ec).string();
		if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
		{
			const std::string why = db_ != nullptr ? sqlite3_errmsg(db_) : "out of memory";
			sqlite3_close(db_);
			db_ = nullptr;
			return why;
		}
		/* as Chimera's: reads and writes at the same time, and no waiting on the disk */
		sqlite3_exec(db_, "PRAGMA journal_mode = 'wal'", nullptr, nullptr, nullptr);
		sqlite3_exec(db_, "PRAGMA synchronous = 'off'", nullptr, nullptr, nullptr);
		return std::string("Database Opened Successfully");
	});
	command("writeCommand", "a command that writes (CREATE, INSERT, UPDATE...); what happened", {"query"},
		[this](const Args &a, std::string &error) -> Value {
			std::string query;
			if (!a.empty() && !arg(a, 0, query, error)) return {};
			if (query.find_first_not_of(" \t\r\n") == std::string::npos) return std::string("query is empty");
			if (db_ == nullptr) return std::string("Database not open.");
			char *message = nullptr;
			if (sqlite3_exec(db_, query.c_str(), nullptr, nullptr, &message) != SQLITE_OK)
			{
				const std::string why = message != nullptr ? message : sqlite3_errmsg(db_);
				sqlite3_free(message);
				return why;
			}
			return std::string("Command ran successfully");
		});
	/* every value of every row, as Chimera keys them ("column row"), a record each: the key, its
	 * kind (i, f, s, b or n) and the value, apart by 0x1F, records apart by 0x1E; else what happened */
	command("readCommand", "a command that reads (SELECT): every value of every row, or what happened (the Lua library makes the table)",
		{"query"}, [this](const Args &a, std::string &error) -> Value {
			std::string query;
			if (!a.empty() && !arg(a, 0, query, error)) return {};
			if (query.find_first_not_of(" \t\r\n") == std::string::npos) return std::string("query is empty");
			if (db_ == nullptr) return std::string("Database not open.");
			sqlite3_exec(db_, "PRAGMA read_uncommitted = 1", nullptr, nullptr, nullptr);
			sqlite3_stmt *st = nullptr;
			if (sqlite3_prepare_v2(db_, query.c_str(), -1, &st, nullptr) != SQLITE_OK) return std::string(sqlite3_errmsg(db_));
			std::string out = "\x1E";
			int64_t row = 0;
			int rc = SQLITE_ROW;
			while ((rc = sqlite3_step(st)) == SQLITE_ROW)
			{
				for (int c = 0; c < sqlite3_column_count(st); c++)
				{
					const char *name = sqlite3_column_name(st, c);
					std::string value, kind;
					switch (sqlite3_column_type(st, c))
					{
					case SQLITE_INTEGER: kind = "i"; value = std::to_string(sqlite3_column_int64(st, c)); break;
					case SQLITE_FLOAT:
					{
						char buf[40];
						std::snprintf(buf, sizeof buf, "%.17g", sqlite3_column_double(st, c));
						kind = "f";
						value = buf;
						break;
					}
					case SQLITE_TEXT: kind = "s"; value.assign(reinterpret_cast<const char *>(sqlite3_column_text(st, c)), static_cast<size_t>(sqlite3_column_bytes(st, c))); break;
					case SQLITE_BLOB: kind = "b"; value.assign(static_cast<const char *>(sqlite3_column_blob(st, c)), static_cast<size_t>(sqlite3_column_bytes(st, c))); break;
					default: kind = "n"; break;
					}
					out += std::string(name != nullptr ? name : "") + " " + std::to_string(row) + "\x1F" + kind + "\x1F" + value + "\x1E";
				}
				row++;
			}
			const std::string why = rc == SQLITE_DONE ? std::string() : std::string(sqlite3_errmsg(db_));
			sqlite3_finalize(st);
			if (!why.empty()) return why;
			if (row == 0) return std::string("No rows found");
			return out;
		});
}

SqlViewModel::~SqlViewModel()
{
	if (db_ != nullptr) sqlite3_close(db_);
}

} // namespace chimera::vm
