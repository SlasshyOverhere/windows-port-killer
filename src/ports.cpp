// ports.cpp - fast native enumeration via the IP Helper API (no netstat/tasklist).
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include "ports.h"

#include <iphlpapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "psapi.lib")

namespace {

inline USHORT NetPort(DWORD p) {
    p &= 0xFFFF;
    return (USHORT)(((p & 0xFF) << 8) | ((p >> 8) & 0xFF));
}

std::wstring FormatV4(DWORD ip, USHORT port) {
    wchar_t buf[32];
    _snwprintf_s(buf, _TRUNCATE, L"%u.%u.%u.%u:%u",
                 ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, port);
    return buf;
}

std::wstring FormatV6(const UCHAR* a, USHORT port) {
    unsigned short g[8];
    for (int i = 0; i < 8; ++i) g[i] = (unsigned short)((a[i * 2] << 8) | a[i * 2 + 1]);

    // RFC 5952: compress the longest run of >=2 zero groups.
    int bestStart = -1, bestLen = 0;
    for (int i = 0; i < 8;) {
        if (g[i] != 0) { ++i; continue; }
        int j = i;
        while (j < 8 && g[j] == 0) ++j;
        if (j - i > bestLen) { bestLen = j - i; bestStart = i; }
        i = j;
    }
    if (bestLen < 2) bestStart = -1;

    std::wstring out = L"[";
    wchar_t tmp[8];
    for (int i = 0; i < 8; ++i) {
        if (i == bestStart) {
            out += L"::";
            i += bestLen - 1;
            continue;
        }
        if (!out.empty() && out.back() != L'[' && out.back() != L':') out += L':';
        _snwprintf_s(tmp, _TRUNCATE, L"%x", g[i]);
        out += tmp;
    }
    out += L"]:";
    _snwprintf_s(tmp, _TRUNCATE, L"%u", port);
    out += tmp;
    return out;
}

template <bool Udp, typename Cls>
DWORD QueryTable(ULONG af, Cls cls, void* p, DWORD* size) {
    if constexpr (Udp)
        return GetExtendedUdpTable(p, size, FALSE, af, cls, 0);
    else
        return GetExtendedTcpTable(p, size, FALSE, af, (TCP_TABLE_CLASS)cls, 0);
}

template <bool Udp, typename Cls>
bool FetchTable(ULONG af, Cls cls, std::vector<BYTE>& buf) {
    DWORD size = 0;
    DWORD rc = QueryTable<Udp>(af, cls, nullptr, &size);
    if (rc != ERROR_INSUFFICIENT_BUFFER) return false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        buf.resize(size + 4096);
        rc = QueryTable<Udp>(af, cls, buf.data(), &size);
        if (rc == NO_ERROR) return true;
        if (rc != ERROR_INSUFFICIENT_BUFFER) return false;
    }
    return false;
}

template <typename Row, bool V6>
void AppendTcp(std::vector<PortConn>& out, const std::vector<BYTE>& buf) {
    DWORD count = *reinterpret_cast<const DWORD*>(buf.data());
    const auto* rows = reinterpret_cast<const Row*>(buf.data() + sizeof(DWORD));
    for (DWORD i = 0; i < count; ++i) {
        PortConn c;
        c.proto = PROTO_TCP;
        c.v6 = V6;
        c.pid = rows[i].dwOwningPid;
        c.state = rows[i].dwState;
        c.localPort = NetPort(rows[i].dwLocalPort);
        c.remotePort = NetPort(rows[i].dwRemotePort);
        if constexpr (V6) {
            c.localAddr = FormatV6(rows[i].ucLocalAddr, c.localPort);
            c.remoteAddr = FormatV6(rows[i].ucRemoteAddr, c.remotePort);
        } else {
            c.localAddr = FormatV4(rows[i].dwLocalAddr, c.localPort);
            c.remoteAddr = FormatV4(rows[i].dwRemoteAddr, c.remotePort);
        }
        out.push_back(std::move(c));
    }
}

template <typename Row, bool V6>
void AppendUdp(std::vector<PortConn>& out, const std::vector<BYTE>& buf) {
    DWORD count = *reinterpret_cast<const DWORD*>(buf.data());
    const auto* rows = reinterpret_cast<const Row*>(buf.data() + sizeof(DWORD));
    for (DWORD i = 0; i < count; ++i) {
        PortConn c;
        c.proto = PROTO_UDP;
        c.v6 = V6;
        c.pid = rows[i].dwOwningPid;
        c.localPort = NetPort(rows[i].dwLocalPort);
        if constexpr (V6) {
            c.localAddr = FormatV6(rows[i].ucLocalAddr, c.localPort);
        } else {
            c.localAddr = FormatV4(rows[i].dwLocalAddr, c.localPort);
        }
        c.remoteAddr = V6 ? L"[::]:0" : L"0.0.0.0:0";
        out.push_back(std::move(c));
    }
}

} // namespace

std::vector<PortConn> EnumConnections() {
    std::vector<PortConn> out;
    out.reserve(512);

    std::vector<BYTE> buf;
    if (FetchTable<false>(AF_INET, TCP_TABLE_OWNER_PID_ALL, buf))
        AppendTcp<MIB_TCPROW_OWNER_PID, false>(out, buf);
    if (FetchTable<false>(AF_INET6, TCP_TABLE_OWNER_PID_ALL, buf))
        AppendTcp<MIB_TCP6ROW_OWNER_PID, true>(out, buf);
    if (FetchTable<true>(AF_INET, UDP_TABLE_OWNER_PID, buf))
        AppendUdp<MIB_UDPROW_OWNER_PID, false>(out, buf);
    if (FetchTable<true>(AF_INET6, UDP_TABLE_OWNER_PID, buf))
        AppendUdp<MIB_UDP6ROW_OWNER_PID, true>(out, buf);

    return out;
}

ProcMap EnumProcMap() {
    ProcMap map;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return map;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcInfo pi;
            pi.pid = pe.th32ProcessID;
            pi.ppid = pe.th32ParentProcessID;
            pi.name = pe.szExeFile;
            map.emplace(pi.pid, std::move(pi));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    for (auto& kv : map) {
        DWORD pid = kv.first;
        if (pid <= 4) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD len = (DWORD)(sizeof(path) / sizeof(path[0]));
        if (QueryFullProcessImageNameW(h, 0, path, &len)) kv.second.path.assign(path, len);
        CloseHandle(h);
    }
    return map;
}

std::wstring TcpStateName(DWORD state) {
    switch (state) {
        case MIB_TCP_STATE_CLOSED: return L"CLOSED";
        case MIB_TCP_STATE_LISTEN: return L"LISTEN";
        case MIB_TCP_STATE_SYN_SENT: return L"SYN_SENT";
        case MIB_TCP_STATE_SYN_RCVD: return L"SYN_RCVD";
        case MIB_TCP_STATE_ESTAB: return L"ESTABLISHED";
        case MIB_TCP_STATE_FIN_WAIT1: return L"FIN_WAIT1";
        case MIB_TCP_STATE_FIN_WAIT2: return L"FIN_WAIT2";
        case MIB_TCP_STATE_CLOSE_WAIT: return L"CLOSE_WAIT";
        case MIB_TCP_STATE_CLOSING: return L"CLOSING";
        case MIB_TCP_STATE_LAST_ACK: return L"LAST_ACK";
        case MIB_TCP_STATE_TIME_WAIT: return L"TIME_WAIT";
        case MIB_TCP_STATE_DELETE_TCB: return L"DELETE_TCB";
        default: return L"-";
    }
}

std::wstring ProtoName(int proto) { return proto == PROTO_UDP ? L"UDP" : L"TCP"; }

bool IsAdmin() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    BOOL ok = FALSE;
    PSID admins = nullptr;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &admins)) {
        CheckTokenMembership(nullptr, admins, &ok);
        FreeSid(admins);
    }
    cached = ok ? 1 : 0;
    return ok != FALSE;
}

bool EnableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(token);
    return ok && GetLastError() == ERROR_SUCCESS;
}

bool KillPid(DWORD pid, DWORD* err) {
    if (err) *err = 0;
    HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!h) {
        if (err) *err = GetLastError();
        return false;
    }
    BOOL ok = TerminateProcess(h, 1);
    DWORD e = ok ? 0 : GetLastError();
    if (ok) WaitForSingleObject(h, 1500);
    CloseHandle(h);
    if (err) *err = e;
    return ok != FALSE;
}

std::vector<DWORD> ChildPids(DWORD pid) {
    struct Ent { DWORD pid, ppid; };
    std::vector<Ent> procs;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                procs.push_back({pe.th32ProcessID, pe.th32ParentProcessID});
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    std::vector<DWORD> out;
    std::vector<DWORD> frontier{pid};
    while (!frontier.empty()) {
        DWORD cur = frontier.back();
        frontier.pop_back();
        for (const auto& p : procs) {
            if (p.ppid != cur || p.pid == 0 || p.pid == 4) continue;
            bool seen = false;
            for (DWORD d : out)
                if (d == p.pid) { seen = true; break; }
            if (seen || p.pid == pid) continue;
            out.push_back(p.pid);
            frontier.push_back(p.pid);
        }
    }
    return out;
}

ULONGLONG WorkingSetOf(DWORD pid) {
    if (pid <= 4) return 0;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    ULONGLONG ws = 0;
    if (K32GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        ws = pmc.WorkingSetSize;
    CloseHandle(h);
    return ws;
}

int TcpStateRank(DWORD state) {
    if (state == MIB_TCP_STATE_LISTEN) return 0;
    if (state == MIB_TCP_STATE_ESTAB) return 1;
    if (state == 0) return 2; // UDP
    if (state == MIB_TCP_STATE_TIME_WAIT) return 4;
    return 3;
}
