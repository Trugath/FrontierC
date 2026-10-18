/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Windows x64 startup refers to __C_specific_handler, and this compiler
 * imports memchr, strchr, strrchr, and strstr from the Universal CRT's
 * private API set. That DLL is not part of Windows 10. These local copies
 * let the executable keep the public Universal CRT, which Windows 10 and
 * Windows 11 both include. */
typedef int fe2_exception_disposition;
typedef unsigned long long fe2_size;
struct fe2_exception_record;
struct fe2_context;
struct fe2_dispatcher_context;

fe2_exception_disposition __cdecl __C_specific_handler(
    struct fe2_exception_record *record, void *frame, struct fe2_context *context,
    struct fe2_dispatcher_context *dispatch)
{
    (void)record;
    (void)frame;
    (void)context;
    (void)dispatch;
    return 1; /* ExceptionContinueSearch */
}

/* Translation units that include <windows.h> import this name. Defining the
 * import pointer here keeps the linker from pulling in the private CRT DLL. */
fe2_exception_disposition (__cdecl *__imp___C_specific_handler)(
    struct fe2_exception_record *, void *, struct fe2_context *,
    struct fe2_dispatcher_context *) = __C_specific_handler;

void *memchr(const void *source, int value, fe2_size count)
{
    const unsigned char *bytes = source;
    unsigned char want = (unsigned char)value;
    fe2_size index;
    for (index = 0; index < count; ++index)
        if (bytes[index] == want) return (void *)(bytes + index);
    return 0;
}

char *strchr(const char *text, int value)
{
    unsigned char want = (unsigned char)value;
    while (*text != 0) {
        if ((unsigned char)*text == want) return (char *)text;
        ++text;
    }
    return want == 0 ? (char *)text : 0;
}

char *strrchr(const char *text, int value)
{
    const char *found = 0;
    unsigned char want = (unsigned char)value;
    while (*text != 0) {
        if ((unsigned char)*text == want) found = text;
        ++text;
    }
    if (want == 0) return (char *)text;
    return (char *)found;
}

char *strstr(const char *text, const char *needle)
{
    fe2_size needle_length = 0;
    fe2_size index;
    if (needle[0] == 0) return (char *)text;
    while (needle[needle_length] != 0) ++needle_length;
    for (; *text != 0; ++text) {
        for (index = 0; index < needle_length; ++index)
            if (text[index] != needle[index]) break;
        if (index == needle_length) return (char *)text;
    }
    return 0;
}

/* Call sites compiled against <string.h> import these names. */
void *(__cdecl *__imp_memchr)(const void *, int, fe2_size) = memchr;
char *(__cdecl *__imp_strchr)(const char *, int) = strchr;
char *(__cdecl *__imp_strrchr)(const char *, int) = strrchr;
char *(__cdecl *__imp_strstr)(const char *, const char *) = strstr;
