/*
obs-remote-deck
Copyright (C) 2026 Remote Deck

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

namespace {

std::wstring argValue(const std::vector<std::wstring> &args, const std::wstring &prefix)
{
	for (const auto &arg : args) {
		if (arg.rfind(prefix, 0) == 0)
			return arg.substr(prefix.size());
	}
	return {};
}

bool waitForProcess(DWORD pid)
{
	if (pid == 0)
		return true;

	HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (!process)
		return true;

	const DWORD waitResult = WaitForSingleObject(process, INFINITE);
	CloseHandle(process);
	return waitResult == WAIT_OBJECT_0;
}

bool runInstaller(const std::wstring &installerPath)
{
	if (installerPath.empty())
		return false;

	std::wstring commandLine = L"\"" + installerPath +
				   L"\" /VERYSILENT /NORESTART /SUPPRESSMSGBOXES /CURRENTUSER";

	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo{};

	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(L'\0');

	if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
			    &startupInfo, &processInfo))
		return false;

	WaitForSingleObject(processInfo.hProcess, INFINITE);
	DWORD exitCode = 1;
	GetExitCodeProcess(processInfo.hProcess, &exitCode);
	CloseHandle(processInfo.hProcess);
	CloseHandle(processInfo.hThread);
	return exitCode == 0;
}

bool relaunchObs(const std::wstring &obsPath)
{
	if (obsPath.empty())
		return true;

	SHELLEXECUTEINFOW info{};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
	info.lpFile = obsPath.c_str();
	info.nShow = SW_SHOWDEFAULT;

	if (!ShellExecuteExW(&info))
		return false;

	if (info.hProcess) {
		CloseHandle(info.hProcess);
	}
	return true;
}

}

int wmain(int argc, wchar_t *argv[])
{
	std::vector<std::wstring> args;
	args.reserve(static_cast<size_t>(argc));
	for (int i = 0; i < argc; ++i)
		args.emplace_back(argv[i]);

	const DWORD waitPid = static_cast<DWORD>(wcstoul(argValue(args, L"--wait-pid=").c_str(), nullptr, 10));
	const std::wstring installer = argValue(args, L"--installer=");
	const std::wstring relaunch = argValue(args, L"--relaunch=");

	if (!waitForProcess(waitPid))
		return 1;
	if (!runInstaller(installer))
		return 2;
	if (!relaunchObs(relaunch))
		return 3;
	return 0;
}

#endif
