/* file_io.hpp - the engine's minimal file access.
 *
 * Whole-file, UTF-8 paths everywhere - on Windows that means
 * converting to wide characters, since the ANSI fopen would mangle non-ASCII
 * paths the C# side (which is UTF-16 native) has no trouble with.
 */

#ifndef CHIMERA_FILE_IO_HPP
#define CHIMERA_FILE_IO_HPP

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace chimera {

/* false when the file cannot be opened or read; out is only valid on true. */
bool readFile(const char *utf8Path, std::vector<uint8_t> &out);

/* A file read a piece at a time, for the ones too big to want whole: a PS2
 * disc image is over four gigabytes, and nothing the engine does with one
 * needs it in memory. Same UTF-8 path handling as the whole-file calls. */
class FileReader
{
public:
	FileReader() = default;
	~FileReader();
	FileReader(const FileReader &) = delete;
	FileReader &operator=(const FileReader &) = delete;

	bool open(const char *utf8Path);
	/* bytes read, 0 at end of file (or on error - ask ok()) */
	uint64_t read(uint8_t *dst, uint64_t max);
	bool ok() const;
	void close();

private:
	void *_f = nullptr;   /* FILE*, kept opaque so the header stays clean */
};


/* The other direction: a file written a piece at a time, beside its final name
 * and moved into place by commit(), so a reader never finds half of one and a
 * write that fails leaves whatever was there before. */
class FileWriter
{
public:
	FileWriter() = default;
	~FileWriter();
	FileWriter(const FileWriter &) = delete;
	FileWriter &operator=(const FileWriter &) = delete;

	/* a link is followed: the file it names is the one replaced */
	bool open(const char *utf8Path);
	bool write(const void *src, uint64_t len);
	/* closes and moves into place; false (and nothing left behind) on any failure.
	 * toDisk waits until the bytes are on the disk before the move, so a power
	 * cut right after a save cannot leave the new name over an empty file. */
	bool commit(bool toDisk = false);

private:
	void *_f = nullptr;
	std::string _path, _tmp;
	bool _failed = false;
};

bool fileExists(const char *utf8Path);

/* A file's size and last-modified time, for deciding whether a remembered
 * answer about it still applies. false when the file cannot be stat'd. */
bool fileStamp(const char *utf8Path, uint64_t *sizeOut, int64_t *mtimeOut);

/* Everything that says whether a file is still the one it was: its size, the
 * time its bytes last changed at the resolution the file system keeps, the
 * time anything about it last changed (which a copy that keeps the source's
 * date, or a tool that sets the date back, cannot set back), and which file on
 * which volume it is (a file replaced by another is another file). */
struct FileIdentity
{
	uint64_t size = 0;
	int64_t modified = 0, changed = 0;
	uint64_t volume = 0, index = 0;
	bool operator==(const FileIdentity &o) const
	{
		return size == o.size && modified == o.modified && changed == o.changed && volume == o.volume && index == o.index;
	}
	bool operator<(const FileIdentity &o) const
	{
		if (size != o.size) return size < o.size;
		if (modified != o.modified) return modified < o.modified;
		if (changed != o.changed) return changed < o.changed;
		if (volume != o.volume) return volume < o.volume;
		return index < o.index;
	}
};
bool fileIdentity(const char *utf8Path, FileIdentity &out);

/* Whole-file write (projects, multifile descriptors); false on any failure.
 * Written beside the file and moved over it, so a save that fails part way
 * (a full disk, the process dying, the power going) leaves the last good copy. */
bool writeFile(const char *utf8Path, const uint8_t *data, uint64_t len);

bool isDirectory(const char *utf8Path);

/* The C file calls, for UTF-8 paths. On Windows the plain fopen, remove and
 * rename read a path in the ANSI code page, so a Windows user whose name is
 * not plain ASCII could not open anything in their own profile folder; these
 * widen the path first. */
FILE *openFile(const char *utf8Path, const char *mode);
bool removeFile(const char *utf8Path);
/* Moves from over to, replacing it in one step. On Windows a virus scanner or
 * the indexer may hold the old file for a moment; that is waited out (up to
 * about a second). */
bool replaceFile(const char *fromUtf8, const char *toUtf8);
/* fseek with a 64-bit offset: Windows' long is 32 bits, so the plain call
 * cannot reach past 2 GiB there. */
bool seekFile(FILE *f, int64_t offset, int whence);

} // namespace chimera

#endif
