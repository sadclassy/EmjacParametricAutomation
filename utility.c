#include "utility.h"
#include "symboltable.h"


wchar_t selectedTabFilePath[MAX_PATH] = L"";


// Print function to display messages in creo
ProError ProGenericMsg(wchar_t* wMsg)
{
	char szMsg[MAX_MSG_BUFFER_SIZE] = { 0 };
	ProWstringToString(szMsg, wMsg);

	// Ensure newline
	strncat_s(szMsg, MAX_MSG_BUFFER_SIZE, "\n", 1);

	// Display message using ProMessageDisplay
	ProMessageDisplay(wMsgFile, "EmjacParametricAutomation %0s", szMsg);

	return PRO_TK_NO_ERROR;
}


void ProPrintf(const wchar_t* format, ...)
{
	wchar_t wbuffer[MAX_MSG_BUFFER_SIZE];
	char buffer[MAX_MSG_BUFFER_SIZE] = { 0 };
	va_list args;
	va_start(args, format);
	vswprintf(wbuffer, MAX_MSG_BUFFER_SIZE, format, args);
	va_end(args);

	// Add newline if not present
	size_t len = wcslen(wbuffer);
	if (len < MAX_MSG_BUFFER_SIZE - 1 && wbuffer[len - 1] != L'\n') {
		wbuffer[len] = L'\n';
		wbuffer[len + 1] = L'\0';
	}

	FILE* log = NULL;
	if (fopen_s(&log, "log.txt", "a") == 0) {
		fwprintf(log, L"%ls\n", wbuffer);
		fclose(log);
	}

	// Convert to multi-byte and display in Creo
	ProWstringToString(buffer, wbuffer);
	ProMessageDisplay(wMsgFile, "EmjacParametricAutomation %0s", buffer);


}

void ProPrintfChar(const char* format, ...)
{
	char buffer[MAX_MSG_BUFFER_SIZE];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, MAX_MSG_BUFFER_SIZE, format, args);
	va_end(args);

	size_t len = strlen(buffer);
	if (len > 0 && buffer[len - 1] != '\n' && len < MAX_MSG_BUFFER_SIZE - 1) {
		buffer[len] = '\n';
		buffer[len + 1] = '\0';
	}

	FILE* log = NULL;
	if (fopen_s(&log, "log.txt", "a") == 0) {
		fprintf(log, "%s", buffer);
		fclose(log);
	}

	// Display directly using the char* buffer
	ProMessageDisplay(wMsgFile, "EmjacParametricAutomation %0s", buffer);
}

void LogOnlyPrintf(const wchar_t* format, ...)
{
	wchar_t wbuffer[MAX_MSG_BUFFER_SIZE];
	va_list args;
	va_start(args, format);
	vswprintf(wbuffer, MAX_MSG_BUFFER_SIZE, format, args);
	va_end(args);

	// Add newline if not present
	size_t len = wcslen(wbuffer);
	if (len < MAX_MSG_BUFFER_SIZE - 1 && wbuffer[len - 1] != L'\n') {
		wbuffer[len] = L'\n';
		wbuffer[len + 1] = L'\0';
	}

	FILE* log = NULL;
	if (fopen_s(&log, "log.txt", "a") == 0) {
		fwprintf(log, L"%ls\n", wbuffer);
		fclose(log);
	}
}

void LogOnlyPrintfChar(const char* format, ...)
{
	char buffer[MAX_MSG_BUFFER_SIZE];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, MAX_MSG_BUFFER_SIZE, format, args);
	va_end(args);

	size_t len = strlen(buffer);
	if (len > 0 && buffer[len - 1] != '\n' && len < MAX_MSG_BUFFER_SIZE - 1) {
		buffer[len] = '\n';
		buffer[len + 1] = '\0';
	}

	FILE* log = NULL;
	if (fopen_s(&log, "log.txt", "a") == 0) {
		fprintf(log, "%s", buffer);
		fclose(log);
	}
}


wchar_t* char_to_wchar(const char* str) {
	if (!str) return NULL;
	int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0); // Get required length
	if (len == 0) return NULL;
	wchar_t* wstr = (wchar_t*)malloc(len * sizeof(wchar_t));
	if (!wstr) return NULL;
	MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len); // Perform conversion
	return wstr;
}

char* wchar_to_char(const wchar_t* wstr) {
	if (!wstr) return NULL;
	int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL); // Get required length
	if (len == 0) return NULL;
	char* str = (char*)malloc(len); // Allocate for UTF-8 bytes, including null terminator
	if (!str) return NULL;
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL); // Perform conversion
	return str;
}

// Helper function to convert string to lowercase (add this if not available)
void to_lowercase(char* str) {
	if (!str) return;
	for (; *str; ++str) {
		*str = (char)tolower((unsigned char)*str);
	}
}

// Get image size
bool get_gif_dimensions(const char* filepath, int* width, int* height)
{
	FILE* fp = NULL;
	errno_t err = fopen_s(&fp, filepath, "rb");
	if (err != 0 || fp == NULL) {
		char errorMsg[256];
		strerror_s(errorMsg, 256, err);
		ProPrintfChar("Could not open file '%s'. Error: %s\n", filepath, errorMsg);
		return false;
	}

	// Buffer size increased to 64 bytes to safely handle BMP (26 bytes) and PNG (24 bytes)
	unsigned char header[64];
	size_t bytesRead = fread(header, 1, 64, fp);
	fclose(fp); // Close file immediately after reading

	// Check minimum bytes for GIF (10 bytes)
	if (bytesRead < 10) {
		ProPrintfChar("File too short.");
		return false;
	}

	// GIF detection and parsing
	if (header[0] == 'G' && header[1] == 'I' && header[2] == 'F') {
		*width = header[6] | (header[7] << 8);  // Little-endian, 2 bytes
		*height = header[8] | (header[9] << 8); // Little-endian, 2 bytes
	}
	// BMP detection and parsing
	else if (header[0] == 'B' && header[1] == 'M') {
		if (bytesRead < 26) {
			ProPrintfChar("File too short for BMP.");
			return false;
		}
		*width = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);  // Little-endian, 4 bytes
		*height = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24); // Little-endian, 4 bytes
	}
	// PNG detection and parsing
	else if (memcmp(header, "\x89PNG\r\n\x1a\n", 8) == 0) {
		if (bytesRead < 24) {
			ProPrintfChar("File too short for PNG.");
			return false;
		}
		// Verify IHDR chunk
		if (memcmp(header + 12, "IHDR", 4) != 0) {
			ProPrintfChar("Invalid PNG: No IHDR chunk.");
			return false;
		}
		*width = (header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19];   // Big-endian, 4 bytes
		*height = (header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23]; // Big-endian, 4 bytes
	}
	else {
		ProPrintfChar("Unknown image format.");
		return false;
	}

	// Validate dimensions
	if (*width <= 0 || *height <= 0 || *width > 10000 || *height > 10000) {
		ProPrintfChar("Invalid dimensions.");
		return false;
	}

	return true;
}

int starts_with(const char* str, const char* prefix) {
	size_t len_prefix = strlen(prefix);
	return strncmp(str, prefix, len_prefix) == 0;
}