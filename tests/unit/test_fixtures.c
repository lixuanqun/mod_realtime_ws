#include "rtw_test.h"
#include "../../src/core/rtw_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static void run(void)
{
    uint8_t payload[8];
    rtw_inbound_event_t ev;
    char *clear = read_file("tests/protocol/fixtures/clear.json");
    char *mark = read_file("tests/protocol/fixtures/mark.json");
    RTW_CHECK(clear != NULL);
    RTW_CHECK(mark != NULL);
    RTW_CHECK(rtw_parse_inbound(clear, &ev, payload, sizeof(payload)) == 0);
    RTW_CHECK(ev.type == RTW_EVT_CLEAR);
    RTW_CHECK(rtw_parse_inbound(mark, &ev, payload, sizeof(payload)) == 0);
    RTW_CHECK(ev.type == RTW_EVT_MARK);
    RTW_CHECK(strcmp(ev.mark_name, "utterance-1") == 0);
    free(clear);
    free(mark);
}

RTW_TEST_MAIN(run)
