// G-WinMem.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>
#include <iostream>
#include <stdlib.h>
#include <string>
#include <iterator>
#include <algorithm>
#include <iphlpapi.h>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "Ws2_32.lib")


class MemoryChunk
{
public:
	LPVOID start;
	SIZE_T size;
};

int GetProcessId()
{
	DWORD size;

	GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);

	auto tcp_pid = new MIB_TCPTABLE_OWNER_PID[size];

	if(GetExtendedTcpTable(tcp_pid, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, NULL) != NO_ERROR)
	{
		std::cout << "Failed to get TCP Table\n";
		return -1;
	}

	for (DWORD i = 0; i < tcp_pid->dwNumEntries; i++)
	{
		auto *owner_pid = &tcp_pid->table[i];

		if (ntohs(owner_pid->dwRemotePort) == 30000 ||
			ntohs(owner_pid->dwRemotePort) == 38101)
			return owner_pid->dwOwningPid;
	}
	return -1;
}

void GetRC4Possibilities(int pid)
{
	std::vector<MemoryChunk *> results;
	const auto hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_OPERATION, false, pid);

	MEMORY_BASIC_INFORMATION mbi;
	SYSTEM_INFO sys_info;

	GetSystemInfo(&sys_info);

	auto addr = reinterpret_cast<uintptr_t>(sys_info.lpMinimumApplicationAddress);
	const auto end = reinterpret_cast<uintptr_t>(sys_info.lpMaximumApplicationAddress);

	while (addr < end)
	{
		const auto offset = 4;

		if (!VirtualQueryEx(hProcess, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
			break;

		if (mbi.State == MEM_COMMIT && ((mbi.Protect & PAGE_GUARD) == 0) && ((mbi.Protect & PAGE_NOACCESS) == 0)) {
			const auto dump = new unsigned char[mbi.RegionSize + 1];
			memset(dump, 0, mbi.RegionSize + 1);
			if(!ReadProcessMemory(hProcess, mbi.BaseAddress, dump, mbi.RegionSize, nullptr))
			{
				std::cout << "Failed to read memory for " << mbi.BaseAddress;
				break;
			}

			auto maskCount = 0;
			int nToMap[256] = { 0 };
			int removeMap[256] = { 0 };

			for (auto i = 0; i < 256; i++) {
				nToMap[i] = -1;
				removeMap[i] = -1;
			}

			auto matchStart = -1;
			auto matchEnd = -1;

			for (auto i = 0; i < mbi.RegionSize; i+= offset)
			{
				const auto b = (static_cast<int>(dump[i]) + 128) % 256;
				const auto indInMap = (i / 4) % 256;

				const auto deletedNumber = removeMap[indInMap];

				if (deletedNumber != -1)
				{
					nToMap[deletedNumber] = -1;
					maskCount--;
					removeMap[indInMap] = -1;
				}

				if (nToMap[b] == -1)
				{
					maskCount++;
					removeMap[indInMap] = b;
					nToMap[b] = indInMap;
				}
				else
				{
					removeMap[nToMap[b]] = -1;
					removeMap[indInMap] = b;
					nToMap[b] = indInMap;
				}

				if (maskCount == 256)
				{
					if (matchStart == -1)
					{
						matchStart = i - ((256 - 1) * offset);
						matchEnd = i;
					}

					if (matchEnd < i - ((256 - 1) * offset))
					{
						auto mem = new MemoryChunk();
						mem->start = dump + matchStart;
						mem->size = matchEnd - matchStart + 4;
						results.push_back(mem);

						matchStart = i - ((256 - 1) * offset);
					}
					matchEnd = i;
				}
			}
			if (matchStart != -1)
			{
				auto mem = new MemoryChunk();
				mem->start = dump + matchStart;
				mem->size = matchEnd - matchStart + 4;
				results.push_back(mem);
			}
		}
		addr += mbi.RegionSize;
	}

	/* PrintRC4Possibilities */

	const auto offset = 4;
	auto count = 0;
	for (auto mem : results)
	{
		if (mem->size >= 1024 && mem->size <= 1024 + 2 * offset)
		{
			for (auto i = 0; i < (mem->size - ((256 - 1) * offset)); i += offset)
			{
				unsigned char wannabeRC4data[1024] = { 0 };
				unsigned char data[256] = { 0 };
				memcpy(wannabeRC4data, static_cast<unsigned char *>(mem->start) + i, 1024);

				auto isvalid = true;

				for (auto j = 0; j < 1024; j++)
				{
					if (j % 4 != 0 && wannabeRC4data[j] != 0)
					{
						isvalid = false;
						break;
					}
					if (j % 4 == 0)
					{
						data[j / 4] = wannabeRC4data[j];
					}
				}
				if (isvalid)
				{
					for (auto idx = 0; idx < 256; idx++)
						printf("%02X", static_cast<signed char>(data[idx]) & 0xFF);

					std::cout << std::endl;
				}
			}
		}
	}
}


int main()
{
	GetRC4Possibilities(GetProcessId());
    return 0;
}

