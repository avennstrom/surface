#include "error.hpp"

#include <Windows.h>

#if !CONFIG_RETAIL
#pragma warning(push)
#pragma warning(disable : 4091)
#include <DbgHelp.h>
#pragma warning(pop)
#endif

#include <varargs.h>
#include <cstdio>

#include <string>

std::string GetLastErrorAsString()
{
	//Get the error message, if any.
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0)
		return std::string(); //No error message has been recorded

	LPSTR messageBuffer = nullptr;
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	std::string message(messageBuffer, size);

	//Free the buffer.
	LocalFree(messageBuffer);

	return message;
}

void fatalError(const char* format, ...)
{
	if (IsDebuggerPresent())
	{
		DebugBreak();
	}

	char errorMessageBuffer[4 * 1024];

	va_list args;
	va_start(args, format);
	vsprintf(errorMessageBuffer, format, args);
	va_end(args);

	strcat_s(errorMessageBuffer, "\n\n");

	const size_t maxCallers = 62;
	void* callers[maxCallers];
	const UINT callerCount = CaptureStackBackTrace(1, maxCallers, callers, nullptr);

#if !CONFIG_RETAIL
	const HANDLE process = GetCurrentProcess();
	void* symbolInfoMemory = malloc(sizeof(SYMBOL_INFO) + 512);
	SYMBOL_INFO* symbolInfo = static_cast<SYMBOL_INFO*>(symbolInfoMemory);
	symbolInfo->MaxNameLen = 511;
	symbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
#endif

	strcat_s(errorMessageBuffer, "Callstack:\n");
	for (UINT i = 0; i < callerCount; i++)
	{
		char callerName[512];
		strcpy(callerName, "");

#if !CONFIG_RETAIL
		if (SymFromAddr(process, (DWORD64)(callers[i]), 0, symbolInfo) == TRUE)
		{
			strcpy_s(callerName, symbolInfo->Name);
		}
#endif

		char callerBuffer[256];
		sprintf(callerBuffer, "%p - %s\n", callers[i], callerName);
		strcat_s(errorMessageBuffer, callerBuffer);
	}

#if !CONFIG_RETAIL
	free(symbolInfoMemory);
#endif

	MessageBoxA(nullptr, errorMessageBuffer, "Error!", MB_ICONERROR);
	ExitProcess(1);
}
