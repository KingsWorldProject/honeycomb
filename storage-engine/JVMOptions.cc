/*
 * Copyright (C) 2013 Near Infinity Corporation
 *
 * This file is part of Honeycomb Storage Engine.
 *
 * Honeycomb Storage Engine is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Honeycomb Storage Engine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Honeycomb Storage Engine.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "JVMOptions.h"
#include "Logging.h"

#include <cstring>
#include <cstdlib>
#include <jni.h>
#include <cctype>

#define OPTION_SEPARATOR " -"
#define CLASSPATH_PREFIX "-Djava.class.path="
#define NEXT_ARGUMENT(arg) arg += 2

class JVMOptionsPrivate
{
  private:
    int index;
  public:
    JavaVMOption* options;
    int count;
    JVMOptionsPrivate(const int count) : 
      index(0),
      options(new JavaVMOption[count]), 
      count(count)
    {
    }

    ~JVMOptionsPrivate()
    {
      for(int i = 0; i < count; i++)
      {
        free(options[i].optionString);
      }

      delete[] options;
    }

    void add_option_string(char* option_string)
    {
      options[index++].optionString = option_string;
    }
};

/**
 * @brief Trim whitespace from left and right of string.
 */
static char* trim(char *string)
{
  if (string == NULL) return string;

  while(isspace(*string)) string++;

 if(*string == 0) return string;

  char *right = string + strlen(string) - 1;
  while(right > string && isspace(*right)) right--;

  *(right + 1) = '\0';

  return string;
}

static int calc_option_count(const char* classpath, const char* jvm_opts)
{
  int count = 0;
  if (classpath != NULL && strlen(classpath) != 0) 
    count++;
  if (jvm_opts == NULL || strlen(jvm_opts) == 0) 
    return count;
  char* ptr = const_cast<char*>(jvm_opts);
  while((ptr = strstr(ptr, " -")))
  {
    NEXT_ARGUMENT(ptr);
    count++;
  }
  count++;

  return count;
}

JVMOptions::JVMOptions() 
{
  char* jvm_opts = trim(getenv(JVM_OPTS));
  char* classpath = trim(getenv(CLASSPATH));
  int count = calc_option_count(classpath, jvm_opts);
  internal = new JVMOptionsPrivate(count);
  internal->options = new JavaVMOption[count];
  extract_classpath(classpath);
  extract_options(jvm_opts);
}

JVMOptions::~JVMOptions()
{
  delete internal;
}

JavaVMOption* JVMOptions::get_options() const
{
  return internal->options;
}

unsigned int JVMOptions::get_options_count() const
{
  return internal->count;
}

void JVMOptions::extract_classpath(const char* classpath)
{
  if (classpath == NULL || strlen(classpath) == 0)
  {
    Logging::error(CLASSPATH " environment variable not set");
    return;
  }

  const int classpath_len = 1 + strlen(CLASSPATH_PREFIX) + strlen(classpath);
  char* full_classpath = new char[classpath_len];
  snprintf(full_classpath, classpath_len, "%s%s", CLASSPATH_PREFIX, classpath);
  internal->add_option_string(full_classpath);
}

void JVMOptions::extract_options(const char* jvm_opts)
{
  if (jvm_opts == NULL || strlen(jvm_opts) == 0)
  {
    Logging::warn(JVM_OPTS " environment variable not set");
    return;
  }

   char* left = const_cast<char*>(jvm_opts);
   char* right = const_cast<char*>(jvm_opts);

   while((right = strstr(right, OPTION_SEPARATOR)))
   {
     *right = '\0';
     internal->add_option_string(strdup(trim(left)));
     left = right + 1;
     NEXT_ARGUMENT(right);
   }

   internal->add_option_string(strdup(trim(left)));
}

