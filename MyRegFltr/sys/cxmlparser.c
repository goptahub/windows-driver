#include "cxmlparser.h"
#include "regfltr.h"
#include "wchar.h"
#include "log.h"


#define XML_PARSER_TAG      'lmxX'

// ============================================================================
// Simple XML Parser for Kernel Mode
// ============================================================================

#define BUF_SZ 2048
WCHAR BUF[BUF_SZ];

typedef enum {
    STATE_ROOT,
    STATE_PROCESSES,
    STATE_REGISTRY_KEYS
} ParserState;

typedef struct {
    const WCHAR* xml;
    size_t pos;
    size_t len;
    ParserState state;
    PRULES_DATA rules;
} XMLParser;

// ----------------------------------------------------------------------------
// Helper Functions (Kernel Mode)
// ----------------------------------------------------------------------------

static VOID SkipWhitespaceW(XMLParser* parser) {
    while (parser->pos < parser->len) {
        WCHAR c = parser->xml[parser->pos];
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n') {
            break;
        }
        parser->pos++;
    }
}

static WCHAR PeekCharW(XMLParser* parser) {
    if (parser->pos < parser->len) {
        return parser->xml[parser->pos];
    }
    return L'\0';
}

static WCHAR GetCharW(XMLParser* parser) {
    if (parser->pos < parser->len) {
        return parser->xml[parser->pos++];
    }
    return L'\0';
}

static int IsNameCharW(WCHAR c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
        (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.';
}

static int CompareTagW(const WCHAR* tag, const WCHAR* name) {
    // Case-insensitive comparison for kernel mode
    while (*tag && *name) {
        WCHAR t = *tag;
        WCHAR n = *name;

        // Convert to uppercase for comparison
        if (t >= L'a' && t <= L'z') t -= (L'a' - L'A');
        if (n >= L'a' && n <= L'z') n -= (L'a' - L'A');

        if (t != n) {
            return 0; // Not equal
        }
        tag++;
        name++;
    }

    // Both must end at the same time
    return (*tag == L'\0' && *name == L'\0');
}

// ----------------------------------------------------------------------------
// Main Parser Function (Kernel Mode)
// ----------------------------------------------------------------------------

NTSTATUS ParseConfigXmlW(
    const WCHAR* xmlContent,
    PRULES_DATA rules
) {
    Log(L"[*] ParseConfigXmlW called\n");

    //Log(L"[*] xmlContent:\n%s\n---------------------------------\n", xmlContent);

    if (!xmlContent || !rules) {
        return STATUS_INVALID_PARAMETER;
    }

    XMLParser parser;
    RtlZeroMemory(&parser, sizeof(parser));
    parser.xml = xmlContent;
    parser.len = wcslen(xmlContent);
    parser.rules = rules;
    parser.state = STATE_ROOT;


    WCHAR tagName[64];
    int isClosing = 0;

    while (parser.pos < parser.len) {
        //Log(L"[*] pos: %llu, len: %llu\n", parser.pos, parser.len);
        SkipWhitespaceW(&parser);
        if (PeekCharW(&parser) != L'<') {
            // Skip text content
            while (parser.pos < parser.len && PeekCharW(&parser) != L'<') {
                GetCharW(&parser);
            }
            continue;
        }

        //Log(L"1\n");

        // Parse opening '<'
        if (GetCharW(&parser) != L'<') {
            continue;
        }

        //Log(L"2\n");

        // Check for closing tag
        if (PeekCharW(&parser) == L'/') {
            GetCharW(&parser);
            isClosing = 1;
        }
        else {
            isClosing = 0;
        }

        //Log(L"3\n");

        // Parse tag name
        int i = 0;
        while (IsNameCharW(PeekCharW(&parser)) &&
            i < (int)(sizeof(tagName) / sizeof(WCHAR)) - 1) {
            tagName[i++] = GetCharW(&parser);
        }
        tagName[i] = L'\0';

        // Handle self-closing tags (e.g., <process ... />)
        int isSelfClosing = 0;

        //Log(L"4\n");

        // Parse attributes and find end of tag
        while (parser.pos < parser.len && PeekCharW(&parser) != L'>' &&
            !(PeekCharW(&parser) == L'/' && parser.pos + 1 < parser.len &&
                parser.xml[parser.pos + 1] == L'>')) {

#ifdef LOGGING
            //Log(L"pos: %llu\n", parser.pos);
            /*wmemset(BUF, 0, BUF_SZ);
            if (parser.pos >= BUF_SZ) {
                Log(L"[*] ParseConfigXmlW: pos (%llu) >= BUF_SZ, truncating log copy\n", parser.pos);
                wcsncpy(BUF, parser.xml, BUF_SZ - 1);
                BUF[BUF_SZ - 1] = L'\0';
            }
            else {
                wcsncpy(BUF, parser.xml, parser.pos);
                BUF[parser.pos] = L'\0';
            }*/
            //Log(L"processed:\n----------------------\n[%s]\n----------------------\n", BUF);
#endif
            //Log(L"41\n");

            // Check for self-closing
            if (PeekCharW(&parser) == L'/') {
                GetCharW(&parser);
                if (PeekCharW(&parser) == L'>') {
                    GetCharW(&parser);
                    isSelfClosing = 1;
                    break;
                }
            }

            //Log(L"42\n");

            // Parse attribute name
            WCHAR attrName[64] = { 0 };
            int nameLen = 0;
            SkipWhitespaceW(&parser);
            while (IsNameCharW(PeekCharW(&parser)) && nameLen < 63) {
                attrName[nameLen++] = GetCharW(&parser);
            }
            attrName[nameLen] = L'\0';

            //Log(L"43\n");

            SkipWhitespaceW(&parser);
            if (PeekCharW(&parser) == L'=') {
                GetCharW(&parser);
                SkipWhitespaceW(&parser);

                //Log(L"431\n");
                if (PeekCharW(&parser) == L'"' || PeekCharW(&parser) == L'\'') {
                    WCHAR quote = GetCharW(&parser);
                    WCHAR attrValue[256] = { 0 };
                    int valLen = 0;

                    //Log(L"44\n");
                    while (parser.pos < parser.len && PeekCharW(&parser) != quote && valLen < 255) {
                        attrValue[valLen++] = GetCharW(&parser);
                    }
                    //Log(L"45\n");
                    attrValue[valLen] = L'\0';
                    if (parser.pos < parser.len && PeekCharW(&parser) == quote) {
                        GetCharW(&parser); // Skip closing quote
                    }

                    // Process the attribute based on current state
                    if (parser.state == STATE_PROCESSES && CompareTagW(tagName, L"process")) {
                        if (CompareTagW(attrName, L"name")) {
                            if (rules->ProcessCount < MAX_PROCESSES) {
                                PROCESS_INFO* proc = &rules->Processes[rules->ProcessCount];
                                RtlStringCchCopyW(proc->Name, MAX_STRING_LEN, attrValue);
                            }
                        }
                        else if (CompareTagW(attrName, L"privilege")) {
                            if (rules->ProcessCount < MAX_PROCESSES) {
                                PROCESS_INFO* proc = &rules->Processes[rules->ProcessCount];
                                proc->Privilege = _wtoi(attrValue);
                                if (proc->Privilege < 1 || proc->Privilege > 4) {
                                    proc->Privilege = 1;
                                }
                            }
                        }
                    }
                    else if (parser.state == STATE_REGISTRY_KEYS && CompareTagW(tagName, L"key")) {
                        if (CompareTagW(attrName, L"path")) {
                            if (rules->RegistryKeyCount < MAX_REGISTRY_KEYS) {
                                REGISTRY_KEY_INFO* key = &rules->RegistryKeys[rules->RegistryKeyCount];
                                RtlStringCchCopyW(key->Path, MAX_STRING_LEN, attrValue);
                            }
                        }
                        else if (CompareTagW(attrName, L"privilege")) {
                            if (rules->RegistryKeyCount < MAX_REGISTRY_KEYS) {
                                REGISTRY_KEY_INFO* key = &rules->RegistryKeys[rules->RegistryKeyCount];
                                key->Privilege = _wtoi(attrValue);
                                if (key->Privilege < 1 || key->Privilege > 4) {
                                    key->Privilege = 1;
                                }
                            }
                        }
                    }
                }
            }
        }

        //Log(L"6\n");

        // Skip to '>' if not already there
        if (!isSelfClosing && parser.pos < parser.len && PeekCharW(&parser) != L'>') {
            while (parser.pos < parser.len && PeekCharW(&parser) != L'>') {
                if (PeekCharW(&parser) == L'/' && parser.pos + 1 < parser.len &&
                    parser.xml[parser.pos + 1] == L'>') {
                    GetCharW(&parser); // Skip '/'
                    GetCharW(&parser); // Skip '>'
                    isSelfClosing = 1;
                    break;
                }
                GetCharW(&parser);
            }
        }

        //Log(L"7\n");

        // Skip '>'
        if (!isSelfClosing && parser.pos < parser.len && PeekCharW(&parser) == L'>') {
            GetCharW(&parser);
        }

        //Log(L"8\n");

        // Update state and add entries
        if (!isClosing) {
            if (CompareTagW(tagName, L"config")) {
                parser.state = STATE_ROOT;
            }
            else if (CompareTagW(tagName, L"processes")) {
                parser.state = STATE_PROCESSES;
            }
            else if (CompareTagW(tagName, L"registry_keys")) {
                parser.state = STATE_REGISTRY_KEYS;
            }
            else if (CompareTagW(tagName, L"process") && isSelfClosing) {
                if (rules->ProcessCount < MAX_PROCESSES) {
                    // Entry already added via attributes
                    rules->ProcessCount++;
                }
            }
            else if (CompareTagW(tagName, L"key") && isSelfClosing) {
                if (rules->RegistryKeyCount < MAX_REGISTRY_KEYS) {
                    // Entry already added via attributes
                    rules->RegistryKeyCount++;
                }
            }
        }
        else {
            // Closing tag
            if (CompareTagW(tagName, L"processes")) {
                parser.state = STATE_ROOT;
            }
            else if (CompareTagW(tagName, L"registry_keys")) {
                parser.state = STATE_ROOT;
            }
            else if (CompareTagW(tagName, L"process")) {
                parser.state = STATE_PROCESSES;
            }
            else if (CompareTagW(tagName, L"key")) {
                parser.state = STATE_REGISTRY_KEYS;
            }
        }

        //Log(L"9\n");
    }

    Log(L"[+] ParseConfigXmlW succeeded\n");

    return STATUS_SUCCESS;
}

// ----------------------------------------------------------------------------
// Parse XML from registry value (kernel mode)
// ----------------------------------------------------------------------------

NTSTATUS ParseConfigXmlFromRegistry(
    HANDLE KeyHandle,
    PRULES_DATA rules
) {
    Log(L"[*] ParseConfigXmlFromRegistry called\n");
    if (!KeyHandle || !rules) {
        return STATUS_INVALID_PARAMETER;
    }

    NTSTATUS status;
    UNICODE_STRING valueName;
    PKEY_VALUE_FULL_INFORMATION valueInfo = NULL;
    ULONG valueInfoSize = 0;
    ULONG resultLength = 0;

    // Initialize value name
    RtlInitUnicodeString(&valueName, CONF_VALUE_NAME);

    Log(L"[*] ParseConfigXmlFromRegistry: ZwQueryValueKey 1 called\n");

    // First, get the size needed
    status = ZwQueryValueKey(
        KeyHandle,
        &valueName,
        KeyValueFullInformation,
        NULL,
        0,
        &resultLength
    );

    if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL) {
        if (status == STATUS_OBJECT_NAME_NOT_FOUND) {
            Log(L"[-] ParseConfigXmlFromRegistry: rule configuration doesnt exist in registry\n");
        }
        else {
            Log(L"[-] ParseConfigXmlFromRegistry: status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL: failed\n");
        }
        return status;
    }

    // Allocate buffer for value information
    valueInfoSize = resultLength;
    valueInfo = (PKEY_VALUE_FULL_INFORMATION)ExAllocatePoolZero(
        NonPagedPoolNx,
        valueInfoSize,
        XML_PARSER_TAG
    );

    if (!valueInfo) {
        Log(L"[-] ParseConfigXmlFromRegistry: !valueInfo: failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Log(L"[*] ParseConfigXmlFromRegistry: ZwQueryValueKey 2 called\n");

    // Query the value
    status = ZwQueryValueKey(
        KeyHandle,
        &valueName,
        KeyValueFullInformation,
        valueInfo,
        valueInfoSize,
        &resultLength
    );

    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(valueInfo, XML_PARSER_TAG);
        return status;
    }

    // Check if it's a string value
    if (valueInfo->Type != REG_SZ && valueInfo->Type != REG_EXPAND_SZ) {
        ExFreePoolWithTag(valueInfo, XML_PARSER_TAG);
        return STATUS_INVALID_PARAMETER;
    }

    // The data is stored after the structure
    WCHAR* xmlData = (WCHAR*)((PUCHAR)valueInfo + valueInfo->DataOffset);
    ULONG dataLength = valueInfo->DataLength / sizeof(WCHAR);

    // Ensure null termination
    if (dataLength > 0 && xmlData[dataLength - 1] != L'\0') {
        // Not null-terminated, create a copy
        WCHAR* nullTerminated = (WCHAR*)ExAllocatePoolZero(
            NonPagedPool,
            (dataLength + 1) * sizeof(WCHAR),
            XML_PARSER_TAG
        );

        if (!nullTerminated) {
            ExFreePoolWithTag(valueInfo, XML_PARSER_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(nullTerminated, xmlData, dataLength * sizeof(WCHAR));
        nullTerminated[dataLength] = L'\0';


        ExAcquireFastMutex(&g_RulesMutex);
        status = ParseConfigXmlW(nullTerminated, rules);
        ExReleaseFastMutex(&g_RulesMutex);
        ExFreePoolWithTag(nullTerminated, XML_PARSER_TAG);
    }
    else {
        // Already null-terminated
        status = ParseConfigXmlW(xmlData, rules);
    }

    ExFreePoolWithTag(valueInfo, XML_PARSER_TAG);

    Log(L"[*] ParseConfigXmlFromRegistry finished\n");

    return status;
}
