#!/bin/sh
# Chimera reaches for nothing over the network (user-decided, 2026-10-07;
# docs/core-manager.md, "Nothing reaches the network"). This holds the line:
# it fails if the frontend's or the engine's sources name a network API.
#
# It reads SOURCE, not binaries, and it is a list of names, so it is a tripwire
# and not a proof: what it catches is the ordinary way such code comes back - a
# class that seemed harmless, pasted in or restored from history. A link that
# is handed to the system's browser is not a request Chimera makes, and is not
# looked for; nor is local IPC (named pipes, memory-mapped files).
#
# Usage: tools/check-no-network.sh [<root>]      exit 0 clean, 1 with the lines
set -eu
root="${1:-$(cd "$(dirname "$0")/.." && pwd)}"

# managed: the namespaces and types a request, a socket or a lookup is made with
managed='System\.Net\b|\bHttpClient\b|\bWebClient\b|\bWebRequest\b|\bHttpWebRequest\b|\bHttpListener\b|\bTcpClient\b|\bTcpListener\b|\bUdpClient\b|\bClientWebSocket\b|\bNetworkStream\b|\bIPAddress\b|\bIPEndPoint\b|\bDns\.|\bSmtpClient\b|\bFtpWebRequest\b'
# native: the headers and calls
native='sys/socket\.h|netinet/|arpa/inet\.h|netdb\.h|winsock|ws2tcpip|\bWSAStartup\b|\bgetaddrinfo\b|\bgethostbyname\b|curl/curl\.h|\bcurl_easy_'

found=0
if grep -rnE --include='*.cs' --include='*.csproj' --include='*.props' --include='*.targets' \
	--exclude-dir=obj --exclude-dir=bin "$managed" "$root/source/gui"; then
	found=1
fi
if grep -rnE --include='*.c' --include='*.cc' --include='*.cpp' --include='*.h' --include='*.hpp' \
	"$native" "$root/source/engine"; then
	found=1
fi
# what is BUILT from extern/ for the bundle is named in meson.build; a network
# library there is the same thing by another road
if grep -nE 'luasocket|libcurl|ws2_32|wsock32' "$root/meson.build"; then
	found=1
fi

if [ "$found" -ne 0 ]; then
	echo "check-no-network: the lines above name a network API. Chimera has none; see docs/core-manager.md." >&2
	exit 1
fi
echo "check-no-network: clean (source/gui, source/engine, meson.build)"
