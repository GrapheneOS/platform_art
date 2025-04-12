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

import static org.junit.Assert.assertEquals;

import org.junit.Test;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;

public class SorterTest {
  // Creates a Sorter for use in tests from the given uri.
  // The default sort is ascending longs.
  // The sort key "abs" can be used to sort by absolute value.
  // The sort key "parity" can be used to sort evens before odds.
  private Sorter<Long> testSorter(String uri) throws URISyntaxException {
    Comparator<Long> normal = Comparator.comparingLong(x -> x);
    Comparator<Long> abs = Comparator.comparingLong(x -> x < 0 ? -x : x);
    Comparator<Long> parity = Comparator.comparingLong(x -> x % 2);
    Sorter<Long> sorter = new Sorter(new Query(new URI(uri)), normal);
    sorter.addKey("abs", abs);
    sorter.addKey("parity", parity);
    return sorter;
  }

  // Creates a list of longs for test purposes.
  private List<Long> testList(long... values) {
    List<Long> list = new ArrayList<Long>(values.length);
    for (int i = 0; i < values.length; ++i) {
      list.add(values[i]);
    }
    return list;
  }

  @Test
  public void defaultSort() throws URISyntaxException {
    // Default sort should be done if no key is selected.
    String uri = "http://localhost:7100/foo";
    Sorter<Long> sorter = testSorter(uri);
    List<Long> list = testList(4, 2, 5, 6, 0);
    sorter.sort(list);

    List<Long> want = testList(0, 2, 4, 5, 6);
    assertEquals(want, list);
  }

  @Test
  public void malformedSort() throws URISyntaxException {
    // Default sort should be done if selected sort is malformed.
    String uri = "http://localhost:7100/foo?sort=Xabs";
    Sorter<Long> sorter = testSorter(uri);
    List<Long> list = testList(4, 2, 5, 6, 0);
    sorter.sort(list);

    List<Long> want = testList(0, 2, 4, 5, 6);
    assertEquals(want, list);
  }

  @Test
  public void ascendingSort() throws URISyntaxException {
    String uri = "http://localhost:7100/foo?sort=+abs";
    Sorter<Long> sorter = testSorter(uri);
    List<Long> list = testList(4, -2, 5, -6, 0);
    sorter.sort(list);

    List<Long> want = testList(0, -2, 4, 5, -6);
    assertEquals(want, list);
  }

  @Test
  public void descendingSort() throws URISyntaxException {
    String uri = "http://localhost:7100/foo?sort=-abs";
    Sorter<Long> sorter = testSorter(uri);
    List<Long> list = testList(4, -2, 5, -6, 0);
    sorter.sort(list);

    List<Long> want = testList(-6, 5, 4, -2, 0);
    assertEquals(want, list);
  }

  @Test
  public void secondarySort() throws URISyntaxException {
    // The default sort should be used as a secondary sort for otherwise equal
    // keys.
    String uri = "http://localhost:7100/foo?sort=+parity";
    Sorter<Long> sorter = testSorter(uri);
    List<Long> list = testList(5, 2, 1, 6, 0);
    sorter.sort(list);

    List<Long> want = testList(0, 2, 6, 1, 5);
    assertEquals(want, list);
  }

  @Test
  public void unusedSort() throws URISyntaxException {
    // Unknown sort keys are ignored, falling back to default sort.
    String uri = "http://localhost:7100/foo?sort=whatever";
    Sorter<Long> sorter = testSorter(uri);
    List<Long> list = testList(4, 2, 5, 6, 0);
    sorter.sort(list);

    List<Long> want = testList(0, 2, 4, 5, 6);
    assertEquals(want, list);
  }

  @Test
  public void defaultLink() throws URISyntaxException {
    String uri = "http://localhost:7100/foo";
    Sorter<Long> sorter = testSorter(uri);
    DocString link = sorter.link("abs", "Hello");
    DocString want = DocString.link(new URI("/foo?sort=-abs"), DocString.text("Hello"));
    assertEquals(want, link);
  }

  @Test
  public void malformedLink() throws URISyntaxException {
    // Treat this as if there was nothing set.
    String uri = "http://localhost:7100/foo?sort=Xabs";
    Sorter<Long> sorter = testSorter(uri);
    DocString link = sorter.link("abs", "Hello");
    DocString want = DocString.link(new URI("/foo?sort=-abs"), DocString.text("Hello"));
    assertEquals(want, link);
  }

  @Test
  public void descendingLink() throws URISyntaxException {
    String uri = "http://localhost:7100/foo?sort=-abs";
    Sorter<Long> sorter = testSorter(uri);
    DocString link = sorter.link("abs", "Hello");
    DocString want = DocString.link(new URI("/foo?sort=+abs"), DocString.text("Hello↓"));
    assertEquals(want, link);
  }

  @Test
  public void ascendingLink() throws URISyntaxException {
    String uri = "http://localhost:7100/foo?sort=+abs";
    Sorter<Long> sorter = testSorter(uri);
    DocString link = sorter.link("abs", "Hello");
    DocString want = DocString.link(new URI("/foo?"), DocString.text("Hello↑"));
    assertEquals(want, link);
  }

  @Test
  public void unselectedLink() throws URISyntaxException {
    String uri = "http://localhost:7100/foo?sort=+parity";
    Sorter<Long> sorter = testSorter(uri);
    DocString link = sorter.link("abs", "Hello");
    DocString want = DocString.link(new URI("/foo?sort=-abs"), DocString.text("Hello"));
    assertEquals(want, link);
  }

  @Test
  public void unusedLink() throws URISyntaxException {
    String uri = "http://localhost:7100/foo?sort=+abs";
    Sorter<Long> sorter = testSorter(uri);
    DocString link = sorter.link("whatever", "Hello");
    DocString want = DocString.text("Hello");
    assertEquals(want, link);
  }
}
