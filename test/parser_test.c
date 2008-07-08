#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include <assert.h>
#include <glib.h>
#define assertFalse(x) assert(!(x)); printf(".")
#define assertTrue(x) assert(x); printf(".")
#define assertEquals(x, y) assert(x == y); printf(".")

void assertStrEquals(const char *expected, const char *given)
{
  int length = strlen(expected);
  assert(strncmp(expected, given, length) == 0);
  printf(".");
}

http_parser parser;

/**
 * Definition of test function
 */
void init_test(void)
{
  http_parser_init(&parser);
  assertEquals(0, parser.nread);
}


/** Parse Simple **/

void parse_simple_element_cb(void *_, int type, const char *at, size_t length)
{
  switch(type) {
  case MONGREL_CONTENT_LENGTH: assertStrEquals("12", at); break;
  case MONGREL_CONTENT_TYPE: assertStrEquals("text/html", at); break;
  case MONGREL_HTTP_VERSION:   assertStrEquals("HTTP/1.1", at); break;
  case MONGREL_REQUEST_PATH:   assertStrEquals("/", at); break;
  case MONGREL_REQUEST_METHOD: assertStrEquals("GET", at); break;
  case MONGREL_REQUEST_URI:    assertStrEquals("/", at); break;
  default:
    printf("unknown: %d\n", type);
    assert(0 && "Got an element that we didn't expect");
  }
}

void parse_simple_field_cb(void *_, const char *field, size_t flen, const char *value, size_t vlen)
{
  assert(0 && "Shouldn't call field_cb");
}

void parse_simple_test(void)
{
  const char *simple = "GET / HTTP/1.1\r\nconTENT-length: 12\r\ncontent-TYPE: text/html\r\n\r\n";
  int nread;
  http_parser_init(&parser);
  parser.on_element = parse_simple_element_cb;
  parser.http_field = parse_simple_field_cb;
  nread = http_parser_execute(&parser, simple, strlen(simple), 0);
  assertEquals(nread, strlen(simple));
  assertTrue(http_parser_is_finished(&parser));
  assertEquals(parser.content_length, 12);
}


/** parse_dumbfuck_headers 1 **/
void parse_dumbfuck_element_cb(void *_, int type, const char *at, size_t length)
{
  switch(type) {
  case MONGREL_HTTP_VERSION:   assertStrEquals("HTTP/1.1", at); break;
  case MONGREL_REQUEST_PATH:   assertStrEquals("/", at); break;
  case MONGREL_REQUEST_METHOD: assertStrEquals("GET", at); break;
  case MONGREL_REQUEST_URI:    assertStrEquals("/", at); break;
  default:
    printf("unknown: %d\n", type);
    assert(0 && "Got an element that we didn't expect");
  }
}

static int parse_dumbfuck_field_count = 0;
void parse_dumbfuck_field_cb(void *_, const char *field, size_t flen, const char *value, size_t vlen)
{
  parse_dumbfuck_field_count++;
  assertStrEquals("aaaaaaaaaaaaa", field);
  assertStrEquals("++++++++++", value);
  assertEquals(1, parse_dumbfuck_field_count);
}

void parse_dumbfuck_test(void)
{
  const char *dumbfuck = "GET / HTTP/1.1\r\naaaaaaaaaaaaa:++++++++++\r\n\r\n";
  http_parser_init(&parser);
  parser.on_element = parse_dumbfuck_element_cb;
  parser.http_field = parse_dumbfuck_field_cb;
  int nread = http_parser_execute(&parser, dumbfuck, strlen(dumbfuck), 0);
  assertEquals(nread, strlen(dumbfuck));
  assertTrue(http_parser_is_finished(&parser));
}








/** parse_dumbfuck_headers 2 **/
void donothing_element_cb(void *_, int type, const char *at, size_t length){;}

void parse_dumbfuck2_test(void)
{
  const char *dumbfuck2 = "GET / HTTP/1.1\r\nX-SSL-Bullshit:   -----BEGIN CERTIFICATE-----\r\n\tMIIFbTCCBFWgAwIBAgICH4cwDQYJKoZIhvcNAQEFBQAwcDELMAkGA1UEBhMCVUsx\r\n\tETAPBgNVBAoTCGVTY2llbmNlMRIwEAYDVQQLEwlBdXRob3JpdHkxCzAJBgNVBAMT\r\n\tAkNBMS0wKwYJKoZIhvcNAQkBFh5jYS1vcGVyYXRvckBncmlkLXN1cHBvcnQuYWMu\r\n\tdWswHhcNMDYwNzI3MTQxMzI4WhcNMDcwNzI3MTQxMzI4WjBbMQswCQYDVQQGEwJV\r\n\tSzERMA8GA1UEChMIZVNjaWVuY2UxEzARBgNVBAsTCk1hbmNoZXN0ZXIxCzAJBgNV\r\n\tBAcTmrsogriqMWLAk1DMRcwFQYDVQQDEw5taWNoYWVsIHBhcmQYJKoZIhvcNAQEB\r\n\tBQADggEPADCCAQoCggEBANPEQBgl1IaKdSS1TbhF3hEXSl72G9J+WC/1R64fAcEF\r\n\tW51rEyFYiIeZGx/BVzwXbeBoNUK41OK65sxGuflMo5gLflbwJtHBRIEKAfVVp3YR\r\n\tgW7cMA/s/XKgL1GEC7rQw8lIZT8RApukCGqOVHSi/F1SiFlPDxuDfmdiNzL31+sL\r\n\t0iwHDdNkGjy5pyBSB8Y79dsSJtCW/iaLB0/n8Sj7HgvvZJ7x0fr+RQjYOUUfrePP\r\n\tu2MSpFyf+9BbC/aXgaZuiCvSR+8Snv3xApQY+fULK/xY8h8Ua51iXoQ5jrgu2SqR\r\n\twgA7BUi3G8LFzMBl8FRCDYGUDy7M6QaHXx1ZWIPWNKsCAwEAAaOCAiQwggIgMAwG\r\n\tA1UdEwEB/wQCMAAwEQYJYIZIAYb4QgEBBAQDAgWgMA4GA1UdDwEB/wQEAwID6DAs\r\n\tBglghkgBhvhCAQ0EHxYdVUsgZS1TY2llbmNlIFVzZXIgQ2VydGlmaWNhdGUwHQYD\r\n\tVR0OBBYEFDTt/sf9PeMaZDHkUIldrDYMNTBZMIGaBgNVHSMEgZIwgY+AFAI4qxGj\r\n\tloCLDdMVKwiljjDastqooXSkcjBwMQswCQYDVQQGEwJVSzERMA8GA1UEChMIZVNj\r\n\taWVuY2UxEjAQBgNVBAsTCUF1dGhvcml0eTELMAkGA1UEAxMCQ0ExLTArBgkqhkiG\r\n\t9w0BCQEWHmNhLW9wZXJhdG9yQGdyaWQtc3VwcG9ydC5hYy51a4IBADApBgNVHRIE\r\n\tIjAggR5jYS1vcGVyYXRvckBncmlkLXN1cHBvcnQuYWMudWswGQYDVR0gBBIwEDAO\r\n\tBgwrBgEEAdkvAQEBAQYwPQYJYIZIAYb4QgEEBDAWLmh0dHA6Ly9jYS5ncmlkLXN1\r\n\tcHBvcnQuYWMudmT4sopwqlBWsvcHViL2NybC9jYWNybC5jcmwwPQYJYIZIAYb4QgEDBDAWLmh0\r\n\tdHA6Ly9jYS5ncmlkLXN1cHBvcnQuYWMudWsvcHViL2NybC9jYWNybC5jcmwwPwYD\r\n\tVR0fBDgwNjA0oDKgMIYuaHR0cDovL2NhLmdyaWQt5hYy51ay9wdWIv\r\n\tY3JsL2NhY3JsLmNybDANBgkqhkiG9w0BAQUFAAOCAQEAS/U4iiooBENGW/Hwmmd3\r\n\tXCy6Zrt08YjKCzGNjorT98g8uGsqYjSxv/hmi0qlnlHs+k/3Iobc3LjS5AMYr5L8\r\n\tUO7OSkgFFlLHQyC9JzPfmLCAugvzEbyv4Olnsr8hbxF1MbKZoQxUZtMVu29wjfXk\r\n\thTeApBv7eaKCWpSp7MCbvgzm74izKhu3vlDk9w6qVrxePfGgpKPqfHiOoGhFnbTK\r\n\twTC6o2xq5y0qZ03JonF7OJspEd3I5zKY3E+ov7/ZhW6DqT8UFvsAdjvQbXyhV8Eu\r\n\tYhixw1aKEPzNjNowuIseVogKOLXxWI5vAi5HgXdS0/ES5gDGsABo4fqovUKlgop3\r\n\tRA==\r\n\t-----END CERTIFICATE-----\r\n\r\n";
  http_parser_init(&parser);
  parser.on_element = donothing_element_cb;
  parser.http_field = 0;
  http_parser_execute(&parser, dumbfuck2, strlen(dumbfuck2), 0);
  assertTrue(http_parser_has_error(&parser));
}



/** test_fragment_in_uri **/
void fragment_in_uri_element_cb(void *_, int type, const char *at, size_t length)
{
  switch(type) {
  case MONGREL_HTTP_VERSION:   assertStrEquals("HTTP/1.1", at); break;
  case MONGREL_REQUEST_PATH:   assertStrEquals("/forums/1/topics/2375", at); break;
  case MONGREL_REQUEST_METHOD: assertStrEquals("GET", at); break;
  case MONGREL_REQUEST_URI:    assertStrEquals("/forums/1/topics/2375?page=1", at); break;
  case MONGREL_FRAGMENT:       assertStrEquals("posts-17408", at); break;
  case MONGREL_QUERY_STRING:   assertStrEquals("page=1", at); break;
  default:
    printf("unknown: %d\n", type);
    assert(0 && "Got an element that we didn't expect");
  }
}

void fragment_in_uri_test(void)
{
  const char *fragment_in_uri = "GET /forums/1/topics/2375?page=1#posts-17408 HTTP/1.1\r\n\r\n";
  http_parser_init(&parser);
  parser.on_element = fragment_in_uri_element_cb;
  parser.http_field = 0;
  http_parser_execute(&parser, fragment_in_uri, strlen(fragment_in_uri), 0);
  assertTrue(http_parser_is_finished(&parser));
}





/* very bad garbage generator */
char *rand_data(int min, int max, int readable)
{
  srand(12345);
  int count = min + (int)((rand()/(float)RAND_MAX) * max + 1)*10;
  char *out = malloc(count);
  int i;
  for(i = 0; i < count; i++) {
    if(readable) {
      out[i] = ((double)rand()/RAND_MAX)*25+'A';
    } else {
      out[i] = ((double)rand()/RAND_MAX)*100+10;
    }
  }
  out[i] = '\0';
  return out;
}

void horrible_queries_test(void) {
  int i;
  GString *req = g_string_new("");
  
  for(i = 0; i < 10; i++) {
    g_string_append_printf(req, "GET /%s HTTP/1.1\r\nX-%s: Test\r\n\r\n"
                              , rand_data(10, 120, TRUE)
                              , rand_data(1024, 1024+i*1024, TRUE)
                              );
    http_parser_init(&parser);
    parser.on_element = donothing_element_cb;
    parser.http_field = 0;
    http_parser_execute(&parser, req->str, req->len, 0);
    assertTrue(http_parser_has_error(&parser));
  }
  req->len = 0;
  
  /* then that large mangled field values are caught */
  for(i = 0; i < 10; i++) {
    g_string_append_printf(req, "GET /%s HTTP/1.1\r\nX-Test: %s\r\n\r\n"
                              , rand_data(10,120, TRUE)
                              , rand_data(1024, 1024+(i*1024), FALSE)
                              );
    http_parser_init(&parser);
    parser.on_element = donothing_element_cb;
    parser.http_field = 0;
    http_parser_execute(&parser, req->str, req->len, 0);
    assertTrue(http_parser_has_error(&parser));
  }
  req->len = 0;
  
  /* then large headers are rejected too */
  g_string_append_printf(req, "GET /%s HTTP/1.1\r\n", rand_data(10,120, TRUE));
  for(i = 0; i < 80 * 1024; i++) {
    g_string_append(req, "X-Test: test\r\n");
  }
  http_parser_init(&parser);
  parser.on_element = donothing_element_cb;
  parser.http_field = 0;
  http_parser_execute(&parser, req->str, req->len, 0);
  //assertTrue(http_parser_is_finished(&parser));
  assertTrue(http_parser_has_error(&parser));
  req->len = 0;
  
  
  /* finally just that random garbage gets blocked all the time */
  for(i = 0; i < 10; i++) {
    g_string_append_printf(req, "GET %s %s\r\n\r\n"
                              , rand_data(1024, 1024+(i*1024), FALSE)
                              , rand_data(1024, 1024+(i*1024), FALSE)
                              );
    http_parser_init(&parser);
    parser.on_element = donothing_element_cb;
    parser.http_field = 0;
    http_parser_execute(&parser, req->str, req->len, 0);
    assertTrue(http_parser_has_error(&parser));
  }
  
  g_string_free(req, TRUE);
}

int main(int argc, char *argv[])
{
  init_test();
  parse_simple_test();
  parse_dumbfuck_test();
  parse_dumbfuck2_test();
  fragment_in_uri_test();
  //horrible_queries_test();
  
  printf("\nAll tests passed!\n");
  return 0;
}
