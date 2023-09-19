#include <Windows.h>
#include <tchar.h>
#include <stdio.h>
#include <cryptopp/filters.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/gzip.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>
#include <cryptopp/files.h>


#pragma warning(disable: 6011)

#define SECTION_NAME "UPX"
#define AES_BLOCK_SIZE 16
#define PE_FILE_ALIGNMENT 0x200 // NtHeaders->OptionalHeader.FileAlignment

/*
* align������2�Ĵη�������0x1000����ĩβ��0����
*/
#define ALIGN(x, align) ((x+align-1)&~(align-1))


BYTE* ReadPeFile(TCHAR* PePath, DWORD* PeSize);
BYTE* EncryptData(BYTE* Data, INT Length, INT* OutputLength);
BOOL PackPE(TCHAR* StubPath, TCHAR* PackedPath, PBYTE SectionData, DWORD SectionSize);
BOOL AppendPeSection(PBYTE PeAddress, CHAR* SectionName, PVOID SectionData, DWORD SectionSize);


int _tmain(int argc, TCHAR* argv[])
{
	if (argc != 3) {
		_tprintf(_T("[x] Usage: PEPacker.exe C:\\Path\\To\\PeStub.exe C:\\Path\\To\\File.exe\n"));
		return 0;
	}

	TCHAR* StubPath = argv[1];
	TCHAR* PePath = argv[2];
	TCHAR* PackedPath = (TCHAR*)_T("Packed.exe");

	//
	// ��ȡ��Ҫ�ӿǵ�PE�ļ�
	//

	DWORD PeSize;
	BYTE* PeAddress = ReadPeFile(PePath, &PeSize);

	if (PeAddress == NULL) {
		return -1;
	}

	_tprintf(_T("[+] Raw File Size: %#x\n"), PeSize);

	//
	// �����ļ�
	//

	INT EncryptedSize = 0;
	BYTE* EncryptedData = EncryptData(PeAddress, PeSize, &EncryptedSize);

	free(PeAddress);

	if (EncryptedData == NULL) {
		return -1;
	}

	_tprintf(_T("[+] Encrypted Data Size: %#x\n"), EncryptedSize);

	_tprintf(_T("[+] Compression Ratio: %.2f\n"), (FLOAT)EncryptedSize / PeSize);

	//
	// ��ʼ�ӿ�
	//

	PBYTE SectionData = EncryptedData;
	DWORD SectionSize = (DWORD)EncryptedSize;

	if (PackPE(StubPath, PackedPath, SectionData, SectionSize) == FALSE) {
		_tprintf(_T("[x] Failed to Packet PE File.\n"));
		free(EncryptedData);
		return -1;
	}

	_tprintf(_T("[+] Success.\n"));

	free(EncryptedData);

	return 0;
}


BYTE* ReadPeFile(TCHAR* PePath, DWORD* PeSize)
{
	HANDLE hFile = CreateFile(PePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		_tprintf(_T("[x] CreateFile Failed, Path: %s. Error: %#x\n"), PePath, GetLastError());
		return NULL;
	}

	*PeSize = GetFileSize(hFile, NULL);
	BYTE* PeAddress = (BYTE*)malloc(*PeSize);
	if (PeAddress == NULL) {
		_tprintf(_T("[x] Malloc Failed, Size: %#x. Error: %#x\n"), *PeSize, GetLastError());
		CloseHandle(hFile);
		return NULL;
	}

	DWORD nBytesRead = 0;
	if (ReadFile(hFile, PeAddress, *PeSize, &nBytesRead, NULL) == FALSE) {
		_tprintf(_T("[x] ReadFile Failed, Path: %s. Error: %#x\n"), PePath, GetLastError());
		CloseHandle(hFile);
		return NULL;
	}

	CloseHandle(hFile);

	return PeAddress;
}


BYTE* EncryptData(BYTE* Data, INT Length, INT* OutputLength)
{
	std::string Key;
	std::string IV = "0123456789ABCDEF";

	try {
		CryptoPP::FileSource fs("LICENSE.txt", true,
			new CryptoPP::HexDecoder(
				new CryptoPP::StringSink(Key)));
	}
	catch (CryptoPP::Exception& e) {
		_tprintf(_T("[x] Read Key Error: %hs\n"), e.what());
		return NULL;
	}

	if (Key.length() != 16 && Key.length() != 24 && Key.length() != 32) {
		_tprintf(_T("[x] Wrong Key Length\n"));
		return NULL;
	}

	std::string Input((CHAR*)Data, Length);
	CryptoPP::StringSource Source(Input, false);

	std::string Output;
	CryptoPP::StringSink Sink(Output);

	try {
		CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption Encryption;
		Encryption.SetKeyWithIV((byte*)Key.c_str(), Key.length(), (byte*)IV.c_str());
		CryptoPP::StreamTransformationFilter Encryptor(Encryption);

		CryptoPP::Gzip Gzip(nullptr, CryptoPP::Gzip::MAX_DEFLATE_LEVEL);
		CryptoPP::Base64Encoder Base64Encoder(nullptr, false);

		Source.Attach(new CryptoPP::Redirector(Gzip));
		Gzip.Attach(new CryptoPP::Redirector(Encryptor));
		Encryptor.Attach(new CryptoPP::Redirector(Base64Encoder));
		Base64Encoder.Attach(new CryptoPP::Redirector(Sink));

		Source.PumpAll();
	}
	catch (CryptoPP::Exception& e) {
		_tprintf(_T("[x] CryptoPP Encrypt Error: %hs\n"), e.what());
		return NULL;
	}

	INT OL = (INT)Output.length();
	BYTE* EncryptedData = (BYTE*)malloc(OL);
	if (EncryptedData == NULL) {
		_tprintf(_T("[x] Malloc Failed, Size: %#x. Error: %#x\n"), OL, GetLastError());
		return NULL;
	}
	memcpy(EncryptedData, Output.c_str(), OL);

	*OutputLength = OL;

	return EncryptedData;
}


BOOL PackPE(TCHAR* StubPath, TCHAR* PackedPath, PBYTE SectionData, DWORD SectionSize)
{
	// ��Stub�ļ�
	HANDLE hStubFile = CreateFile(StubPath,
		GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hStubFile == INVALID_HANDLE_VALUE) {
		_tprintf(_T("[x] CreateFile Failed, Path: %s. Error: %#x\n"), StubPath, GetLastError());
		return FALSE;
	}

	DWORD dwStubFileSize = GetFileSize(hStubFile, NULL);

	// �̶���Ϊ�Ŀ�Section
	PBYTE SectionData_1st = SectionData;
	DWORD SectionSize_1st = (SectionSize / 4) + (SectionSize % 4);
	PBYTE SectionData_2nd = SectionData_1st + SectionSize_1st;
	DWORD SectionSize_2nd = SectionSize / 4;
	PBYTE SectionData_3rd = SectionData_2nd + SectionSize_2nd;
	DWORD SectionSize_3rd = SectionSize / 4;
	PBYTE SectionData_4th = SectionData_3rd + SectionSize_3rd;
	DWORD SectionSize_4th = SectionSize / 4;

	// �����ļ��´�С
	DWORD dwPackedFileSize = dwStubFileSize;
	dwPackedFileSize += ALIGN(SectionSize_1st, PE_FILE_ALIGNMENT);
	dwPackedFileSize += ALIGN(SectionSize_2nd, PE_FILE_ALIGNMENT);
	dwPackedFileSize += ALIGN(SectionSize_3rd, PE_FILE_ALIGNMENT);
	dwPackedFileSize += ALIGN(SectionSize_4th, PE_FILE_ALIGNMENT);

	// ����Packed�ļ�
	HANDLE hPackedFile = CreateFile(PackedPath,
		GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hPackedFile == INVALID_HANDLE_VALUE) {
		_tprintf(_T("[x] CreateFile Failed, Path: %s. Error: %#x\n"), StubPath, GetLastError());
		CloseHandle(hStubFile);
		return FALSE;
	}

	HANDLE hFileMapping = CreateFileMapping(hPackedFile, NULL, PAGE_READWRITE, 0, dwPackedFileSize, NULL);
	if (hFileMapping == NULL) {
		_tprintf(_T("[x] CreateFileMapping Failed. Error: %#x\n"), GetLastError());
		CloseHandle(hPackedFile);
		CloseHandle(hStubFile);
		return FALSE;
	}

	PBYTE PeAddress = (PBYTE)MapViewOfFile(hFileMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, dwPackedFileSize);
	if (PeAddress == NULL) {
		_tprintf(_T("[x] MapViewOfFile Failed. Error: %#x\n"), GetLastError());
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		CloseHandle(hStubFile);
		return FALSE;
	}

	// ����Stub�ļ����ݵ�Packed�ļ���
	DWORD nBytesRead = 0;
	if (ReadFile(hStubFile, PeAddress, dwStubFileSize, &nBytesRead, NULL) == FALSE) {
		_tprintf(_T("[x] ReadFile Failed, Path: %s. Error: %#x\n"), StubPath, GetLastError());
		UnmapViewOfFile(PeAddress);
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		CloseHandle(hStubFile);
		return NULL;
	}

	CloseHandle(hStubFile);

	// ׷�ӵ�һ��
	if (AppendPeSection(PeAddress, (CHAR*)SECTION_NAME "0", 
		SectionData_1st, SectionSize_1st) == FALSE) {
		UnmapViewOfFile(PeAddress);
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		return FALSE;
	}
	
	// ׷�ӵڶ���
	if (AppendPeSection(PeAddress, (CHAR*)SECTION_NAME "1", 
		SectionData_2nd, SectionSize_2nd) == FALSE) {
		UnmapViewOfFile(PeAddress);
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		return FALSE;
	}
	
	// ׷�ӵ�����
	if (AppendPeSection(PeAddress, (CHAR*)SECTION_NAME "2", 
		SectionData_3rd, SectionSize_3rd) == FALSE) {
		UnmapViewOfFile(PeAddress);
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		return FALSE;
	}

	// ׷�ӵ��Ķ�
	if (AppendPeSection(PeAddress, (CHAR*)SECTION_NAME "3", 
		SectionData_4th, SectionSize_4th) == FALSE) {
		UnmapViewOfFile(PeAddress);
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		return FALSE;
	}

	// �ͷ���Դ
	UnmapViewOfFile(PeAddress);
	CloseHandle(hFileMapping);
	CloseHandle(hPackedFile);

	return TRUE;
}


BOOL AppendPeSection(PBYTE PeAddress, CHAR* SectionName, PVOID SectionData, DWORD SectionSize)
{
	PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)PeAddress;
	PIMAGE_NT_HEADERS NtHeaders = (PIMAGE_NT_HEADERS)((ULONG_PTR)PeAddress + DosHeader->e_lfanew);
	PIMAGE_SECTION_HEADER SectionHeaders = (PIMAGE_SECTION_HEADER)((ULONG_PTR)NtHeaders + sizeof(IMAGE_NT_HEADERS));

	// �жϿ�϶�Ƿ��㹻װ���µĶ�
	SIZE_T AvailableSpace = (ULONG_PTR)PeAddress + SectionHeaders[0].PointerToRawData
		- (ULONG_PTR)&SectionHeaders[NtHeaders->FileHeader.NumberOfSections];
	if (AvailableSpace < sizeof(IMAGE_SECTION_HEADER)) {
		_tprintf(_T("[x] Remaining Space Is Not Enough To Add New Section.\n"));
		return FALSE;
	}

	// ��ȡ����ֵ
	DWORD SectionAlignment = NtHeaders->OptionalHeader.SectionAlignment;
	DWORD FileAlignment = NtHeaders->OptionalHeader.FileAlignment;

	PIMAGE_SECTION_HEADER LastSection = &SectionHeaders[NtHeaders->FileHeader.NumberOfSections - 1];
	PIMAGE_SECTION_HEADER NewSection = &SectionHeaders[NtHeaders->FileHeader.NumberOfSections];

	// д�������
	memset(NewSection, 0, sizeof(IMAGE_SECTION_HEADER));
	memcpy(NewSection->Name, SectionName, min(strlen(SectionName), 8));

	// д��ε���ʵ��С
	NewSection->Misc.VirtualSize = SectionSize;

	// д��ε���ʵ��ַ
	DWORD VirtualAddress = LastSection->VirtualAddress + LastSection->Misc.VirtualSize;
	NewSection->VirtualAddress = ALIGN(VirtualAddress, SectionAlignment);

	// д��ε��ļ���С
	NewSection->SizeOfRawData = ALIGN(SectionSize, FileAlignment);

	// д��ε��ļ���ַ
	DWORD PointerToRawData = LastSection->PointerToRawData + LastSection->SizeOfRawData;
	NewSection->PointerToRawData = PointerToRawData;

	// д�������
	NewSection->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;

	// �������������ļ���С
	NtHeaders->FileHeader.NumberOfSections += 1;
	NtHeaders->OptionalHeader.SizeOfImage =
		ALIGN(NtHeaders->OptionalHeader.SizeOfImage + SectionSize, SectionAlignment);

	// д�������
	memcpy((BYTE*)PeAddress + NewSection->PointerToRawData, SectionData, SectionSize);

	return TRUE;
}
