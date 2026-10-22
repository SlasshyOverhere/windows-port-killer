// ports.h - native Windows connection/process enumeration.
#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

enum : int { PROTO_TCP = 6, PROTO_UDP = 17 };

struct PortConn {
    DWORD pid = 0;
    int proto = PROTO_TCP;
    bool v6 = false;
    DWORD state = 0;            // MIB_TCP_STATE_*; 0 for UDP
    USHORT localPort = 0;
    USHORT remotePort = 0;
    std::wstring localAddr;     // "0.0.0.0:80" / "[::]:80"
    std::wstring remoteAddr;    // "0.0.0.0:0"  / "[::]:0"
    std::wstring processName;   // filled by the UI from ProcMap
};

struct ProcInfo {
    std::wstring name;
    std::wstring path;
    DWORD pid = 0;
    DWORD ppid = 0;
};

using ProcMap = std::unordered_map<DWORD, ProcInfo>;

std::vector<PortConn> EnumConnections();
ProcMap EnumProcMap();
std::wstring TcpStateName(DWORD state);
std::wstring ProtoName(int proto);
bool IsAdmin();
bool EnableDebugPrivilege();
bool KillPid(DWORD pid, DWORD* err);
std::vector<DWORD> ChildPids(DWORD pid);
ULONGLONG WorkingSetOf(DWORD pid);
int TcpStateRank(DWORD state);  // sort weight: listening first
