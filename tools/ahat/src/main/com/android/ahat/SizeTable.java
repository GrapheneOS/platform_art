/*
 * Copyright (C) 2017 The Android Open Source Project
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

import com.android.ahat.heapdump.Size;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.function.Function;

/**
 * Class for rendering a table that includes all categories of Size.
 * Two table formats are supported, one where a custom left column can be
 * added before the size columns:
 *    |left column|Java Size|Native Size|...|Total Size|custom columns...|
 *
 * The other without the custom left column:
 *    |Java Size|Native Size|...|Total Size|custom columns...|
 */
class SizeTable {
  /**
   * Specification to make a sortable size table.
   * To make the internal columns of a SizeTable sortable, you need to provide
   * the sorter instance for rows of the table, a sort key prefix that
   * SizeTable can use for generating its own sort keys, and functions to
   * extract the Size and base Size of an object.
   */
  public static class SortSpec<T> {
    public Sorter<T> sorter;
    public String sortKeyPrefix;
    public Function<T, Size> getSize;
    public Function<T, Size> getBaseSize;

    public SortSpec(Sorter<T> sorter, String sortKeyPrefix, Function<T, Size> getSize,
        Function<T, Size> getBaseSize) {
      this.sorter = sorter;
      this.sortKeyPrefix = sortKeyPrefix;
      this.getSize = getSize;
      this.getBaseSize = getBaseSize;
    }
  }

  /**
   * Start a sortable size table with a custom left column.
   *
   * |left column|Java Size|Native Size|...|Total Size|custom columns...|
   *
   * This should be followed by calls to the 'row' method to fill in the table
   * contents and the 'end' method to end the table.
   *
   * @param doc The output document to write to.
   * @param left The left column spec.
   * @param showDiff Set to true if size diffs should be shown.
   * @param sortSpec The sort specification.
   * @param columns Custom columns to add to the table.
   */
  static <T> void table(
      Doc doc, Column left, boolean showDiff, SortSpec<T> sortSpec, Column... columns) {
    String javaKey = sortSpec.sortKeyPrefix + ".java";
    String javaDeltaKey = sortSpec.sortKeyPrefix + ".java.delta";
    String nativeKey = sortSpec.sortKeyPrefix + ".native";
    String nativeDeltaKey = sortSpec.sortKeyPrefix + ".native.delta";
    String totalKey = sortSpec.sortKeyPrefix + ".total";
    String totalDeltaKey = sortSpec.sortKeyPrefix + ".total.delta";

    Sorter<T> sorter = sortSpec.sorter;
    sorter.addKey(javaKey, Comparator.comparingLong(x -> sortSpec.getSize.apply(x).getJavaSize()));
    sorter.addKey(javaDeltaKey,
        Comparator.comparingLong(x
            -> sortSpec.getSize.apply(x).getJavaSize()
                - sortSpec.getBaseSize.apply(x).getJavaSize()));
    sorter.addKey(nativeKey,
        Comparator.comparingLong(x -> sortSpec.getSize.apply(x).getRegisteredNativeSize()));
    sorter.addKey(nativeDeltaKey,
        Comparator.comparingLong(x
            -> sortSpec.getSize.apply(x).getRegisteredNativeSize()
                - sortSpec.getBaseSize.apply(x).getRegisteredNativeSize()));
    sorter.addKey(totalKey, Comparator.comparingLong(x -> sortSpec.getSize.apply(x).getSize()));
    sorter.addKey(totalDeltaKey,
        Comparator.comparingLong(
            x -> sortSpec.getSize.apply(x).getSize() - sortSpec.getBaseSize.apply(x).getSize()));

    List<Column> cols = new ArrayList<Column>();
    cols.add(left);
    cols.add(new Column(sorter.link(javaKey, "Java Size"), Column.Align.RIGHT));
    cols.add(new Column(sorter.link(javaDeltaKey, "Δ"), Column.Align.RIGHT, showDiff));
    cols.add(new Column(sorter.link(nativeKey, "Registered Native Size"), Column.Align.RIGHT));
    cols.add(new Column(sorter.link(nativeDeltaKey, "Δ"), Column.Align.RIGHT, showDiff));
    cols.add(new Column(sorter.link(totalKey, "Total Size"), Column.Align.RIGHT));
    cols.add(new Column(sorter.link(totalDeltaKey, "Δ"), Column.Align.RIGHT, showDiff));
    cols.addAll(Arrays.asList(columns));
    doc.table(cols.toArray(new Column[cols.size()]));
  }

  /**
   * Start a size table with a custom left column.
   *
   * |left column|Java Size|Native Size|...|Total Size|custom columns...|
   *
   * This should be followed by calls to the 'row' method to fill in the table
   * contents and the 'end' method to end the table.
   *
   * Set showDiff to true if size diffs should be shown.
   */
  static void table(Doc doc, Column left, boolean showDiff, Column... columns) {
    List<Column> cols = new ArrayList<Column>();
    cols.add(left);
    cols.add(new Column("Java Size", Column.Align.RIGHT));
    cols.add(new Column("Δ", Column.Align.RIGHT, showDiff));
    cols.add(new Column("Registered Native Size", Column.Align.RIGHT));
    cols.add(new Column("Δ", Column.Align.RIGHT, showDiff));
    cols.add(new Column("Total Size", Column.Align.RIGHT));
    cols.add(new Column("Δ", Column.Align.RIGHT, showDiff));
    cols.addAll(Arrays.asList(columns));
    doc.table(cols.toArray(new Column[cols.size()]));
  }

  /**
   * Add a row to the currently active size table with custom left column.
   * The number of values must match the number of columns provided for the
   * currently active table.
   */
  static void row(Doc doc, DocString left, Size size, Size base, DocString... values) {
    List<DocString> vals = new ArrayList<DocString>();
    vals.add(left);
    vals.add(DocString.size(size.getJavaSize(), false));
    vals.add(DocString.delta(false, false, size.getJavaSize(), base.getJavaSize()));
    vals.add(DocString.size(size.getRegisteredNativeSize(), false));
    vals.add(DocString.delta(false, false,
          size.getRegisteredNativeSize(), base.getRegisteredNativeSize()));
    vals.add(DocString.size(size.getSize(), false));
    vals.add(DocString.delta(false, false, size.getSize(), base.getSize()));
    vals.addAll(Arrays.asList(values));
    doc.row(vals.toArray(new DocString[vals.size()]));
  }

  /**
   * Start a size table without a custom left column.
   *
   * |Java Size|Native Size|...|Total Size|custom columns...|
   * This should be followed by calls to the 'row' method to fill in the table
   * contents and the 'end' method to end the table.
   *
   * Set showDiff to true if size diffs should be shown.
   */
  static void table(Doc doc, boolean showDiff, Column... columns) {
    // Re-use the code for a size table with custom left column by having an
    // invisible custom left column.
    table(doc, new Column("", Column.Align.LEFT, false), showDiff, columns);
  }

  /**
   * Start a sortable size table without a custom left column.
   *
   * |Java Size|Native Size|...|Total Size|custom columns...|
   * This should be followed by calls to the 'row' method to fill in the table
   * contents and the 'end' method to end the table.
   *
   * Set showDiff to true if size diffs should be shown.
   */
  static <T> void table(Doc doc, boolean showDiff, SortSpec<T> sortSpec, Column... columns) {
    // Re-use the code for a size table with custom left column by having an
    // invisible custom left column.
    table(doc, new Column("", Column.Align.LEFT, false), showDiff, sortSpec, columns);
  }

  /**
   * Add a row to the currently active size table without a custom left column.
   * The number of values must match the number of columns provided for the
   * currently active table.
   */
  static void row(Doc doc, Size size, Size base, DocString... values) {
    row(doc, new DocString(), size, base, values);
  }

  /**
   * End the currently active table.
   */
  static void end(Doc doc) {
    doc.end();
  }
}
