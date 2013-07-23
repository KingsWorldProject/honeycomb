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

#ifndef JVMOPTION_H
#define JVMOPTION_H

#define JVM_OPTS "HONEYCOMB_JVM_OPTS"
#define CLASSPATH "HONEYCOMB_CLASSPATH"

struct JavaVMOption;
class JVMOptionsPrivate;

/**
 * @brief Helper class for pulling JVM options from the environment and loading
 * loading them into a JavaVMOption suitable for launching a JVM through the
 * invocation API.  Note that this class only does minimal checking of the options
 * format; it is left up to the JVM to reject bad options.  In particular, this
 * class makes the assumption that JVM options are always space separated, may
 * include spaces, and always start with a '-' character.
 */
class JVMOptions
{

  private:
    JVMOptionsPrivate* internal;

    /**
     * @brief Extract the JVM options from a string. (Changes input string)
     *
     * @param jvm_opts String with options
     */
    void extract_options(char* jvm_opts);

    /**
     * @brief Extract the classpath from a string.
     *
     * @param classpath String containing classpath
     */
    void extract_classpath(const char* classpath);

  public:
    JVMOptions();
    ~JVMOptions();

    /**
     * @brief Retrieve the JVM Options
     */
    JavaVMOption* get_options() const;

    /**
     * @brief Retrieve the number of JVM options
     */
    unsigned int get_options_count() const;
};
#endif
