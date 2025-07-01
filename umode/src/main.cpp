#include <stdio.h>
#include <windows.h>
#include <TlHelp32.h>

static DWORD getprocessID(const wchar_t* processName) {
	DWORD processID = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (snapshot == INVALID_HANDLE_VALUE) {
		return processID;
	}

	PROCESSENTRY32W processEntry = {};

	processEntry.dwSize = sizeof(decltype(processEntry));

	if (Process32FirstW(snapshot, &processEntry)) {

		// check if first handle is the one we want
		if (_wcsicmp(processEntry.szExeFile, processName) == 0) {
			processID = processEntry.th32ProcessID;
		}
		else {
			while (Process32NextW(snapshot, &processEntry) == TRUE) {
				if (_wcsicmp(processEntry.szExeFile, processName) == 0) {
					processID = processEntry.th32ProcessID;
					break;
				}
			}
		}
	}

	CloseHandle(snapshot);
	return processID;
}

static uintptr_t getModuleBaseAddress(DWORD processID, const wchar_t* moduleName) {
	uintptr_t baseAddress = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);

	if (snapshot == INVALID_HANDLE_VALUE) {
		return baseAddress;
	}

	MODULEENTRY32W moduleEntry = {};
	moduleEntry.dwSize = sizeof(decltype(moduleEntry));

	if (Module32FirstW(snapshot, &moduleEntry) == TRUE) {
		if (wcsstr(moduleName, moduleEntry.szModule) != nullptr) {
			baseAddress = reinterpret_cast<uintptr_t>(moduleEntry.modBaseAddr);
		}
		else {
			while (Module32NextW(snapshot, &moduleEntry) == TRUE) {
				if (wcsstr(moduleName, moduleEntry.szModule) != nullptr) {
					baseAddress = reinterpret_cast<uintptr_t>(moduleEntry.modBaseAddr);
					break;
				}
			}
		}
	}


	CloseHandle(snapshot);
	return baseAddress;
}

namespace driver {
	namespace ctlCodes {
		// setup driver
		constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
		// read/write memory
		constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
		constexpr ULONG write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	}

	struct Request {
		HANDLE processID;

		PVOID targetAddress;
		PVOID buffer;

		SIZE_T bufferSize;
		SIZE_T returnSize;
	};

	bool attachtoProcess(HANDLE driverHandle, const DWORD pid)
	{
		Request r;
		r.processID = reinterpret_cast<HANDLE>(pid);

		return DeviceIoControl(
			driverHandle,
			ctlCodes::attach,
			&r,
			sizeof(r),
			&r,
			sizeof(r),
			nullptr,
			nullptr
		);

	}

	template <class T>
	T readMemory(HANDLE driverHandle, const uintptr_t address)
	{
		T temp = {};

		Request r;

		r.targetAddress = reinterpret_cast<PVOID>(address);
		r.buffer = &temp;
		r.bufferSize = sizeof(T);

		DeviceIoControl(
			driverHandle,
			ctlCodes::read,
			&r,
			sizeof(r),
			&r,
			sizeof(r),
			nullptr,
			nullptr
		);

		return temp;
	}

	template <class T>
	void writeMemory(HANDLE driverHandle, const uintptr_t address, const T& value) {
		Request r;
		r.targetAddress = reinterpret_cast<PVOID>(address);
		r.buffer = (PVOID)&value;
		r.bufferSize = sizeof(T);

		DeviceIoControl(
			driverHandle,
			ctlCodes::write,
			&r,
			sizeof(r),
			&r,
			sizeof(r),
			nullptr,
			nullptr
		);
	}
}

int main() {

	const DWORD pid = getprocessID(L"notepad.exe");
	if (pid == 0) {
		printf("Process not found!\n");
		
		return 1;
	}

	printf("Process ID: %lu\n", pid);

	const HANDLE driverHandle = CreateFileW(L"\\\\.\\SMDriver", GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (driverHandle == INVALID_HANDLE_VALUE) {
		printf("Failed to open driver handle!\n");

		return 1;
	}

	if (driver::attachtoProcess(driverHandle, pid) == TRUE)
		printf("Attached to process successfully!\n");

	CloseHandle(driverHandle);
	
	return 0;
}