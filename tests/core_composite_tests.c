/* 공통 프로토콜 시험: 제품 객체/Windows 서버 앱에 의존하지 않는다. */
#include "CUnit/CUnit.h"
#include "internals.h"
#include "tests.h"
#include <string.h>

#if defined(LWM2M_CLIENT_MODE) && !defined(LWM2M_VERSION_1_0) && \
    defined(LWM2M_SUPPORT_SENML_JSON) && defined(LWM2M_SUPPORT_SENML_CBOR)

static void paths_json_preserve_scope(void)
{
    const char input[] = "[{\"bn\":\"/27343/\",\"n\":\"0/0\"},{\"n\":\"1/0\"},"
                         "{\"bn\":\"/\",\"n\":\"27345/7/7/12\"}]";
    lwm2m_uri_t *uris = NULL;
    int count = senml_json_parse_paths((const uint8_t *)input, strlen(input), &uris);
    CU_ASSERT_EQUAL_FATAL(count, 3);
    CU_ASSERT_PTR_NOT_NULL_FATAL(uris);
    CU_ASSERT_EQUAL(uris[0].objectId, 27343);
    CU_ASSERT_EQUAL(uris[0].instanceId, 0);
    CU_ASSERT_EQUAL(uris[0].resourceId, 0);
    CU_ASSERT_EQUAL(uris[0].resourceInstanceId, LWM2M_MAX_ID);
    CU_ASSERT_EQUAL(uris[1].objectId, 27343);
    CU_ASSERT_EQUAL(uris[1].instanceId, 1);
    CU_ASSERT_EQUAL(uris[1].resourceId, 0);
    CU_ASSERT_EQUAL(uris[2].objectId, 27345);
    CU_ASSERT_EQUAL(uris[2].instanceId, 7);
    CU_ASSERT_EQUAL(uris[2].resourceId, 7);
    CU_ASSERT_EQUAL(uris[2].resourceInstanceId, 12);
    lwm2m_free(uris);
}

static void paths_cbor_golden_and_every_truncation(void)
{
    /* 독립 고정 CBOR: [{0:"/27343/0/0"},{0:"/27343/1/0"}]. */
    const uint8_t input[] = {0x82, 0xa1, 0x00, 0x6a,
        '/', '2', '7', '3', '4', '3', '/', '0', '/', '0',
        0xa1, 0x00, 0x6a, '/', '2', '7', '3', '4', '3', '/', '1', '/', '0'};
    size_t length;
    lwm2m_uri_t *uris = NULL;
    CU_ASSERT_EQUAL_FATAL(senml_cbor_parse_paths(input, sizeof(input), &uris), 2);
    CU_ASSERT_EQUAL(uris[0].objectId, 27343);
    CU_ASSERT_EQUAL(uris[0].instanceId, 0);
    CU_ASSERT_EQUAL(uris[0].resourceId, 0);
    CU_ASSERT_EQUAL(uris[0].resourceInstanceId, LWM2M_MAX_ID);
    CU_ASSERT_EQUAL(uris[1].objectId, 27343);
    CU_ASSERT_EQUAL(uris[1].instanceId, 1);
    CU_ASSERT_EQUAL(uris[1].resourceId, 0);
    CU_ASSERT_EQUAL(uris[1].resourceInstanceId, LWM2M_MAX_ID);
    lwm2m_free(uris);
    for (length = 0; length < sizeof(input); ++length)
    {
        uris = (lwm2m_uri_t *)(uintptr_t)1;
        CU_ASSERT(senml_cbor_parse_paths(input, length, &uris) < 0);
        CU_ASSERT_PTR_NULL(uris);
    }
}

static void paths_reject_values_and_invalid_json(void)
{
    const char *invalid[] = {
        "", "[]", "[{}]", "[{\"n\":\"/3/0/9\",\"v\":0}]",
        "[{\"n\":\"/3/0/9\",\"vb\":false}]", "[{\"n\":\"/3/0/9\",\"vs\":\"\"}]",
        "[{\"n\":\"/3/0/9\",\"vd\":\"\"}]", "[{\"bn\":\"/3/0/\",\"n\":\"9\",\"bv\":2}]",
        "[{\"n\":\"/3/0/9\"}]x", "[{\"n\":\"/3/0/9/0/1\"}]",
        "[{\"n\":\"/65536/0/1\"}]", "[{\"n\":\"/-1/0/1\"}]"
    };
    size_t i;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        lwm2m_uri_t *uris = (lwm2m_uri_t *)(uintptr_t)1;
        CU_ASSERT(senml_json_parse_paths((const uint8_t *)invalid[i], strlen(invalid[i]), &uris) < 0);
        CU_ASSERT_PTR_NULL(uris);
    }
}

static void paths_preserve_explicit_security_before_normalization(void)
{
    const char input[] = "[{\"n\":\"/\"},{\"n\":\"/0/1/5\"},{\"n\":\"/27343\"},"
                         "{\"n\":\"/27343/0\"},{\"n\":\"/27343/0\"}]";
    lwm2m_uri_t *uris = NULL;
    CU_ASSERT_EQUAL_FATAL(senml_json_parse_paths((const uint8_t *)input, strlen(input), &uris), 5);
    CU_ASSERT_FALSE(LWM2M_URI_IS_SET_OBJECT(uris));
    CU_ASSERT_EQUAL(uris[1].objectId, 0);
    CU_ASSERT_FALSE(LWM2M_URI_IS_SET_INSTANCE(uris + 2));
    CU_ASSERT_EQUAL(uris[3].instanceId, 0);
    CU_ASSERT_EQUAL(uris[4].instanceId, 0);
    lwm2m_free(uris);
}

static void paths_reject_cbor_values_and_trailing_bytes(void)
{
    const uint8_t value[] = {0x81, 0xa2, 0, 0x66, '/', '3', '/', '0', '/', '9', 2, 0};
    const uint8_t trailing[] = {0x81, 0xa1, 0, 0x66, '/', '3', '/', '0', '/', '9', 0};
    lwm2m_uri_t *uris = NULL;
    CU_ASSERT(senml_cbor_parse_paths(value, sizeof(value), &uris) < 0);
    CU_ASSERT_PTR_NULL(uris);
    CU_ASSERT(senml_cbor_parse_paths(trailing, sizeof(trailing), &uris) < 0);
    CU_ASSERT_PTR_NULL(uris);
}

static void paths_limit_and_holes(void)
{
    senml_record_t records[LWM2M_COMPOSITE_MAX_PATHS + 1];
    lwm2m_uri_t *uris = NULL;
    int i;
    memset(records, 0, sizeof(records));
    for (i = 0; i <= LWM2M_COMPOSITE_MAX_PATHS; ++i)
    {
        memset(records[i].ids, 0xff, sizeof(records[i].ids));
        records[i].ids[0] = 27343;
        records[i].ids[1] = (uint16_t)i;
        records[i].pathPresent = true;
    }
    CU_ASSERT_EQUAL(senml_records_to_paths(records, LWM2M_COMPOSITE_MAX_PATHS, &uris),
                    LWM2M_COMPOSITE_MAX_PATHS);
    lwm2m_free(uris);
    CU_ASSERT_EQUAL(senml_records_to_paths(records, LWM2M_COMPOSITE_MAX_PATHS + 1, &uris), -3);
    CU_ASSERT_PTR_NULL(uris);
    records[0].ids[1] = LWM2M_MAX_ID;
    records[0].ids[2] = 1;
    CU_ASSERT_EQUAL(senml_records_to_paths(records, 1, &uris), -1);
    CU_ASSERT_PTR_NULL(uris);
}

static struct TestTable table[] = {
    {"Q02 JSON path scope", paths_json_preserve_scope},
    {"Q03 CBOR golden and truncation", paths_cbor_golden_and_every_truncation},
    {"Q02 invalid JSON and values", paths_reject_values_and_invalid_json},
    {"Q01 explicit security path preservation", paths_preserve_explicit_security_before_normalization},
    {"Q02 CBOR values and trailing bytes", paths_reject_cbor_values_and_trailing_bytes},
    {"Q12 path count and holes", paths_limit_and_holes},
    {NULL, NULL}
};

CU_ErrorCode create_composite_test_suit(void)
{
    CU_pSuite suite = CU_add_suite("composite", NULL, NULL);
    return suite == NULL ? CU_get_error() : add_tests(suite, table);
}
#endif
