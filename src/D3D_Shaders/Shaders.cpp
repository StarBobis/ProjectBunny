// Shaders.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include <direct.h>
#include <memory>
#include <limits>
#include <stdexcept>

using namespace std;

FILE *LogFile = NULL;
bool gLogDebug = false;

static vector<string> enumerateFiles(string pathName, string filter = "") {
	vector<string> files;
	WIN32_FIND_DATAA FindFileData;
	HANDLE hFind;
	string sName = pathName;
	sName.append(filter);
	hFind = FindFirstFileA(sName.c_str(), &FindFileData);
	if (hFind != INVALID_HANDLE_VALUE)	{
		string fName = pathName;
		fName.append(FindFileData.cFileName);
		files.push_back(fName);
		while (FindNextFileA(hFind, &FindFileData)) {
			fName = pathName;
			fName.append(FindFileData.cFileName);
			files.push_back(fName);
		}
		FindClose(hFind);
	}
	return files;
}

static vector<byte_t> readFile(string fileName) {
	vector<byte_t> buffer;
	FILE* input = nullptr;
	if (fopen_s(&input, fileName.c_str(), "rb") || !input)
		throw runtime_error("Cannot open shader input: " + fileName);
	unique_ptr<FILE, decltype(&fclose)> file(input, &fclose);
	if (_fseeki64(file.get(), 0, SEEK_END))
		throw runtime_error("Cannot seek shader input: " + fileName);
	const __int64 fileSize = _ftelli64(file.get());
	// DXBC lengths and this tool's assembled code sizes are 32-bit.
	if (fileSize < 0 || static_cast<unsigned long long>(fileSize) > (numeric_limits<uint32_t>::max)() ||
		static_cast<unsigned long long>(fileSize) > buffer.max_size())
		throw runtime_error("Invalid or unsupported shader input length: " + fileName);
	if (_fseeki64(file.get(), 0, SEEK_SET))
		throw runtime_error("Cannot rewind shader input: " + fileName);
	buffer.resize(static_cast<size_t>(fileSize));
	if ((!buffer.empty() && fread(buffer.data(), 1, buffer.size(), file.get()) != buffer.size()) || ferror(file.get()))
		throw runtime_error("Cannot read complete shader input: " + fileName);
	if (fclose(file.release()))
		throw runtime_error("Cannot close shader input: " + fileName);
	return buffer;
}

static void writeFile(const string& fileName, const vector<byte_t>& buffer)
{
	FILE* output = nullptr;
	if (fopen_s(&output, fileName.c_str(), "wb") || !output)
		throw runtime_error("Cannot open shader output: " + fileName);
	unique_ptr<FILE, decltype(&fclose)> file(output, &fclose);
	if ((!buffer.empty() && fwrite(buffer.data(), 1, buffer.size(), file.get()) != buffer.size()) || ferror(file.get()))
		throw runtime_error("Cannot write complete shader output: " + fileName);
	if (fclose(file.release()))
		throw runtime_error("Cannot close shader output: " + fileName);
}

static int runShaderTool()
{
	int shaderNo = 1;
	vector<string> gameNames;
	string pathName;
	vector<string> files;
	FILE* f;
	char cwd[MAX_PATH];
	char gamebuffer[10000];

	if (!_getcwd(cwd, MAX_PATH))
		return 1;
	vector<string> lines;
	fopen_s(&f, "gamelist.txt", "rb");
	if (f) {
		size_t fr = ::fread(gamebuffer, 1, 10000, f);
		fclose(f);
		lines = stringToLines(gamebuffer, fr);
	}

	if (lines.size() > 0) {
		for (auto i = lines.begin(); i != lines.end(); i++) {
			gameNames.push_back(*i);
		}
	} else {
		gameNames.push_back(cwd);
	}
	for (DWORD i = 0; i < gameNames.size(); i++) {
		string gameName = gameNames[i];
		cout << gameName << ":" << endl;

		int progress = 0;
		pathName = gameName;
		pathName.append("\\ShaderCache\\");
		files = enumerateFiles(pathName, "????????????????-??.bin");
		if (files.size() > 0) {
			cout << "bin->asm: ";
			for (DWORD i = 0; i < files.size(); i++) {
				string fileName = files[i];

				vector<byte_t> ASM;
				disassembler(&readFile(fileName), &ASM, NULL);

				fileName.erase(fileName.size() - 3, 3);
				fileName.append("txt");
				writeFile(fileName, ASM);
				
				int newProgress = (int)(50.0 * i / files.size());
				if (newProgress > progress) {
					cout << ".";
					progress++;
				}
			}
		}
		cout << endl;

		progress = 0;
		pathName = gameName;
		pathName.append("\\ShaderCache\\");
		files = enumerateFiles(pathName, "????????????????-??.txt");
		if (files.size() > 0) {
			cout << "asm->cbo: ";
			for (DWORD i = 0; i < files.size(); i++) {
				string fileName = files[i];

				auto ASM = readFile(fileName);
				fileName.erase(fileName.size() - 3, 3);
				fileName.append("bin");
				auto BIN = readFile(fileName);
				
				auto CBO = assembler((vector<char>*)&ASM, BIN);

				fileName.erase(fileName.size() - 3, 3);
				fileName.append("cbo");
				writeFile(fileName, CBO);

				int newProgress = (int)(50.0 * i / files.size());
				if (newProgress > progress) {
					cout << ".";
					progress++;
				}
			}
		}
		cout << endl;

		progress = 0;
		pathName = gameNames[i];
		pathName.append("\\Mark\\");
		files = enumerateFiles(pathName, "*.bin");
		if (files.size() > 0) {
			cout << "bin->asm validate: ";
			for (DWORD i = 0; i < files.size(); i++) {
				string fileName = files[i];

				vector<byte_t> ASM;
				disassembler(&readFile(fileName), &ASM, NULL);

				fileName.erase(fileName.size() - 3, 3);
				fileName.append("txt");
				writeFile(fileName, ASM);

				int newProgress = (int)(50.0 * i / files.size());
				if (newProgress > progress) {
					cout << ".";
					progress++;
				}
			}
		}
		cout << endl;

		writeLUT();
	}
	return 0;
}

int _tmain(int argc, _TCHAR* argv[])
{
	try {
		return runShaderTool();
	} catch (const std::exception& error) {
		std::cerr << "Shader tool failed: " << error.what() << std::endl;
		return 1;
	}
}
