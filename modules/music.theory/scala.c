#include "scala.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static char* mel_scala_trim(char* s)
{
  while (*s && isspace((unsigned char)*s)) s++;
  if (!*s) return s;
  char* end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end)) end--;
  end[1] = '\0';
  return s;
}

static uint8_t mel_scala_is_comment_line(const char* line)
{
  while (*line && isspace((unsigned char)*line)) line++;
  return *line == '!' || *line == '\0';
}

static char* mel_scala_strip_comment(char* line)
{
  int in_token = 0;
  for (char* s = line; *s; s++)
  {
    if (!isspace((unsigned char)*s)) in_token = 1;
    else if (in_token)
    {
      char* p = s + 1;
      while (*p && isspace((unsigned char)*p)) p++;
      if (!*p || *p == '!') { *s = '\0'; break; }
    }
  }
  return line;
}

static int32_t mel_scala_parse_value(const char* s, mpfr_t out)
{
  mpfr_t tmp;
  mpfr_init2(tmp, 256);

  const char* slash = strchr(s, '/');
  if (slash)
  {
    size_t num_len = (size_t)(slash - s);
    char* num_buf = malloc(num_len + 1);
    memcpy(num_buf, s, num_len);
    num_buf[num_len] = '\0';

    mpfr_set_str(out, num_buf, 10, MPFR_RNDN);
    mpfr_set_str(tmp, slash + 1, 10, MPFR_RNDN);
    mpfr_div(out, out, tmp, MPFR_RNDN);

    free(num_buf);
    mpfr_clear(tmp);
    return 1;
  }

  const char* dot = strchr(s, '.');
  if (dot)
  {
    char* end;
    double d = strtod(s, &end);
    if (end == s) { mpfr_clear(tmp); return 0; }
    mpfr_set_d(out, d, MPFR_RNDN);
    mpfr_div_ui(out, out, 1200, MPFR_RNDN);
    mpfr_exp2(out, out, MPFR_RNDN);
    mpfr_clear(tmp);
    return 1;
  }

  mpfr_set_str(out, s, 10, MPFR_RNDN);
  mpfr_clear(tmp);
  return 1;
}

Mel_Tuning mel_scala_parse_to_tuning(const char* data, Mel_Hz ref_frequency)
{
  Mel_Tuning t = {0};

  size_t len = strlen(data);
  char* buf = malloc(len + 1);
  memcpy(buf, data, len + 1);

  char* cursor = buf;
  int32_t pitch_count = 0;
  int32_t value_idx = 0;
  mpfr_t* values = NULL;

  while (*cursor)
  {
    char* line_start = cursor;
    char* nl = strchr(cursor, '\n');
    if (nl) { *nl = '\0'; cursor = nl + 1; }
    else cursor += strlen(cursor);

    if (mel_scala_is_comment_line(line_start)) continue;
    mel_scala_strip_comment(line_start);
    char* trimmed = mel_scala_trim(line_start);
    if (!*trimmed) continue;

    if (pitch_count == 0)
    {
      pitch_count = atoi(trimmed);
      if (pitch_count <= 0) { free(buf); return t; }
      values = malloc((size_t)pitch_count * sizeof(mpfr_t));
      for (int32_t i = 0; i < pitch_count; i++)
        mpfr_init2(values[i], 256);
      continue;
    }

    if (value_idx < pitch_count)
    {
      if (!mel_scala_parse_value(trimmed, values[value_idx]))
        goto fail;
      value_idx++;
    }
  }

  if (value_idx != pitch_count) goto fail;

  t.kind = MEL_TUNING_CUSTOM;
  t.custom.steps_count = (uint32_t)pitch_count;
  t.custom.steps = values;
  mpfr_init2(t.custom.eq_ratio, 256);
  mpfr_set(t.custom.eq_ratio, values[pitch_count - 1], MPFR_RNDN);

  mpfr_init2(t.ref_frequency.value, 256);
  mpfr_set(t.ref_frequency.value, ref_frequency.value, MPFR_RNDN);

  free(buf);
  return t;

fail:
  free(buf);
  if (values)
  {
    for (int32_t i = 0; i < pitch_count; i++)
      mpfr_clear(values[i]);
    free(values);
  }
  return t;
}

char* mel_scala_export(const Mel_Tuning* t, const char* description)
{
  if (t->kind != MEL_TUNING_CUSTOM) return NULL;

  uint32_t count = t->custom.steps_count;
  if (count == 0) return NULL;

  char header[512];
  int header_len = snprintf(header, sizeof(header),
    "! %s\n"
    "%u\n"
    "!\n",
    description ? description : "melody scale",
    count);

  size_t est_size = header_len + count * 32 + 1;
  char* out = malloc(est_size);
  if (!out) return NULL;

  char* cursor = out;
  memcpy(cursor, header, header_len);
  cursor += header_len;

  for (uint32_t i = 0; i < count; i++)
  {
    mpfr_t cents;
    mpfr_init2(cents, 256);

    mpfr_log2(cents, t->custom.steps[i], MPFR_RNDN);
    mpfr_mul_ui(cents, cents, 1200, MPFR_RNDN);

    mpfr_exp_t exp;
    char* val_str = mpfr_get_str(NULL, &exp, 10, 6, cents, MPFR_RNDN);

    int len = strlen(val_str);

    if (exp <= 0)
    {
      cursor += sprintf(cursor, "0.");
      for (mpfr_exp_t e = exp; e < 0; e++)
        *cursor++ = '0';
      memcpy(cursor, val_str, len);
      cursor += len;
    }
    else
    {
      int int_digits = (int)exp;
      if (int_digits >= len)
      {
        memcpy(cursor, val_str, len);
        cursor += len;
        for (int j = 0; j < int_digits - len; j++)
          *cursor++ = '0';
        *cursor++ = '.';
      }
      else
      {
        memcpy(cursor, val_str, int_digits);
        cursor += int_digits;
        *cursor++ = '.';
        memcpy(cursor, val_str + int_digits, len - int_digits);
        cursor += len - int_digits;
      }
    }

    *cursor++ = '\n';

    mpfr_free_str(val_str);
    mpfr_clear(cents);
  }

  *cursor = '\0';
  return out;
}
