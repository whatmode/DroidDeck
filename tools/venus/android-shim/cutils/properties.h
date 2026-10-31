/* Stand-in for AOSP's <cutils/properties.h> on top of the NDK's system property API. */
#pragma once
#include <string.h>
#include <sys/system_properties.h>
#ifndef PROPERTY_VALUE_MAX
#define PROPERTY_VALUE_MAX PROP_VALUE_MAX
#endif
#ifndef PROPERTY_KEY_MAX
#define PROPERTY_KEY_MAX PROP_NAME_MAX
#endif
static inline int property_get(const char *key, char *value, const char *default_value)
{
   int len = __system_property_get(key, value);
   if (len <= 0 && default_value) {
      strncpy(value, default_value, PROPERTY_VALUE_MAX - 1);
      value[PROPERTY_VALUE_MAX - 1] = '\0';
      len = (int) strlen(value);
   }
   return len;
}
