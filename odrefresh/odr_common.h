/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_ODREFRESH_ODR_COMMON_H_
#define ART_ODREFRESH_ODR_COMMON_H_

#include <initializer_list>
#include <string>
#include <string_view>

namespace art {
namespace odrefresh {

// Concatenates a list of strings into a single string.
std::string Concatenate(std::initializer_list<std::string_view> args);

// Quotes a path with single quotes (').
std::string QuotePath(std::string_view path);

}  // namespace odrefresh
}  // namespace art

#endif  // ART_ODREFRESH_ODR_COMMON_H_
