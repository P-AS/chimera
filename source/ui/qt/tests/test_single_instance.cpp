/* Single instance mode's hand-over: the files a second start names reach
 * the first whole, in Chimera's payload (each argument's UTF-8 bytes in
 * upper-case hex, joined by '|'), a name with spaces, accents or a '|' of
 * its own among them. The mutex and the pipe are tried on Windows by
 * hand: two starts, the second ending at once and the first opening its
 * file. */
#include "../single_instance.hpp"

#include <cstdio>

namespace {
int checks = 0, failed = 0;

void check(bool ok, const char *what, int line)
{
	checks++;
	if (ok) return;
	failed++;
	std::printf("FAIL %s:%d: %s\n", __FILE__, line, what);
}
#define CHECK(x) check((x), #x, __LINE__)
} // namespace

int main()
{
	using chimera::qt::SingleInstance;
	/* Chimera's: "C:\a" is 433A5C61 */
	CHECK(SingleInstance::payload({QStringLiteral("C:\\a")}) == "433A5C61");
	CHECK(SingleInstance::payload({QStringLiteral("a"), QStringLiteral("b")}) == "61|62");
	const QStringList files{QStringLiteral("C:\\Games\\Caf\u00e9 | 2.chimeraProject"), QStringLiteral("D:\\x y\\\u65e5\u672c.sfc")};
	CHECK(SingleInstance::arguments(SingleInstance::payload(files)) == files);
	CHECK(SingleInstance::arguments(SingleInstance::payload({})) == QStringList{QString()});
	std::printf("%s: %d checks, %d failed\n", failed == 0 ? "PASS" : "FAIL", checks, failed);
	return failed == 0 ? 0 : 1;
}
