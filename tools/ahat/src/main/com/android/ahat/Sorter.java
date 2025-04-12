/*
 * Copyright (C) 2025 The Android Open Source Project
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

package com.android.ahat;

import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * UI support to allow a user to control how things are sorted.
 * You can instantiate a Sorter for a particular list of objects you want
 * sorted some way in the page output, such as rows of a table. The current
 * sort order is controlled by selecting a specific key to sort the objects
 * on.
 *
 * The Sorter class takes care of encoding the chosen sort keys and directions
 * in the URL, generating links the user can click on to select the sort key
 * and direction, and performing a sort based on the currently selected
 * settings.
 *
 * Typical usage is to construct the Sorter for a specific page query, add one
 * or more sort keys, get links associated for those keys to display on the
 * page, and finally call the sort method to sort the objects in question.
 *
 * The Sorter will ignore any sort keys in the URL that it doesn't know about.
 * That makes it possible to have multiple different sorters on the same page,
 * as long as they don't have any sort keys in common.
 */
class Sorter<T> {
  private Query mQuery;
  private Map<String, Comparator<T>> mKeys;
  private Comparator<T> mDefaultCompare;

  /**
   * Constructs a sorter object.
   * @param query A query for the page containing the sort specification.
   * @param defaultCompare Default comparison to use for sort.
   */
  Sorter(Query query, Comparator<T> defaultCompare) {
    mQuery = query;
    mKeys = new HashMap<>();
    mDefaultCompare = defaultCompare;
  }

  /**
   * Defines a sort key to use in this sorter.
   * @param key A unique string used to identify the sort key.
   * @param compare Comparator to use if the sort key is selected.
   */
  void addKey(String key, Comparator<T> compare) {
    mKeys.put(key, compare);
  }

  /**
   * Returns a sort link for the given sort key.
   *
   * @param key the sort key for the link to control.
   * @param text the display contents for the link.
   *
   * @return A link that users can click to control the sort key.
   */
  DocString link(String key, String text) {
    if (!mKeys.containsKey(key)) {
      // We know nothing about this key. No need to link anything.
      return DocString.text(text);
    }

    String sort = mQuery.get("sort", null);
    boolean sorting = sort != null && key.equals(sort.substring(1));

    if (sorting && sort.startsWith("-")) {
      // Descending sort is currently enabled on this key. Add a link that
      // enables ascending sort when you click on it.
      return DocString.link(mQuery.with("sort", "+" + key), DocString.text(text + "↓"));
    }

    if (sorting && sort.startsWith("+")) {
      // Ascending sort is currently enabled on this key. Add a link that
      // reverts back to the default sort when you click on it.
      return DocString.link(mQuery.with("sort", null), DocString.text(text + "↑"));
    }

    // No sort is currently enabled on this key. Add a link that enables
    // descending sort when you click on it.
    return DocString.link(mQuery.with("sort", "-" + key), DocString.text(text));
  }

  /**
   * Sorts the given list based on the current sort options.
   */
  void sort(List<T> list) {
    Comparator<T> compare = mDefaultCompare;

    String sort = mQuery.get("sort", null);
    if (sort != null) {
      Comparator<T> selected = mKeys.get(sort.substring(1));
      if (selected != null) {
        if (sort.startsWith("+")) {
          compare = selected.thenComparing(compare);
        } else if (sort.startsWith("-")) {
          compare = selected.reversed().thenComparing(compare);
        } else {
          // Ignore malformed sort value.
        }
      }
    }

    Collections.sort(list, compare);
  }
};
