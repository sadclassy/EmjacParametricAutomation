#include "utility.h"
#include "symboltable.h"


wchar_t selectedTabFilePath[MAX_PATH] = L"";

static SelMapEntry* g_selmap = NULL;
static size_t g_selmap_count = 0;
static int g_selmap_loaded = 0;
static char g_selmap_path[MAX_PATH] = "C:\\emjacScript\\component_engine.txt";

/* forward decls for local helpers */
static void selmap_free_all(void);
static char* dup_trim_line(const char* in);
static int is_blank_line(const char* s);


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

/* Optional: let caller override path before first use. */
void selmap_set_path(const char* path)
{
	if (!path || !path[0]) return;
	strncpy_s(g_selmap_path, sizeof(g_selmap_path), path, _TRUNCATE);
}

/* Reload on demand (clears cache) */
int selmap_reload(void)
{
	g_selmap_loaded = 0;
	selmap_free_all();
	return 0;
}

static int selmap_load_once(void)
{
	if (g_selmap_loaded) return 1;

	FILE* fp = NULL;
	errno_t e = fopen_s(&fp, g_selmap_path, "r");
	if (e != 0 || !fp) {
		ProPrintfChar("selmap: could not open '%s'\n", g_selmap_path);
		g_selmap_loaded = 1; /* avoid retry storms */
		return 0;
	}

	/* We'll read three logical lines per entry, ignoring blanks between blocks. */
	char buf1[512], buf2[512], buf3[512];
	while (1) {
		/* Seek first non-blank as line1 */
		char* p1 = NULL;
		while ((p1 = fgets(buf1, sizeof(buf1), fp)) != NULL && is_blank_line(p1)) { /* skip */ }
		if (!p1) break; /* EOF */

		char* p2 = fgets(buf2, sizeof(buf2), fp);
		char* p3 = fgets(buf3, sizeof(buf3), fp);
		if (!p2 || !p3) {
			ProPrintfChar("selmap: truncated block for key starting with '%s'\n", buf1);
			break;
		}

		char* key = dup_trim_line(p1);
		char* name = dup_trim_line(p2);
		char* confirm = dup_trim_line(p3);
		if (!key || !name || !confirm) {
			free(key); free(name); free(confirm);
			continue;
		}

		/* Accept only if name equals confirm */
		if (strcmp(name, confirm) == 0 && key[0] != '\0' && name[0] != '\0') {
			size_t new_count = g_selmap_count + 1;
			SelMapEntry* tmp = (SelMapEntry*)realloc(g_selmap, new_count * sizeof(SelMapEntry));
			if (!tmp) {
				ProPrintfChar("selmap: out of memory\n");
				free(key); free(name); free(confirm);
				break;
			}
			g_selmap = tmp;

			/* ---- IMPORTANT: zero-init the new slot before assigning ---- */
			g_selmap[g_selmap_count].key = NULL;
			g_selmap[g_selmap_count].label = NULL;

			g_selmap[g_selmap_count].key = key;
			g_selmap[g_selmap_count].label = name;
			g_selmap_count++;
		}
		else {
			/* reject; mismatch or empty */
			free(key);
			free(name);
		}
		free(confirm);

		/* Skip any blank separator lines that may follow; loop restarts to find next block */
	}

	fclose(fp);
	g_selmap_loaded = 1;
	LogOnlyPrintfChar("selmap: loaded %zu entries from '%s'\n", g_selmap_count, g_selmap_path);
	return (g_selmap_count > 0);
}

/* Returns 1 if found and *out_wlabel is set to a newly-allocated wchar_t* (caller frees).
   Returns 0 if not found (out_wlabel untouched). */
int selmap_lookup_w(const char* param, wchar_t** out_wlabel)
{
	if (!param || !out_wlabel) return 0;
	if (!g_selmap_loaded) (void)selmap_load_once();

	/* case-sensitive match to align with your parameter keys; tweak if needed */
	for (size_t i = 0; i < g_selmap_count; ++i) {
		/* guard against any hypothetical partial slot */
		if (g_selmap[i].key && strcmp(g_selmap[i].key, param) == 0) {
			*out_wlabel = char_to_wchar(g_selmap[i].label ? g_selmap[i].label : "");
			return (*out_wlabel != NULL);
		}
	}
	return 0;
}

/* ---- internals ---- */

static void selmap_free_all(void)
{
	if (!g_selmap) {
		g_selmap_count = 0;
		return;
	}
	for (size_t i = 0; i < g_selmap_count; ++i) {
		if (g_selmap[i].key) { free(g_selmap[i].key);   g_selmap[i].key = NULL; }
		if (g_selmap[i].label) { free(g_selmap[i].label); g_selmap[i].label = NULL; }
	}
	free(g_selmap);
	g_selmap = NULL;
	g_selmap_count = 0;
}

static void rstrip(char* s)
{
	if (!s) return;
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
		s[--n] = '\0';
	}
}

static char* dup_trim_line(const char* in)
{
	if (!in) return NULL;
	size_t n = strlen(in);
	while (n > 0 && (in[n - 1] == '\n' || in[n - 1] == '\r' || in[n - 1] == ' ' || in[n - 1] == '\t')) {
		--n;
	}
	size_t start = 0;
	while (start < n && (in[start] == ' ' || in[start] == '\t')) ++start;
	size_t len = (n > start) ? (n - start) : 0;

	char* out = (char*)malloc(len + 1);
	if (!out) return NULL;
	if (len) memcpy(out, in + start, len);
	out[len] = '\0';
	return out;
}

static int is_blank_line(const char* s)
{
	if (!s) return 1;
	while (*s) {
		if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') return 0;
		++s;
	}
	return 1;
}
