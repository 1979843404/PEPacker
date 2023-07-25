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

/*
* align������2�Ĵη�������0x1000����ĩβ��0����
*/
#define ALIGN(x, align) ((x+align-1)&~(align-1))


BYTE* ReadPeFile(TCHAR* PePath, DWORD* PeSize);
BYTE* EncryptData(BYTE* Data, INT Length, INT* OutputLength);
BOOL PackPE(TCHAR* StubPath, TCHAR* PackedPath, PVOID SectionData, DWORD SectionSize);


int _tmain(int argc, TCHAR* argv[])
{
	if (argc != 2) {
		_tprintf(_T("[x] Usage: PEPacker.exe C:\\Path\\To\\File.exe\n"));
		return 0;
	}

	//
	// ��ȡ��Ҫ�ӿǵ�PE�ļ�
	//

	TCHAR* PePath = argv[1];

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

	PVOID SectionData = EncryptedData;
	DWORD SectionSize = (DWORD)EncryptedSize;

	TCHAR* StubPath = (TCHAR*)_T("PEStub.exe");
	TCHAR* PackedPath = (TCHAR*)_T("Packed.exe");

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


BOOL PackPE(TCHAR* StubPath, TCHAR* PackedPath, PVOID SectionData, DWORD SectionSize)
{
	// ��Stub�ļ�
	HANDLE hStubFile = CreateFile(StubPath,
		GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hStubFile == INVALID_HANDLE_VALUE) {
		_tprintf(_T("[x] CreateFile Failed, Path: %s. Error: %#x\n"), StubPath, GetLastError());
		return FALSE;
	}

	DWORD dwFileSize = GetFileSize(hStubFile, NULL);

	/*
	* �����ļ��´�С����С��Ҫ�� 0x200 ���룬����ֵһ���ǹ̶�����ģ����Ծ���ǰд����
	* ���б䶯���� NtHeaders->OptionalHeader.FileAlignment ��ֵ�޸�
	*/
	DWORD dwNewFileSize = dwFileSize + ALIGN(SectionSize, 0x200);

	// ����Packed�ļ�
	HANDLE hPackedFile = CreateFile(PackedPath,
		GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hStubFile == INVALID_HANDLE_VALUE) {
		_tprintf(_T("[x] CreateFile Failed, Path: %s. Error: %#x\n"), StubPath, GetLastError());
		CloseHandle(hStubFile);
		return FALSE;
	}

	HANDLE hFileMapping = CreateFileMapping(hPackedFile, NULL, PAGE_READWRITE, 0, dwNewFileSize, NULL);
	if (hFileMapping == NULL) {
		_tprintf(_T("[x] CreateFileMapping Failed. Error: %#x\n"), GetLastError());
		CloseHandle(hPackedFile);
		CloseHandle(hStubFile);
		return FALSE;
	}

	PBYTE PeAddress = (PBYTE)MapViewOfFile(hFileMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, dwNewFileSize);
	if (PeAddress == NULL) {
		_tprintf(_T("[x] MapViewOfFile Failed. Error: %#x\n"), GetLastError());
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		CloseHandle(hStubFile);
		return FALSE;
	}

	// ����Stub�ļ����ݵ�Packed�ļ���
	DWORD nBytesRead = 0;
	if (ReadFile(hStubFile, PeAddress, dwFileSize, &nBytesRead, NULL) == FALSE) {
		_tprintf(_T("[x] ReadFile Failed, Path: %s. Error: %#x\n"), StubPath, GetLastError());
		UnmapViewOfFile(PeAddress);
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		CloseHandle(hStubFile);
		return NULL;
	}

	CloseHandle(hStubFile);

	PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)PeAddress;
	PIMAGE_NT_HEADERS NtHeaders = (PIMAGE_NT_HEADERS)((ULONG_PTR)PeAddress + DosHeader->e_lfanew);
	PIMAGE_SECTION_HEADER SectionHeaders = (PIMAGE_SECTION_HEADER)((ULONG_PTR)NtHeaders + sizeof(IMAGE_NT_HEADERS));

	// �ж��Ƿ��Ѿ����ڼӿǶν�
	for (int i = 0; i <= NtHeaders->FileHeader.NumberOfSections; i++) {
		if (strcmp((CHAR*)SectionHeaders[i].Name, SECTION_NAME) == 0) {
			_tprintf(_T("[x] Section Name Conflict Found: %hs.\n"), SECTION_NAME);
			UnmapViewOfFile(PeAddress);
			CloseHandle(hFileMapping);
			CloseHandle(hPackedFile);
			return FALSE;
		}
	}

	// �жϿ�϶�Ƿ��㹻װ���µĶν�
	SIZE_T AvailableSpace = (ULONG_PTR)PeAddress + SectionHeaders[0].PointerToRawData
		- (ULONG_PTR)&SectionHeaders[NtHeaders->FileHeader.NumberOfSections];
	if (AvailableSpace < sizeof(IMAGE_SECTION_HEADER)) {
		_tprintf(_T("[x] Remaining Space Is Not Enough To Add New Section.\n"));
		UnmapViewOfFile(PeAddress);
		CloseHandle(hFileMapping);
		CloseHandle(hPackedFile);
		return FALSE;
	}

	// ��ȡ����ֵ
	DWORD SectionAlignment = NtHeaders->OptionalHeader.SectionAlignment;
	DWORD FileAlignment = NtHeaders->OptionalHeader.FileAlignment;

	PIMAGE_SECTION_HEADER LastSection = &SectionHeaders[NtHeaders->FileHeader.NumberOfSections - 1];
	PIMAGE_SECTION_HEADER NewSection = &SectionHeaders[NtHeaders->FileHeader.NumberOfSections];

	// �����ν�
	memset(NewSection, 0, sizeof(IMAGE_SECTION_HEADER));
	memcpy(NewSection->Name, SECTION_NAME, min(sizeof(SECTION_NAME), 8));
	NewSection->Misc.VirtualSize = SectionSize;
	DWORD VirtualAddress = LastSection->VirtualAddress + LastSection->Misc.VirtualSize;
	NewSection->VirtualAddress = ALIGN(VirtualAddress, SectionAlignment);
	NewSection->SizeOfRawData = ALIGN(SectionSize, FileAlignment);
	DWORD PointerToRawData = LastSection->PointerToRawData + LastSection->SizeOfRawData;
	NewSection->PointerToRawData = PointerToRawData;
	NewSection->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
	NtHeaders->FileHeader.NumberOfSections += 1;
	NtHeaders->OptionalHeader.SizeOfImage =
		ALIGN(NtHeaders->OptionalHeader.SizeOfImage + SectionSize, SectionAlignment);
	memcpy((BYTE*)PeAddress + NewSection->PointerToRawData, SectionData, SectionSize);

	// �ͷ���Դ
	UnmapViewOfFile(PeAddress);
	CloseHandle(hFileMapping);
	CloseHandle(hPackedFile);

	return TRUE;
}
